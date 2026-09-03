// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "DirectorySession.hpp"

#include <QAbstractListModel>
#include <QVector>

class TabManager : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(DirectorySession* currentSession READ currentSession NOTIFY currentSessionChanged)
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

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    DirectorySession* currentSession() const;

    Q_INVOKABLE void newTab(const QString& path = QString());
    Q_INVOKABLE void duplicateCurrentTab();
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void closeCurrentTab();
    Q_INVOKABLE void reopenClosedTab();
    Q_INVOKABLE void nextTab();
    Q_INVOKABLE void previousTab();

signals:
    void currentIndexChanged();
    void currentSessionChanged();
    void countChanged();

private:
    struct ClosedTab {
        QString path;
    };

    void attachSession(DirectorySession* session);
    void emitTabChanged(DirectorySession* session, const QList<int>& roles);

    QVector<DirectorySession*> m_tabs;
    QVector<ClosedTab> m_closedTabs;
    int m_currentIndex = -1;
};
