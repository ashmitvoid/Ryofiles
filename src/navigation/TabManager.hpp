// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "DirectorySession.hpp"

#include <QAbstractListModel>
#include <QVector>

class TabManager : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(DirectorySession* currentSession READ currentSession NOTIFY currentSessionChanged)
    Q_PROPERTY(DirectorySession* primarySession READ primarySession NOTIFY currentSessionChanged)
    Q_PROPERTY(DirectorySession* secondarySession READ secondarySession NOTIFY splitChanged)
    Q_PROPERTY(bool split READ split NOTIFY splitChanged)
    Q_PROPERTY(int activePane READ activePane WRITE setActivePane NOTIFY activePaneChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        PathRole,
        SessionRole,
        ActiveRole,
    };
    Q_ENUM(Role)

    explicit TabManager(QObject* parent = nullptr);
    ~TabManager() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);

    DirectorySession* currentSession() const;
    DirectorySession* primarySession() const;
    DirectorySession* secondarySession() const;
    bool split() const;
    int activePane() const;
    void setActivePane(int pane);

    Q_INVOKABLE void newTab(const QString& path = QString());
    Q_INVOKABLE void duplicateCurrentTab();
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void closeCurrentTab();
    Q_INVOKABLE void reopenClosedTab();
    Q_INVOKABLE void nextTab();
    Q_INVOKABLE void previousTab();

    Q_INVOKABLE void toggleSplitView();
    Q_INVOKABLE void openSplit(const QString& path = QString());
    Q_INVOKABLE void closeSplit();
    Q_INVOKABLE void swapPanes();

    Q_INVOKABLE int recoverUnmountedMount(
        const QString& mountRoot,
        const QString& preferredFallback = QString());
    Q_INVOKABLE int recoverUnmountedNetwork(
        const QString& rootUri,
        const QString& preferredFallback = QString());

signals:
    void currentIndexChanged();
    void currentSessionChanged();
    void splitChanged();
    void activePaneChanged();
    void countChanged();

private:
    struct SplitState {
        DirectorySession* secondary = nullptr;
        int activePane = 0;
    };

    struct ClosedTab {
        QString primaryPath;
        QString secondaryPath;
        bool split = false;
        int activePane = 0;
    };

    void attachSession(DirectorySession* session);
    void emitTabChanged(DirectorySession* session, const QList<int>& roles);
    int tabIndexForSession(DirectorySession* session) const;
    void emitCurrentSplitStateChanged();

    QVector<DirectorySession*> m_tabs;
    QVector<SplitState> m_splitStates;
    QVector<ClosedTab> m_closedTabs;
    int m_currentIndex = -1;
    quint64 m_mountRecoverySubscription = 0;
    quint64 m_networkMountRecoverySubscription = 0;
};
