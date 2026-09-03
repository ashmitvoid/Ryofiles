// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "../fs/DirectoryModel.hpp"

#include <QObject>
#include <QSet>
#include <QVector>

class DirectorySession : public QObject {
    Q_OBJECT

    Q_PROPERTY(DirectoryModel* model READ model CONSTANT)
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)

    Q_PROPERTY(QString selectedPath READ selectedPath WRITE setSelectedPath NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedPaths READ selectedPaths NOTIFY selectionChanged)
    Q_PROPERTY(quint64 selectionRevision READ selectionRevision NOTIFY selectionChanged)

    Q_PROPERTY(qreal scrollPosition READ scrollPosition WRITE setScrollPosition NOTIFY scrollPositionChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)

public:
    enum ViewMode {
        CompactView = 0,
        GridView = 1,
        DetailsView = 2,
    };
    Q_ENUM(ViewMode)

    explicit DirectorySession(const QString& initialPath = QString(), QObject* parent = nullptr);

    DirectoryModel* model() { return &m_model; }
    const DirectoryModel* model() const { return &m_model; }

    QString path() const;
    QString title() const;
    bool canGoBack() const;
    bool canGoForward() const;

    QString selectedPath() const;
    void setSelectedPath(const QString& path);
    int selectionCount() const;
    QStringList selectedPaths() const;
    quint64 selectionRevision() const { return m_selectionRevision; }

    qreal scrollPosition() const;
    void setScrollPosition(qreal position);

    int viewMode() const { return m_viewMode; }
    void setViewMode(int mode);

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

signals:
    void pathChanged();
    void titleChanged();
    void historyChanged();
    void selectionChanged();
    void scrollPositionChanged();
    void viewModeChanged();
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
    void applyHistoryEntry();
    void emitSelectionChanged();
    HistoryEntry* currentEntry();
    const HistoryEntry* currentEntry() const;

    DirectoryModel m_model;
    QVector<HistoryEntry> m_history;
    int m_historyIndex = -1;
    int m_viewMode = DetailsView;
    quint64 m_selectionRevision = 0;
};
