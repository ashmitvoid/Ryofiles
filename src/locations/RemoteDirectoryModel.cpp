// SPDX-License-Identifier: GPL-3.0-only

#include <gio/gio.h>

#include "locations/RemoteDirectoryModel.hpp"

#include "locations/LocationSpec.hpp"

#include <QDateTime>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

struct RemoteDirectoryModel::ScanContext {
    QPointer<RemoteDirectoryModel> owner;
    GFile* root = nullptr;
    GFileEnumerator* enumerator = nullptr;
    GCancellable* cancellable = nullptr;
    quint64 generation = 0;
    QString uri;
    QVector<RemoteDirectoryEntry> entries;
    QString terminalError;
    bool terminalCancelled = false;
    bool superseded = false;
    bool initialPublished = false;

    ~ScanContext() {
        if (enumerator)
            g_object_unref(enumerator);
        if (cancellable)
            g_object_unref(cancellable);
        if (root)
            g_object_unref(root);
    }
};

namespace {
constexpr int kBatchSize = 128;

const char* kAttributes =
    G_FILE_ATTRIBUTE_STANDARD_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_SIZE ","
    G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
    G_FILE_ATTRIBUTE_TIME_MODIFIED;

bool entryLessThan(const RemoteDirectoryEntry& left, const RemoteDirectoryEntry& right) {
    if (left.directory != right.directory)
        return left.directory > right.directory;

    const int insensitive = QString::compare(left.name, right.name, Qt::CaseInsensitive);
    if (insensitive != 0)
        return insensitive < 0;

    const int exact = QString::compare(left.name, right.name, Qt::CaseSensitive);
    if (exact != 0)
        return exact < 0;
    return left.uri < right.uri;
}

QString utf8(const gchar* text) {
    return text ? QString::fromUtf8(text) : QString();
}

QString displayNameForInfo(GFileInfo* info) {
    QString displayName = utf8(g_file_info_get_display_name(info));
    if (!displayName.isEmpty())
        return displayName;

    const gchar* rawName = g_file_info_get_name(info);
    if (!rawName)
        return {};

    gchar* safe = g_filename_display_name(rawName);
    displayName = utf8(safe);
    g_free(safe);
    return displayName;
}
} // namespace

RemoteDirectoryModel::RemoteDirectoryModel(QObject* parent)
    : QAbstractListModel(parent) {
}

RemoteDirectoryModel::~RemoteDirectoryModel() {
    ++m_generation;
    if (!m_scan)
        return;

    ScanContext* context = m_scan;
    m_scan = nullptr;
    context->superseded = true;
    context->owner.clear();
    g_cancellable_cancel(context->cancellable);
}

int RemoteDirectoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant RemoteDirectoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const RemoteDirectoryEntry& entry = m_entries.at(index.row());
    switch (role) {
    case NameRole:
        return entry.name;
    case UriRole:
        return entry.uri;
    case DirectoryRole:
        return entry.directory;
    case SizeTextRole:
        return entry.sizeText;
    case ModifiedTextRole:
        return entry.modifiedText;
    case HiddenRole:
        return entry.hidden;
    default:
        return {};
    }
}

QHash<int, QByteArray> RemoteDirectoryModel::roleNames() const {
    return {
        {NameRole, "name"},
        {UriRole, "entryUri"},
        {DirectoryRole, "isDir"},
        {SizeTextRole, "sizeText"},
        {ModifiedTextRole, "modifiedText"},
        {HiddenRole, "isHidden"},
    };
}

void RemoteDirectoryModel::setUri(const QString& requestedUri) {
    const LocationSpec target = LocationSpec::parse(requestedUri);
    if (!target.isValid() || !target.isNetwork()) {
        setError(target.error.isEmpty()
            ? tr("Only supported network locations can be browsed")
            : target.error);
        return;
    }

    if (target.canonical == m_uri) {
        refresh();
        return;
    }

    supersedeCurrentScan();
    m_uri = target.canonical;
    emit uriChanged();

    if (!m_filterQuery.isEmpty()) {
        m_filterQuery.clear();
        emit filterQueryChanged();
    }

    m_allEntries.clear();
    replaceVisibleEntries({});
    setError({});
    startScan();
}

void RemoteDirectoryModel::setShowHidden(bool show) {
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    emit showHiddenChanged();
    rebuildVisibleEntries();
}

void RemoteDirectoryModel::setFilterQuery(const QString& query) {
    const QString normalized = query.trimmed();
    if (m_filterQuery == normalized)
        return;
    m_filterQuery = normalized;
    emit filterQueryChanged();
    rebuildVisibleEntries();
}

void RemoteDirectoryModel::refresh() {
    if (m_uri.isEmpty())
        return;
    startScan();
}

void RemoteDirectoryModel::cancel() {
    if (!m_scan) {
        ++m_generation;
        setLoading(false);
        return;
    }

    ScanContext* context = m_scan;
    m_scan = nullptr;
    context->superseded = true;
    ++m_generation;
    g_cancellable_cancel(context->cancellable);
    setLoading(false);
}

