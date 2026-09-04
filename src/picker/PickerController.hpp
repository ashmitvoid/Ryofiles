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

#include <utility>

struct PickerContract {
    struct SaveTarget {
        bool valid = false;
        bool overwriteRequired = false;
        QString path;
        QString error;
    };

    bool valid = false;
    bool folderMode = false;
    bool saveMode = false;
    bool multiple = false;
    QString initialDirectory = QDir::homePath();
    QString suggestedName;
    QStringList mimeTypes;
    QString error;

    static PickerContract parse(
        const QString& modeText,
        bool multipleSelection,
        const QString& requestedInitialDirectory,
        const QStringList& requestedMimeTypes,
        const QString& requestedSuggestedName = QString()) {
        PickerContract contract;

        const QString normalizedMode = modeText.trimmed().toLower();
        if (normalizedMode == QStringLiteral("open")) {
            contract.folderMode = false;
            contract.saveMode = false;
        } else if (normalizedMode == QStringLiteral("folder")) {
            contract.folderMode = true;
            contract.saveMode = false;
        } else if (normalizedMode == QStringLiteral("save")) {
            contract.folderMode = false;
            contract.saveMode = true;
        } else {
            contract.error = QStringLiteral("Picker mode must be 'open', 'save', or 'folder'");
            return contract;
        }

        if ((contract.folderMode || contract.saveMode) && multipleSelection) {
            contract.error = contract.folderMode
                ? QStringLiteral("Folder picker does not support --multiple")
                : QStringLiteral("Save picker does not support --multiple");
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

        if (!requestedSuggestedName.isEmpty() && !contract.saveMode) {
            contract.error = QStringLiteral("Suggested file name requires --picker save");
            return contract;
        }
        if (contract.saveMode
            && !requestedSuggestedName.isEmpty()
            && !validLeafName(requestedSuggestedName)) {
            contract.error = QStringLiteral("Suggested file name is invalid");
            return contract;
        }

        contract.multiple = !contract.folderMode && !contract.saveMode && multipleSelection;
        contract.initialDirectory = QDir::cleanPath(initialInfo.absoluteFilePath());
        contract.suggestedName = requestedSuggestedName;
        contract.valid = true;
        return contract;
    }

    QString modeName() const {
        if (folderMode)
            return QStringLiteral("folder");
        if (saveMode)
            return QStringLiteral("save");
        return QStringLiteral("open");
    }

    QString validationError(
        const QStringList& selectedPaths,
        const QString& currentDirectory) const {
        if (!valid)
            return error.isEmpty() ? QStringLiteral("Picker is not configured") : error;

        if (saveMode)
            return QStringLiteral("Enter a file name");

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

    SaveTarget saveTarget(
        const QString& currentDirectory,
        const QString& fileName) const {
        SaveTarget target;
        if (!valid) {
            target.error = error.isEmpty()
                ? QStringLiteral("Picker is not configured")
                : error;
            return target;
        }
        if (!saveMode) {
            target.error = QStringLiteral("Picker is not in save mode");
            return target;
        }

        const QString directory = normalizeExistingDirectory(currentDirectory);
        if (directory.isEmpty()) {
            target.error = QStringLiteral("Current folder is not writable save context");
            return target;
        }
        if (!validLeafName(fileName)) {
            target.error = QStringLiteral("Enter a valid file name");
            return target;
        }

        target.path = QDir(directory).filePath(fileName);
        target.path = QDir::cleanPath(QFileInfo(target.path).absoluteFilePath());

        if (!matchesMimeFilters(target.path)) {
            target.error = QStringLiteral("File name does not match the requested MIME filter");
            return target;
        }

        const QFileInfo targetInfo(target.path);
        if (targetInfo.isDir()) {
            target.error = QStringLiteral("A folder already exists with this name");
            return target;
        }

        target.overwriteRequired = targetInfo.exists() || targetInfo.isSymLink();
        target.valid = true;
        return target;
    }

private:
    static bool validLeafName(const QString& name) {
        return !name.isEmpty()
            && !name.trimmed().isEmpty()
            && !name.contains(QLatin1Char('/'))
            && !name.contains(QChar::Null)
            && name != QStringLiteral(".")
            && name != QStringLiteral("..");
    }

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
    Q_PROPERTY(bool saveMode READ saveMode CONSTANT)
    Q_PROPERTY(bool multiple READ multiple CONSTANT)
    Q_PROPERTY(QString initialDirectory READ initialDirectory CONSTANT)
    Q_PROPERTY(QString suggestedName READ suggestedName CONSTANT)
    Q_PROPERTY(QStringList mimeTypes READ mimeTypes CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool overwriteConfirmationRequired READ overwriteConfirmationRequired NOTIFY overwriteConfirmationChanged)
    Q_PROPERTY(QString pendingOverwritePath READ pendingOverwritePath NOTIFY overwriteConfirmationChanged)

public:
    explicit PickerController(QObject* parent = nullptr)
        : QObject(parent) {
    }

    bool configure(
        const QString& modeText,
        bool multipleSelection,
        const QString& requestedInitialDirectory,
        const QStringList& requestedMimeTypes,
        const QString& requestedSuggestedName = QString(),
        QString* errorOut = nullptr) {
        PickerContract contract = PickerContract::parse(
            modeText,
            multipleSelection,
            requestedInitialDirectory,
            requestedMimeTypes,
            requestedSuggestedName);
        if (!contract.valid) {
            setError(contract.error);
            if (errorOut)
                *errorOut = contract.error;
            return false;
        }

        m_contract = std::move(contract);
        clearOverwriteConfirmation();
        setError(QString());
        return true;
    }

    QString modeName() const { return m_contract.modeName(); }
    bool folderMode() const { return m_contract.folderMode; }
    bool saveMode() const { return m_contract.saveMode; }
    bool multiple() const { return m_contract.multiple; }
    QString initialDirectory() const { return m_contract.initialDirectory; }
    QString suggestedName() const { return m_contract.suggestedName; }
    QStringList mimeTypes() const { return m_contract.mimeTypes; }
    QString error() const { return m_error; }
    bool overwriteConfirmationRequired() const { return !m_pendingOverwritePath.isEmpty(); }
    QString pendingOverwritePath() const { return m_pendingOverwritePath; }

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
        if (m_contract.saveMode) {
            setError(QStringLiteral("Save picker requires a file name"));
            return false;
        }

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

        clearOverwriteConfirmation();
        setError(QString());
        emit acceptedPaths(accepted);
        return true;
    }

    Q_INVOKABLE QString saveValidationError(
        const QString& currentDirectory,
        const QString& fileName) const {
        return m_contract.saveTarget(currentDirectory, fileName).error;
    }

    Q_INVOKABLE bool canSave(
        const QString& currentDirectory,
        const QString& fileName) const {
        return m_contract.saveTarget(currentDirectory, fileName).valid;
    }

    Q_INVOKABLE bool saveNeedsOverwrite(
        const QString& currentDirectory,
        const QString& fileName) const {
        const PickerContract::SaveTarget target =
            m_contract.saveTarget(currentDirectory, fileName);
        return target.valid && target.overwriteRequired;
    }

    Q_INVOKABLE bool requestSave(
        const QString& currentDirectory,
        const QString& fileName) {
        const PickerContract::SaveTarget target =
            m_contract.saveTarget(currentDirectory, fileName);
        if (!target.valid) {
            clearOverwriteConfirmation();
            setError(target.error);
            return false;
        }

        if (target.overwriteRequired) {
            setPendingOverwritePath(target.path);
            setError(QString());
            return false;
        }

        clearOverwriteConfirmation();
        setError(QString());
        emit acceptedPaths({target.path});
        return true;
    }

    Q_INVOKABLE bool confirmOverwrite(
        const QString& currentDirectory,
        const QString& fileName) {
        const PickerContract::SaveTarget target =
            m_contract.saveTarget(currentDirectory, fileName);
        if (!target.valid) {
            clearOverwriteConfirmation();
            setError(target.error);
            return false;
        }

        if (m_pendingOverwritePath.isEmpty()
            || target.path != m_pendingOverwritePath) {
            clearOverwriteConfirmation();
            setError(QStringLiteral("Save target changed; confirm the new target again"));
            return false;
        }

        clearOverwriteConfirmation();
        setError(QString());
        emit acceptedPaths({target.path});
        return true;
    }

    Q_INVOKABLE void clearOverwriteConfirmation() {
        setPendingOverwritePath(QString());
    }

    Q_INVOKABLE void cancel() {
        clearOverwriteConfirmation();
        setError(QString());
        emit cancelled();
    }

signals:
    void errorChanged();
    void overwriteConfirmationChanged();
    void acceptedPaths(const QStringList& paths);
    void cancelled();

private:
    void setError(const QString& message) {
        if (m_error == message)
            return;
        m_error = message;
        emit errorChanged();
    }

    void setPendingOverwritePath(const QString& path) {
        if (m_pendingOverwritePath == path)
            return;
        m_pendingOverwritePath = path;
        emit overwriteConfirmationChanged();
    }

    PickerContract m_contract;
    QString m_error;
    QString m_pendingOverwritePath;
};
