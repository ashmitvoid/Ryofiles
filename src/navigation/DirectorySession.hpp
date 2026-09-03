// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "../fs/DirectoryModel.hpp"

#include <QObject>
#include <QStringList>
#include <QVector>

class DirectorySession : public QObject {
    Q_OBJECT

    Q_PROPERTY(DirectoryModel* model READ model CONSTANT)
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(QString selectedPath READ selectedPath WRITE setSelectedPath NOTIFY selectedPathChanged)
    Q_PROPERTY(qreal scrollPosition READ scrollPosition WRITE setScrollPosition NOTIFY scrollPositionChanged)

public:
    explicit DirectorySession(const QString& initialPath = QString(), QObject* parent = nullptr);

    DirectoryModel* model() { return &m_model; }
    const DirectoryModel* model() const { return &m_model; }

    QString path() const;
    QString title() const;
    bool canGoBack() const;
    bool canGoForward() const;

    QString selectedPath() const;
    void setSelectedPath(const QString& path);

    qreal scrollPosition() const;
    void setScrollPosition(qreal position);

    Q_INVOKABLE bool navigate(const QString& path);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void activate(int index);

signals:
    void pathChanged();
    void titleChanged();
    void historyChanged();
    void selectedPathChanged();
    void scrollPositionChanged();
    void errorOccurred(const QString& message);

private:
    struct HistoryEntry {
        QString path;
        QString selectedPath;
        qreal scrollPosition = 0.0;
    };

    static QString normalizeDirectoryPath(const QString& path);
    void applyHistoryEntry();
    HistoryEntry* currentEntry();
    const HistoryEntry* currentEntry() const;

    DirectoryModel m_model;
    QVector<HistoryEntry> m_history;
    int m_historyIndex = -1;
};