QString RemoteDirectoryModel::uriAt(int index) const {
    if (index < 0 || index >= m_entries.size())
        return {};
    return m_entries.at(index).uri;
}

bool RemoteDirectoryModel::isDirectoryAt(int index) const {
    if (index < 0 || index >= m_entries.size())
        return false;
    return m_entries.at(index).directory;
}

int RemoteDirectoryModel::indexOfUri(const QString& targetUri) const {
    if (targetUri.isEmpty())
        return -1;
    for (int index = 0; index < m_entries.size(); ++index) {
        if (m_entries.at(index).uri == targetUri)
            return index;
    }
    return -1;
}

QVector<RemoteDirectoryEntry> RemoteDirectoryModel::visibleEntries(
    QVector<RemoteDirectoryEntry> entries,
    bool showHidden,
    const QString& query) {
    const QString normalized = query.trimmed();

    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](const RemoteDirectoryEntry& entry) {
            if (!showHidden && entry.hidden)
                return true;
            return !normalized.isEmpty() &&
                !entry.name.contains(normalized, Qt::CaseInsensitive);
        }),
        entries.end());

    std::sort(entries.begin(), entries.end(), entryLessThan);
    return entries;
}

QString RemoteDirectoryModel::formatSize(qint64 bytes) {
    constexpr qreal k = 1024.0;
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / k, 'f', 1));
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (k * k), 'f', 1));
    return QStringLiteral("%1 GB").arg(QString::number(bytes / (k * k * k), 'f', 1));
}

void RemoteDirectoryModel::startScan() {
    const LocationSpec target = LocationSpec::parse(m_uri);
    if (!target.isNetwork()) {
        setError(tr("Network location is invalid"));
        setLoading(false);
        return;
    }

    supersedeCurrentScan();
    const quint64 generation = ++m_generation;

    auto* context = new ScanContext;
    context->owner = this;
    context->generation = generation;
    context->uri = target.canonical;
    context->cancellable = g_cancellable_new();

    const QByteArray uriBytes = target.canonical.toUtf8();
    context->root = g_file_new_for_uri(uriBytes.constData());
    if (!context->root || !context->cancellable) {
        delete context;
        setError(tr("Could not initialize remote directory access"));
        setLoading(false);
        return;
    }

    m_scan = context;
    setError({});
    setLoading(true);

    g_file_enumerate_children_async(
        context->root,
        kAttributes,
        G_FILE_QUERY_INFO_NONE,
        G_PRIORITY_DEFAULT,
        context->cancellable,
        +[](GObject* sourceObject, GAsyncResult* result, gpointer userData) {
            auto* context = static_cast<ScanContext*>(userData);
            GError* error = nullptr;
            GFileEnumerator* enumerator = g_file_enumerate_children_finish(
                G_FILE(sourceObject), result, &error);

            const bool cancelled = error &&
                g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
            const QString errorText = error ? QString::fromUtf8(error->message) : QString();
            if (error)
                g_error_free(error);

            if (!context) {
                if (enumerator)
                    g_object_unref(enumerator);
                return;
            }

            context->enumerator = enumerator;
            if (!context->owner) {
                delete context;
                return;
            }

            if (!enumerator) {
                context->owner->finishEnumeration(context, errorText, cancelled);
                return;
            }
            context->owner->requestNextBatch(context);
        },
        context);
}

void RemoteDirectoryModel::requestNextBatch(ScanContext* context) {
    if (!context || !context->enumerator)
        return;

    if (context != m_scan ||
        context->generation != m_generation ||
        context->superseded) {
        context->superseded = true;
        g_cancellable_cancel(context->cancellable);
        finishEnumeration(context, {}, true);
        return;
    }

    g_file_enumerator_next_files_async(
        context->enumerator,
        kBatchSize,
        G_PRIORITY_DEFAULT,
        context->cancellable,
        +[](GObject* sourceObject, GAsyncResult* result, gpointer userData) {
            auto* context = static_cast<ScanContext*>(userData);
            GError* error = nullptr;
            GList* infos = g_file_enumerator_next_files_finish(
                G_FILE_ENUMERATOR(sourceObject), result, &error);
            const bool hadInfos = infos != nullptr;

            const bool cancelled = error &&
                g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
            const QString errorText = error ? QString::fromUtf8(error->message) : QString();
            if (error)
                g_error_free(error);

            QVector<RemoteDirectoryEntry> batch;
            if (infos && context && context->enumerator) {
                for (GList* node = infos; node; node = node->next) {
                    auto* info = G_FILE_INFO(node->data);
                    GFile* child = g_file_enumerator_get_child(context->enumerator, info);
                    if (!child)
                        continue;

                    gchar* rawUri = g_file_get_uri(child);
                    const QString childUri = utf8(rawUri);
                    g_free(rawUri);
                    g_object_unref(child);

                    const LocationSpec childLocation = LocationSpec::parse(childUri);
                    if (!childLocation.isNetwork())
                        continue;

                    RemoteDirectoryEntry entry;
                    entry.name = displayNameForInfo(info);
                    entry.uri = childLocation.canonical;
                    entry.directory =
                        g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
                    entry.hidden = g_file_info_get_is_hidden(info) ||
                        entry.name.startsWith(QLatin1Char('.'));
                    entry.sizeText = entry.directory
                        ? QString()
                        : RemoteDirectoryModel::formatSize(g_file_info_get_size(info));

                    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED)) {
                        const guint64 seconds = g_file_info_get_attribute_uint64(
                            info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
                        entry.modifiedText = QDateTime::fromSecsSinceEpoch(
                            static_cast<qint64>(seconds)).toString(
                                QStringLiteral("yyyy-MM-dd HH:mm"));
                    }

                    if (!entry.name.isEmpty()) {
                        context->entries.push_back(entry);
                        batch.push_back(std::move(entry));
                    }
                }
            }

            if (infos)
                g_list_free_full(infos, g_object_unref);

            if (!context)
                return;
            if (!context->owner) {
                delete context;
                return;
            }

            if (!context->initialPublished && !batch.isEmpty()) {
                context->initialPublished = true;
                context->owner->publishInitialEntries(batch, context->generation);
            }

            if (!errorText.isEmpty() || cancelled) {
                context->owner->finishEnumeration(context, errorText, cancelled);
                return;
            }
            if (!hadInfos) {
                context->owner->finishEnumeration(context, {}, false);
                return;
            }

            context->owner->requestNextBatch(context);
        },
        context);
}

