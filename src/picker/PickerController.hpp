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

struct PickerContract {
    bool valid = false;
    bool folderMode = false;
    bool multiple = false;
    QString initialDirectory = QDir::homePath();
    QStringList mimeTypes;
    QString error;

    static PickerContract parse(
        const QString& modeText,
        bool multipleSelection,
        const QString& requestedInitialDirectory,
        const QStringList& requestedMimeTypes) {
        PickerContract contract;

        const QString normalizedMode = modeText.trimmed().toLower();
        if (normalizedMode == QStringLiteral("open")) {
            contract.folderMode = false;
        } else if (normalizedMode == QStringLiteral("folder")) {
            contract.folderMode = true;
        } else {
            contract.error = QStringLiteral("Picker mode must be 'open' or 'folder'");
            return contract;
        }

        if (contract.folderMode && multipleSelection) {
            contract.error = QStringLiteral("Folder picker does not support --multiple");
            return contract;
        }

        QString initial = requestedInitialDirectory.trimmed();
        if (initial.isEmpty())
            initial = QDir::homePath();
        if (LocalPathGuard::isUriLike(initial)) {
            contract.error = QStringLiteral("Picker initial directory must be a local path");
            return contract;
        }

        const QFileInfo initialInfo(initial);
        if (!initialInfo.exists() || !initialInfo.isDir()) {
            contract.error =
                QStringLiteral("Picker initial directory does not exist: %1").arg(initial);
            return contract;
        }

        QSet<QString> seenMimeTypes;
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
                    contract.error =
                        QStringLiteral("Invalid MIME filter: %1").arg(part.trimmed());
                    return contract;
                }

                if (!seenMimeTypes.contains(filter)) {
                    seenMimeTypes.insert(filter);
                    contract.mimeTypes.push_back(filter);
                }
            }
        }

        if (contract.folderMode && !contract.mimeTypes.isEmpty()) {
            contract.error = QStringLiteral("Folder picker does not use MIME filters");
            return contract;
        }

        contract.multiple = !contract.folderMode && multipleSelection;
        contract.initialDirectory = QDir::cleanPath(initialInfo.absoluteFilePath());
        contract.valid = true;
        return contract;
    }

    QString modeName() const {
        return folderMode ? QStringLiteral("folder") : QStringLiteral("open");
    }

    QString validationError(
        const QStringList& selectedPaths,
        const QString& currentDirectory) const {
        if (!valid)
            return error.isEmpty() ? QStringLiteral("Picker is not configured") : error;

        if (folderMode) {
            return normalizeExistingDirectory(currentDirectory).isEmpty()
                ? QStringLiteral("Current folder is not selectable")
                : QString();
        }

        if (selectedPaths.isEmpty())
            return QStringLiteral("Select a file");
        if (!multiple && selectedPaths.size() != 1)
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

            if (!matchesMimeFilters(path))
                return QStringLiteral("Selection does not match the requested MIME filter");
        }

        if (seen.isEmpty())
            return QStringLiteral("Select a file");
        if (!multiple && seen.size() != 1)
            return QStringLiteral("Select exactly one file");
        return {};
    }

    bool canAccept(
        const QStringList& selectedPaths,
        const QString& currentDirectory) const {
        return validationError(selectedPaths, currentDirectory).isEmpty();
    }

    QStringList acceptedPaths(
        const QStringList& selectedPaths,
        const QString& currentDirectory) const {
        if (!canAccept(selectedPaths, currentDirectory))
            return {};

        if (folderMode)
            return {normalizeExistingDirectory(currentDirectory)};

        QStringList accepted;
        QSet<QString> seen;
        for (const QString& requested : selectedPaths) {
            const QFileInfo info(requested);
            const QString path = QDir::cleanPath(info.absoluteFilePath());
            if (seen.contains(path))
                continue;
            seen.insert(path);
            accepted.push_back(path);
        }
        return accepted;
    }

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
        if (mimeTypes.isEmpty())
            return true;

        QMimeDatabase database;
        const QMimeType mime = database.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
        if (!mime.isValid())
            return false;

        const QString name = mime.name().toLower();
        for (const QString& filter : mimeTypes) {
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
};

class PickerController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ modeName CONSTANT)
    Q_PROPERTY(bool folderMode READ folderMode CONSTANT)
    Q_PROPERTY(bool multiple READ multiple CONSTANT)
    Q_PROPERTY(QString initialDirectory READ initialDirectory CONSTANT)
    Q_PROPERTY(QStringList mimeTypes READ mimeTypes CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit PickerController(QObject* parent = nullptr)
        : QObject(parent) {
    }

    bool configure(
        const QString& modeText,
        bool multipleSelection,
        const QString& requestedInitialDirectory,
        const QStringList& requestedMimeTypes,
        QString* errorOut = nullptr) {
        PickerContract contract = PickerContract::parse(
            modeText,
            multipleSelection,
            requestedInitialDirectory,
            requestedMimeTypes);
        if (!contract.valid) {
            setError(contract.error);
            if (errorOut)
                *errorOut = contract.error;
            return false;
        }

        m_contract = std::move(contract);
        setError(QString());
        return true;
    }

    QString modeName() const { return m_contract.modeName(); }
    bool folderMode() const { return m_contract.folderMode; }
    bool multiple() const { return m_contract.multiple; }
    QString initialDirectory() const { return m_contract.initialDirectory; }
    QStringList mimeTypes() const { return m_contract.mimeTypes; }
    QString error() const { return m_error; }

    Q_INVOKABLE QString validationError(
        const QStringList& selectedPaths,
        const QString& currentDirectory) const {
        return m_contract.validationError(selectedPaths, currentDirectory);
    }

    Q_INVOKABLE bool canAccept(
        const QStringList& selectedPaths,
        const QString& currentDirectory) const {
        return m_contract.canAccept(selectedPaths, currentDirectory);
    }

    Q_INVOKABLE bool accept(
        const QStringList& selectedPaths,
        const QString& currentDirectory) {
        const QString validation = m_contract.validationError(selectedPaths, currentDirectory);
        if (!validation.isEmpty()) {
            setError(validation);
            return false;
        }

        const QStringList accepted = m_contract.acceptedPaths(selectedPaths, currentDirectory);
        if (accepted.isEmpty()) {
            setError(QStringLiteral("Picker selection is empty"));
            return false;
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
    void setError(const QString& message) {
        if (m_error == message)
            return;
        m_error = message;
        emit errorChanged();
    }

    PickerContract m_contract;
    QString m_error;
};
