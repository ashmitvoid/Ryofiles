// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QStringList>

class ClipboardController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool hasFiles READ hasFiles NOTIFY changed)
    Q_PROPERTY(bool cut READ isCut NOTIFY changed)

public:
    explicit ClipboardController(QObject* parent = nullptr);

    bool hasFiles() const;
    bool isCut() const;

    Q_INVOKABLE QStringList filePaths() const;
    Q_INVOKABLE void copyFiles(const QStringList& paths);
    Q_INVOKABLE void cutFiles(const QStringList& paths);
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE bool matchesFiles(const QStringList& paths, bool cutExpected) const;
    Q_INVOKABLE void clearIfMatches(const QStringList& paths, bool cutExpected);
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    static QStringList normalizedPaths(const QStringList& paths);
    static QStringList pathsFromGnomePayload(const QByteArray& payload);
    static bool gnomePayloadIsCut(const QByteArray& payload);
    void setFiles(const QStringList& paths, bool cut);
};