void RemoteDirectoryModel::finishEnumeration(
    ScanContext* context,
    const QString& error,
    bool cancelled) {
    if (!context)
        return;

    context->terminalError = error;
    context->terminalCancelled = cancelled || context->superseded;

    if (!context->enumerator) {
        finishAfterClose(context);
        return;
    }

    g_file_enumerator_close_async(
        context->enumerator,
        G_PRIORITY_DEFAULT,
        nullptr,
        +[](GObject* sourceObject, GAsyncResult* result, gpointer userData) {
            auto* context = static_cast<ScanContext*>(userData);
            GError* error = nullptr;
            g_file_enumerator_close_finish(
                G_FILE_ENUMERATOR(sourceObject), result, &error);
            if (error)
                g_error_free(error);

            if (!context)
                return;
            if (context->owner)
                context->owner->finishAfterClose(context);
            else
                delete context;
        },
        context);
}

void RemoteDirectoryModel::finishAfterClose(ScanContext* context) {
    if (!context)
        return;

    const bool current = context == m_scan &&
        context->generation == m_generation &&
        !context->superseded;
    if (context == m_scan)
        m_scan = nullptr;

    if (!current || context->terminalCancelled) {
        if (current)
            setLoading(false);
        delete context;
        return;
    }

    const quint64 generation = context->generation;
    const QString error = context->terminalError;
    QVector<RemoteDirectoryEntry> entries = std::move(context->entries);
    delete context;

    sortAndApplyFinal(std::move(entries), generation, error);
}

void RemoteDirectoryModel::publishInitialEntries(
    const QVector<RemoteDirectoryEntry>& entries,
    quint64 generation) {
    if (generation != m_generation || entries.isEmpty())
        return;

    m_allEntries = visibleEntries(entries, true, {});
    rebuildVisibleEntries();
}

void RemoteDirectoryModel::sortAndApplyFinal(
    QVector<RemoteDirectoryEntry> entries,
    quint64 generation,
    const QString& error) {
    auto* watcher = new QFutureWatcher<QVector<RemoteDirectoryEntry>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
        [this, watcher, generation, error] {
            QVector<RemoteDirectoryEntry> sorted = watcher->result();
            watcher->deleteLater();

            if (generation != m_generation)
                return;

            m_allEntries = std::move(sorted);
            rebuildVisibleEntries();
            setError(error);
            setLoading(false);
        });

    watcher->setFuture(QtConcurrent::run([entries = std::move(entries)]() mutable {
        std::sort(entries.begin(), entries.end(), entryLessThan);
        return entries;
    }));
}

void RemoteDirectoryModel::rebuildVisibleEntries() {
    QVector<RemoteDirectoryEntry> visible;
    visible.reserve(m_allEntries.size());
    const QString query = m_filterQuery.trimmed();

    for (const RemoteDirectoryEntry& entry : std::as_const(m_allEntries)) {
        if (!m_showHidden && entry.hidden)
            continue;
        if (!query.isEmpty() && !entry.name.contains(query, Qt::CaseInsensitive))
            continue;
        visible.push_back(entry);
    }

    replaceVisibleEntries(std::move(visible));
}

void RemoteDirectoryModel::replaceVisibleEntries(QVector<RemoteDirectoryEntry> entries) {
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    emit countChanged();
}

void RemoteDirectoryModel::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void RemoteDirectoryModel::setError(const QString& error) {
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void RemoteDirectoryModel::supersedeCurrentScan() {
    if (!m_scan)
        return;

    ScanContext* context = m_scan;
    m_scan = nullptr;
    context->superseded = true;
    g_cancellable_cancel(context->cancellable);
}
