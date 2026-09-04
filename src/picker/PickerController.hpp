// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "locations/LocalPathGuard.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QObject>
#include <QSet>
#include <QStringList>

class PickerController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ modeName CONSTANT)
    Q_PROPERTY(bool folderMode READ folderMode CONSTANT)
    Q_PROPERTY(bool multiple READ multiple CONSTANT)
    Q_PROPERTY(QString initialDirectory READ initialDirectory CONSTANT)
    Q_PROPERTY(QStringList mimeTypes READ mimeTypes CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Mode {
        OpenMode = 0,
        FolderMode,
    };
    Q_ENUM(Mode)

    explicit PickerController(QObject* parent = nullptr)
        : QObject(parent) {
    }

    bool configure(
        const QString& modeText,
        bool multipleSelection,
        const QString& requestedInitialDirectory,
        const QStringList& requestedMimeTypes,
        QString* errorOut = nullptr) {
        const QString normalizedMode = modeText.trimmed().toLower();
        if (normalizedMode == QStringLiteral("open")) {
            m_mode = OpenMode;
        } else if (normalizedMode == QStringLiteral("folder")) {
            m_mode = FolderMode;
        } else {
            return failConfiguration(
                QStringLiteral("Picker mode must be 'open' or 'folder'"),
                errorOut);
        }

        if (m_mode == FolderMode && multipleSelection) {
            return failConfiguration(
                QStringLiteral("Folder picker does not support --multiple"),
                errorOut);
        }

        QString initial = requestedInitialDirectory.trimmed();
        if (initial.isEmpty())
            initial = QDir::homePath();
        if (LocalPathGuard::isUriLike(initial)) {
            return failConfiguration(
                QStringLiteral("Picker initial directory must be a local path"),
                errorOut);
        }

        const QFileInfo initialInfo(initial);
        if (!initialInfo.exists() || !initialInfo.isDir()) {
            return failConfiguration(
                QStringLiteral("Picker initial directory does not exist: %1").arg(initial),
                errorOut);
        }

        QStringList mimeTypes;
        QSet<QString> seen;
        for (const QString& requested : requestedMimeTypes) {
            const QStringList parts = requested.split(
                QLatin1Char(','),
                Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                const QString filter = part.trimmed().toLower();
                if (filter.isEmpty() || filter == QStringLiteral("*/*"))
                    continue;

                const qsizetype slash = filter.indexOf(QLatin1Char('/'));
                if (slash <= 0 || slash == filter.size() - 1
                    || filter.indexOf(QLatin1Char('/'), slash + 1) >= 0) {
                    return failConfiguration(
                        QStringLiteral("Invalid MIME filter: %1").arg(part.trimmed()),
                        errorOut);
                }

                if (!seen.contains(filter)) {
                    seen.insert(filter);
                    mimeTypes.push_back(filter);
                }
            }
        }

        if (m_mode == FolderMode && !mimeTypes.isEmpty()) {
            return failConfiguration(
                QStringLiteral("Folder picker does not use MIME filters"),
                errorOut);
        }

        m_multiple = m_mode == OpenMode && multipleSelection;
        m_initialDirectory = QDir::cleanPath(initialInfo.absoluteFilePath());
        m_mimeTypes = mimeTypes;
        setError(QString());
        return true;
    }

    QString modeName() const {
        return m_mode == FolderMode
            ? QStringLiteral("folder")
            : QStringLiteral("open");
    }
    bool folderMode() const { return m_mode == FolderMode; }
    bool multiple() const { return m_multiple; }
    QString initialDirectory() const { return m_initialDirectory; }
    QStringList mimeTypes() const { return m_mimeTypes; }
    QString error() const { return m_error; }

    Q_INVOKABLE QString validationError(
        const QStringList& selectedPaths,
        const QString& currentDirectory) const {
        if (m_mode == FolderMode) {
            const QString directory = normalizeExistingDirectory(currentDirectory);
            return directory.isEmpty()
                ? QStringLiteral("Current folder is not selectable")
                : QString();
        }

        if (selectedPaths.isEmpty())
            return QStringLiteral("Select a file");
        if (!m_multiple && selectedPaths.size() != 1)
            return QStringLiteral("Select exactly one file");

        QSet<QString> seen;
        for (const QString& requested : selectedPaths) {
            if (requested.trimmed().isEmpty() || LocalPathGuard::isUriLike(requested))
                return QStringLiteral("Only local files can be selected in this picker phase");

            const QFileInfo info(requested);
            if (!info.exists() || !info.isFile())
                return QStringLiteral("Folders cannot be returned by the open-file picker");

            const QString path = QDir::cleanPath(info.absoluteFilePath());
            if (seen.contains(path))
                continue;
            seen.insert(path);

            if (!matchesMimeFilters(path)) {
                return QStringLiteral("Selection does not match the requested MIME filter");
            }
        }

        if (seen.isEmpty())
            return QStringLiteral("Select a file");
        if (!m_multiple && seen.size() != 1)
            return QStringLiteral("Select exactly one file");
        return {};
    }

    Q_INVOKABLE bool canAccept(
        const QStringList& selectedPaths,
        const QString& currentDirectory) const {
        return validationError(selectedPaths, currentDirectory).isEmpty();
    }

    Q_INVOKABLE bool accept(
        const QStringList& selectedPaths,
        const QString& currentDirectory) {
        const QString validation = validationError(selectedPaths, currentDirectory);
        if (!validation.isEmpty()) {
            setError(validation);
            return false;
        }

        QStringList accepted;
        if (m_mode == FolderMode) {
            accepted.push_back(normalizeExistingDirectory(currentDirectory));
        } else {
            QSet<QString> seen;
            for (const QString& requested : selectedPaths) {
                const QFileInfo info(requested);
                const QString path = QDir::cleanPath(info.absoluteFilePath());
                if (seen.contains(path))
                    continue;
                seen.insert(path);
                accepted.push_back(path);
            }
        }

        setError(QString());
        emit acceptedPaths(accepted);
        return true;
    }

    Q_INVOKABLE void cancel() {
        setError(QString());
        emit cancelled();
    }

signals:
    void errorChanged();
    void acceptedPaths(const QStringList& paths);
    void cancelled();

private:
    static QString normalizeExistingDirectory(const QString& requested) {
        if (requested.trimmed().isEmpty() || LocalPathGuard::isUriLike(requested))
            return {};

        const QFileInfo info(requested);
        if (!info.exists() || !info.isDir())
            return {};
        return QDir::cleanPath(info.absoluteFilePath());
    }

    bool matchesMimeFilters(const QString& path) const {
        if (m_mimeTypes.isEmpty())
            return true;

        QMimeDatabase database;
        const QMimeType mime = database.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
        if (!mime.isValid())
            return false;

        const QString name = mime.name().toLower();
        for (const QString& filter : m_mimeTypes) {
            if (filter.endsWith(QStringLiteral("/*"))) {
                const QString prefix = filter.left(filter.size() - 1);
                if (name.startsWith(prefix))
                    return true;
                continue;
            }

            if (name == filter || mime.inherits(filter))
                return true;
        }
        return false;
    }

    bool failConfiguration(const QString& message, QString* errorOut) {
        setError(message);
        if (errorOut)
            *errorOut = message;
        return false;
    }

    void setError(const QString& message) {
        if (m_error == message)
            return;
        m_error = message;
        emit errorChanged();
    }

    Mode m_mode = OpenMode;
    bool m_multiple = false;
    QString m_initialDirectory = QDir::homePath();
    QStringList m_mimeTypes;
    QString m_error;
};
