// SPDX-License-Identifier: GPL-3.0-only

#include <gio/gio.h>

#include "RemoteOperationManager.hpp"

#include "locations/LocationSpec.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>

namespace {
QString stateText(RemoteOperationManager::OperationState state) {
    switch (state) {
    case RemoteOperationManager::Queued: return QStringLiteral("queued");
    case RemoteOperationManager::Running: return QStringLiteral("running");
    case RemoteOperationManager::Completed: return QStringLiteral("completed");
    case RemoteOperationManager::Failed: return QStringLiteral("failed");
    case RemoteOperationManager::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

QString kindText(RemoteOperationManager::OperationKind kind) {
    switch (kind) {
    case RemoteOperationManager::CopyOperation: return QStringLiteral("copy");
    case RemoteOperationManager::MoveOperation: return QStringLiteral("move");
    case RemoteOperationManager::RenameOperation: return QStringLiteral("rename");
    case RemoteOperationManager::CreateFolderOperation: return QStringLiteral("new folder");
    case RemoteOperationManager::TrashOperation: return QStringLiteral("trash");
    }
    return QStringLiteral("unknown");
}

bool cancelledError(const GError* error) {
    return error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}
} // namespace

RemoteOperationManager::ActiveContext::~ActiveContext() {
    if (cancellable)
        g_object_unref(cancellable);
    if (destinationFile)
        g_object_unref(destinationFile);
    if (sourceFile)
        g_object_unref(sourceFile);
}

RemoteOperationManager::RemoteOperationManager(QObject* parent)
    : QAbstractListModel(parent) {
}

RemoteOperationManager::~RemoteOperationManager() {
    if (!m_active)
        return;

    ActiveContext* context = m_active;
    m_active = nullptr;
    context->owner.clear();
    if (context->cancellable)
        g_cancellable_cancel(context->cancellable);
}

int RemoteOperationManager::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_jobs.size();
}

int RemoteOperationManager::activeCount() const {
    int result = 0;
    for (const auto& job : m_jobs) {
        if (!terminal(job->state))
            ++result;
    }
    return result;
}

QVariant RemoteOperationManager::data(const QModelIndex& indexValue, int role) const {
    if (!indexValue.isValid() || indexValue.row() < 0 || indexValue.row() >= m_jobs.size())
        return {};

    const auto& job = m_jobs.at(indexValue.row());
    switch (role) {
    case IdRole:
        return job->id;
    case KindRole:
        return kindText(job->kind);
    case StateRole:
        return stateText(job->state);
    case CurrentSourceRole:
        return job->currentSource;
    case DestinationRole:
        return job->destinationDirectory;
    case ProgressRole:
        return job->progress;
    case ErrorRole:
        return job->error;
    default:
        return {};
    }
}

QHash<int, QByteArray> RemoteOperationManager::roleNames() const {
    return {
        {IdRole, "jobId"},
        {KindRole, "kind"},
        {StateRole, "state"},
        {CurrentSourceRole, "currentSource"},
        {DestinationRole, "destination"},
        {ProgressRole, "progress"},
        {ErrorRole, "errorText"},
    };
}

bool RemoteOperationManager::terminal(OperationState state) {
    return state == Completed || state == Failed || state == Cancelled;
}

bool RemoteOperationManager::validLeafName(const QString& name) {
    const QString clean = name.trimmed();
    return !clean.isEmpty()
        && !clean.contains(QLatin1Char('/'))
        && clean != QStringLiteral(".")
        && clean != QStringLiteral("..");
}

QString RemoteOperationManager::normalizedLocation(
    const QString& input,
    bool* network,
    QString* error) {
    if (network)
        *network = false;
    if (error)
        error->clear();

    const LocationSpec spec = LocationSpec::parse(input);
    if (!spec.isValid()) {
        if (error)
            *error = spec.error.isEmpty()
                ? QStringLiteral("Location is invalid")
                : spec.error;
        return {};
    }

    if (network)
        *network = spec.isNetwork();
    return spec.isNetwork() ? spec.canonical : spec.localPath;
}

RemoteOperationPlan RemoteOperationManager::planTransfer(
    const QString& source,
    const QString& destinationDirectory) {
    RemoteOperationPlan plan;
    bool sourceNetwork = false;
    bool destinationNetwork = false;
    QString error;

    plan.source = normalizedLocation(source, &sourceNetwork, &error);
    if (plan.source.isEmpty()) {
        plan.error = error;
        return plan;
    }

    plan.destinationDirectory = normalizedLocation(
        destinationDirectory,
        &destinationNetwork,
        &error);
    if (plan.destinationDirectory.isEmpty()) {
        plan.error = error;
        return plan;
    }

    plan.involvesNetwork = sourceNetwork || destinationNetwork;
    if (!plan.involvesNetwork) {
        plan.error = QStringLiteral("Local-only transfers must use the local operation engine");
        return plan;
    }

    plan.valid = true;
    return plan;
}

RemoteOperationPlan RemoteOperationManager::planRemoteItem(const QString& source) {
    RemoteOperationPlan plan;
    bool network = false;
    QString error;
    plan.source = normalizedLocation(source, &network, &error);
    if (plan.source.isEmpty()) {
        plan.error = error;
        return plan;
    }
    if (!network) {
        plan.error = QStringLiteral("This operation requires a network location");
        return plan;
    }

    plan.valid = true;
    plan.involvesNetwork = true;
    return plan;
}

RemoteOperationPlan RemoteOperationManager::planCreateFolder(
    const QString& parentDirectory,
    const QString& name) {
    RemoteOperationPlan plan = planRemoteItem(parentDirectory);
    if (!plan.valid)
        return plan;

    const QString cleanName = name.trimmed();
    if (!validLeafName(cleanName)) {
        plan.valid = false;
        plan.error = QStringLiteral("Folder name is invalid");
        return plan;
    }

    plan.destinationDirectory = plan.source;
    plan.source.clear();
    plan.name = cleanName;
    return plan;
}

GFile* RemoteOperationManager::fileForLocation(const QString& location) {
    const LocationSpec spec = LocationSpec::parse(location);
    if (!spec.isValid())
        return nullptr;

    if (spec.isNetwork()) {
        const QByteArray encoded = spec.canonical.toUtf8();
        return g_file_new_for_uri(encoded.constData());
    }

    const QByteArray encoded = QFile::encodeName(spec.localPath);
    return g_file_new_for_path(encoded.constData());
}

QString RemoteOperationManager::gioErrorText(
    const GError* error,
    const QString& fallback) {
    if (!error)
        return fallback;
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        return QStringLiteral("Destination already exists; Ryofiles will not overwrite it automatically");
    const QString message = QString::fromUtf8(error->message ? error->message : "").trimmed();
    return message.isEmpty() ? fallback : message;
}

QString RemoteOperationManager::enqueue(
    OperationKind kind,
    const RemoteOperationPlan& plan,
    const QString& name) {
    if (!plan.valid)
        return {};

    pruneFinished();

    auto job = std::make_shared<Job>();
    job->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job->kind = kind;
    job->source = plan.source;
    job->destinationDirectory = plan.destinationDirectory;
    job->name = name.isEmpty() ? plan.name : name;
    job->currentSource = plan.source;

    const int row = m_jobs.size();
    beginInsertRows({}, row, row);
    m_jobs.push_back(job);
    endInsertRows();
    emit countChanged();
    emit activeCountChanged();

    startNext();
    return job->id;
}

QString RemoteOperationManager::copyFile(
    const QString& source,
    const QString& destinationDirectory) {
    return enqueue(CopyOperation, planTransfer(source, destinationDirectory));
}

QString RemoteOperationManager::moveFile(
    const QString& source,
    const QString& destinationDirectory) {
    return enqueue(MoveOperation, planTransfer(source, destinationDirectory));
}

QString RemoteOperationManager::rename(const QString& source, const QString& newName) {
    RemoteOperationPlan plan = planRemoteItem(source);
    const QString cleanName = newName.trimmed();
    if (!plan.valid || !validLeafName(cleanName))
        return {};
    plan.name = cleanName;
    return enqueue(RenameOperation, plan, cleanName);
}

QString RemoteOperationManager::createFolder(
    const QString& parentDirectory,
    const QString& name) {
    const RemoteOperationPlan plan = planCreateFolder(parentDirectory, name);
    return enqueue(CreateFolderOperation, plan, plan.name);
}

QString RemoteOperationManager::trash(const QString& source) {
    return enqueue(TrashOperation, planRemoteItem(source));
}

std::shared_ptr<RemoteOperationManager::Job> RemoteOperationManager::findJob(
    const QString& id) const {
    for (const auto& job : m_jobs) {
        if (job->id == id)
            return job;
    }
    return {};
}

int RemoteOperationManager::indexOfJob(const std::shared_ptr<Job>& job) const {
    return static_cast<int>(m_jobs.indexOf(job));
}

void RemoteOperationManager::pruneFinished(int keep) {
    int finished = 0;
    for (const auto& job : m_jobs) {
        if (terminal(job->state))
            ++finished;
    }

    for (int row = 0; row < m_jobs.size() && finished > keep;) {
        if (!terminal(m_jobs.at(row)->state)) {
            ++row;
            continue;
        }
        beginRemoveRows({}, row, row);
        m_jobs.removeAt(row);
        endRemoveRows();
        --finished;
        emit countChanged();
    }
}

void RemoteOperationManager::startNext() {
    if (m_active)
        return;

    std::shared_ptr<Job> next;
    for (const auto& job : m_jobs) {
        if (job->state == Queued) {
            next = job;
            break;
        }
    }
    if (!next)
        return;

    auto* context = new ActiveContext;
    context->owner = this;
    context->job = next;
    context->cancellable = g_cancellable_new();
    m_active = context;

    next->state = Running;
    next->error.clear();
    next->progress = 0.0;
    const int row = indexOfJob(next);
    if (row >= 0)
        emit dataChanged(index(row), index(row));
    emit activeCountChanged();

    startActive(context);
}

void RemoteOperationManager::startActive(ActiveContext* context) {
    if (!context || context != m_active)
        return;

    const auto& job = context->job;
    if (job->kind == CopyOperation || job->kind == MoveOperation) {
        context->sourceFile = fileForLocation(job->source);
        GFile* parent = fileForLocation(job->destinationDirectory);
        if (!context->sourceFile || !parent) {
            if (parent)
                g_object_unref(parent);
            finishActive(context, Failed, tr("Could not resolve transfer locations"));
            return;
        }

        gchar* basename = g_file_get_basename(context->sourceFile);
        if (!basename || basename[0] == '\0') {
            g_free(basename);
            g_object_unref(parent);
            finishActive(context, Failed, tr("Could not determine the source file name"));
            return;
        }

        context->destinationFile = g_file_get_child(parent, basename);
        g_free(basename);
        g_object_unref(parent);

        if (!context->destinationFile ||
            g_file_equal(context->sourceFile, context->destinationFile)) {
            finishActive(context, Failed, tr("Source and destination are the same file"));
            return;
        }

        startTransferTypeQuery(context);
        return;
    }

    if (job->kind == RenameOperation) {
        context->sourceFile = fileForLocation(job->source);
        if (!context->sourceFile) {
            finishActive(context, Failed, tr("Could not resolve the remote file"));
            return;
        }
        const QByteArray name = job->name.toUtf8();
        g_file_set_display_name_async(
            context->sourceFile,
            name.constData(),
            G_PRIORITY_DEFAULT,
            context->cancellable,
            &RemoteOperationManager::renameReady,
            context);
        return;
    }

    if (job->kind == CreateFolderOperation) {
        GFile* parent = fileForLocation(job->destinationDirectory);
        if (!parent) {
            finishActive(context, Failed, tr("Could not resolve the remote folder"));
            return;
        }

        GError* error = nullptr;
        const QByteArray name = job->name.toUtf8();
        context->destinationFile = g_file_get_child_for_display_name(
            parent,
            name.constData(),
            &error);
        g_object_unref(parent);
        if (!context->destinationFile) {
            const QString message = gioErrorText(error, tr("Could not create the folder name"));
            g_clear_error(&error);
            finishActive(context, Failed, message);
            return;
        }

        g_file_make_directory_async(
            context->destinationFile,
            G_PRIORITY_DEFAULT,
            context->cancellable,
            &RemoteOperationManager::createFolderReady,
            context);
        return;
    }

    context->sourceFile = fileForLocation(job->source);
    if (!context->sourceFile) {
        finishActive(context, Failed, tr("Could not resolve the remote file"));
        return;
    }
    g_file_trash_async(
        context->sourceFile,
        G_PRIORITY_DEFAULT,
        context->cancellable,
        &RemoteOperationManager::trashReady,
        context);
}

void RemoteOperationManager::startTransferTypeQuery(ActiveContext* context) {
    g_file_query_info_async(
        context->sourceFile,
        G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        G_PRIORITY_DEFAULT,
        context->cancellable,
        &RemoteOperationManager::transferTypeReady,
        context);
}

void RemoteOperationManager::transferTypeReady(
    GObject*,
    GAsyncResult* result,
    gpointer userData) {
    auto* context = static_cast<ActiveContext*>(userData);
    GError* error = nullptr;
    GFileInfo* info = g_file_query_info_finish(context->sourceFile, result, &error);

    if (!context->owner) {
        if (info)
            g_object_unref(info);
        g_clear_error(&error);
        delete context;
        return;
    }

    RemoteOperationManager* owner = context->owner;
    if (!info) {
        const bool cancelled = cancelledError(error);
        const QString message = gioErrorText(error, owner->tr("Could not inspect the source file"));
        g_clear_error(&error);
        owner->finishActive(context, cancelled ? Cancelled : Failed, cancelled ? QString() : message);
        return;
    }

    const GFileType type = g_file_info_get_file_type(info);
    g_object_unref(info);
    if (type != G_FILE_TYPE_REGULAR) {
        owner->finishActive(
            context,
            Failed,
            owner->tr("Remote directory and special-file transfers are not available yet"));
        return;
    }

    owner->startTransfer(context);
}

void RemoteOperationManager::startTransfer(ActiveContext* context) {
    if (context->job->kind == CopyOperation) {
        g_file_copy_async(
            context->sourceFile,
            context->destinationFile,
            G_FILE_COPY_NONE,
            G_PRIORITY_DEFAULT,
            context->cancellable,
            &RemoteOperationManager::transferProgress,
            context,
            &RemoteOperationManager::copyReady,
            context);
        return;
    }

    g_file_move_async(
        context->sourceFile,
        context->destinationFile,
        G_FILE_COPY_NONE,
        G_PRIORITY_DEFAULT,
        context->cancellable,
        &RemoteOperationManager::transferProgress,
        context,
        &RemoteOperationManager::moveReady,
        context);
}

void RemoteOperationManager::transferProgress(
    goffset currentBytes,
    goffset totalBytes,
    gpointer userData) {
    auto* context = static_cast<ActiveContext*>(userData);
    if (!context->owner)
        return;
    const double progress = totalBytes > 0
        ? std::clamp(static_cast<double>(currentBytes) / static_cast<double>(totalBytes), 0.0, 1.0)
        : 0.0;
    context->owner->setProgress(context, progress);
}

void RemoteOperationManager::copyReady(
    GObject*,
    GAsyncResult* result,
    gpointer userData) {
    auto* context = static_cast<ActiveContext*>(userData);
    GError* error = nullptr;
    const gboolean ok = g_file_copy_finish(context->sourceFile, result, &error);
    if (!context->owner) {
        g_clear_error(&error);
        delete context;
        return;
    }

    RemoteOperationManager* owner = context->owner;
    const bool cancelled = cancelledError(error);
    const QString message = gioErrorText(error, owner->tr("Remote copy failed"));
    g_clear_error(&error);
    owner->finishActive(
        context,
        ok ? Completed : (cancelled ? Cancelled : Failed),
        ok || cancelled ? QString() : message);
}

void RemoteOperationManager::moveReady(
    GObject*,
    GAsyncResult* result,
    gpointer userData) {
    auto* context = static_cast<ActiveContext*>(userData);
    GError* error = nullptr;
    const gboolean ok = g_file_move_finish(context->sourceFile, result, &error);
    if (!context->owner) {
        g_clear_error(&error);
        delete context;
        return;
    }

    RemoteOperationManager* owner = context->owner;
    const bool cancelled = cancelledError(error);
    const QString message = gioErrorText(error, owner->tr("Remote move failed"));
    g_clear_error(&error);
    owner->finishActive(
        context,
        ok ? Completed : (cancelled ? Cancelled : Failed),
        ok || cancelled ? QString() : message);
}

void RemoteOperationManager::renameReady(
    GObject*,
    GAsyncResult* result,
    gpointer userData) {
    auto* context = static_cast<ActiveContext*>(userData);
    GError* error = nullptr;
    GFile* renamed = g_file_set_display_name_finish(context->sourceFile, result, &error);
    const bool ok = renamed != nullptr;
    if (renamed)
        g_object_unref(renamed);

    if (!context->owner) {
        g_clear_error(&error);
        delete context;
        return;
    }

    RemoteOperationManager* owner = context->owner;
    const bool cancelled = cancelledError(error);
    const QString message = gioErrorText(error, owner->tr("Remote rename failed"));
    g_clear_error(&error);
    owner->finishActive(
        context,
        ok ? Completed : (cancelled ? Cancelled : Failed),
        ok || cancelled ? QString() : message);
}

void RemoteOperationManager::createFolderReady(
    GObject*,
    GAsyncResult* result,
    gpointer userData) {
    auto* context = static_cast<ActiveContext*>(userData);
    GError* error = nullptr;
    const gboolean ok = g_file_make_directory_finish(context->destinationFile, result, &error);
    if (!context->owner) {
        g_clear_error(&error);
        delete context;
        return;
    }

    RemoteOperationManager* owner = context->owner;
    const bool cancelled = cancelledError(error);
    const QString message = gioErrorText(error, owner->tr("Remote folder creation failed"));
    g_clear_error(&error);
    owner->finishActive(
        context,
        ok ? Completed : (cancelled ? Cancelled : Failed),
        ok || cancelled ? QString() : message);
}

void RemoteOperationManager::trashReady(
    GObject*,
    GAsyncResult* result,
    gpointer userData) {
    auto* context = static_cast<ActiveContext*>(userData);
    GError* error = nullptr;
    const gboolean ok = g_file_trash_finish(context->sourceFile, result, &error);
    if (!context->owner) {
        g_clear_error(&error);
        delete context;
        return;
    }

    RemoteOperationManager* owner = context->owner;
    const bool cancelled = cancelledError(error);
    const QString message = gioErrorText(error, owner->tr("Remote Trash operation failed"));
    g_clear_error(&error);
    owner->finishActive(
        context,
        ok ? Completed : (cancelled ? Cancelled : Failed),
        ok || cancelled ? QString() : message);
}

void RemoteOperationManager::setProgress(ActiveContext* context, double progress) {
    if (!context || context != m_active)
        return;
    const int row = indexOfJob(context->job);
    if (row < 0)
        return;

    const double bounded = std::clamp(progress, 0.0, 1.0);
    if (qFuzzyCompare(context->job->progress + 1.0, bounded + 1.0))
        return;
    context->job->progress = bounded;
    emit dataChanged(index(row), index(row), {ProgressRole});
}

void RemoteOperationManager::finishActive(
    ActiveContext* context,
    OperationState state,
    const QString& error) {
    if (!context)
        return;
    if (context != m_active) {
        delete context;
        return;
    }

    const auto job = context->job;
    m_active = nullptr;
    job->state = state;
    job->error = error;
    if (state == Completed)
        job->progress = 1.0;

    const int row = indexOfJob(job);
    if (row >= 0)
        emit dataChanged(index(row), index(row));
    emit activeCountChanged();
    emit jobFinished(job->id, state == Completed);

    delete context;
    startNext();
}

void RemoteOperationManager::cancel(const QString& jobId) {
    const auto job = findJob(jobId);
    if (!job || terminal(job->state))
        return;

    if (m_active && m_active->job == job) {
        if (m_active->cancellable)
            g_cancellable_cancel(m_active->cancellable);
        return;
    }

    if (job->state == Queued) {
        job->state = Cancelled;
        const int row = indexOfJob(job);
        if (row >= 0)
            emit dataChanged(index(row), index(row));
        emit activeCountChanged();
        emit jobFinished(job->id, false);
    }
}

QString RemoteOperationManager::errorFor(const QString& jobId) const {
    const auto job = findJob(jobId);
    return job ? job->error : QString();
}

void RemoteOperationManager::dismiss(const QString& jobId) {
    for (int row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row)->id != jobId)
            continue;
        if (!terminal(m_jobs.at(row)->state))
            return;
        beginRemoveRows({}, row, row);
        m_jobs.removeAt(row);
        endRemoveRows();
        emit countChanged();
        return;
    }
}

void RemoteOperationManager::clearFinished() {
    bool changed = false;
    for (int row = m_jobs.size() - 1; row >= 0; --row) {
        if (!terminal(m_jobs.at(row)->state))
            continue;
        beginRemoveRows({}, row, row);
        m_jobs.removeAt(row);
        endRemoveRows();
        changed = true;
    }
    if (changed)
        emit countChanged();
}
