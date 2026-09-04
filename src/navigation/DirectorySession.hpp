// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "../fs/DirectoryModel.hpp"
#include "../locations/RemoteDirectoryModel.hpp"
#include "../locations/SessionFileModel.hpp"
#include "../search/DeepSearchModel.hpp"

#include <QObject>
#include <QSet>
#include <QVector>

class DirectorySession : public QObject {
    Q_OBJECT

    Q_PROPERTY(SessionFileModel* model READ model CONSTANT)
    Q_PROPERTY(DeepSearchModel* deepSearch READ deepSearch CONSTANT)
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool remote READ remote NOTIFY locationKindChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)

    Q_PROPERTY(QString selectedPath READ selectedPath WRITE setSelectedPath NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedPaths READ selectedPaths NOTIFY selectionChanged)
    Q_PROPERTY(quint64 selectionRevision READ selectionRevision NOTIFY selectionChanged)

    Q_PROPERTY(qreal scrollPosition READ scrollPosition WRITE setScrollPosition NOTIFY scrollPositionChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(bool previewVisible READ previewVisible WRITE setPreviewVisible NOTIFY previewVisibleChanged)

public:
    enum ViewMode {
        CompactView = 0,
        GridView = 1,
        DetailsView = 2,
    };
    Q_ENUM(ViewMode)

    explicit DirectorySession(const QString& initialPath = QString(), QObject* parent = nullptr);

    SessionFileModel* model() { return &m_model; }
    const SessionFileModel* model() const { return &m_model; }
    DeepSearchModel* deepSearch() { return &m_deepSearch; }
    const DeepSearchModel* deepSearch() const { return &m_deepSearch; }

    QString path() const;
    QString title() const;
    bool remote() const { return m_model.remote(); }
    bool canGoBack() const;
    bool canGoForward() const;

    // Exposed for deterministic backend-lifecycle tests, not as QML API.
    bool localBackendActive() const { return m_localModel.active(); }

    QString selectedPath() const;
    void setSelectedPath(const QString& path);
    int selectionCount() const;
    QStringList selectedPaths() const;
    quint64 selectionRevision() const { return m_selectionRevision; }

    qreal scrollPosition() const;
    void setScrollPosition(qreal position);

    int viewMode() const { return m_viewMode; }
    void setViewMode(int mode);

    bool previewVisible() const { return m_previewVisible; }
    void setPreviewVisible(bool visible);

    Q_INVOKABLE bool navigate(const QString& path);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void activate(int index);

    Q_INVOKABLE bool isSelectedPath(const QString& path) const;
    Q_INVOKABLE void selectSingle(int index);
    Q_INVOKABLE void toggleSelection(int index);
    Q_INVOKABLE void selectRange(int index);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();

    bool recoverFromUnmount(
        const QString& mountRoot,
        const QString& preferredFallback = QString());
    static bool pathInsideRoot(const QString& path, const QString& rootPath);
    static QString recoveryPathForUnmount(
        const QString& mountRoot,
        const QString& preferredFallback = QString());

signals:
    void pathChanged();
    void titleChanged();
    void locationKindChanged();
    void historyChanged();
    void selectionChanged();
    void scrollPositionChanged();
    void viewModeChanged();
    void previewVisibleChanged();
    void errorOccurred(const QString& message);

private:
    struct HistoryEntry {
        QString path;
        QSet<QString> selectedPaths;
        QString primarySelectedPath;
        QString anchorPath;
        qreal scrollPosition = 0.0;
    };

    static QString normalizeDirectoryPath(const QString& path);
    static QString normalizeSessionLocation(const QString& location, QString* error = nullptr);
    static QString parentRemoteLocation(const QString& location);
    void applyHistoryEntry();
    void applyBackendForLocation(const QString& location);
    void emitSelectionChanged();
    HistoryEntry* currentEntry();
    const HistoryEntry* currentEntry() const;

    DirectoryModel m_localModel;
    RemoteDirectoryModel m_remoteModel;
    SessionFileModel m_model;
    DeepSearchModel m_deepSearch;
    QVector<HistoryEntry> m_history;
    int m_historyIndex = -1;
    int m_viewMode = DetailsView;
    bool m_previewVisible = false;
    quint64 m_selectionRevision = 0;
};
