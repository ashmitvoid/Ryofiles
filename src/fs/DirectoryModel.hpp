// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

class DirectoryModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString filterQuery READ filterQuery WRITE setFilterQuery NOTIFY filterQueryChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

    Q_PROPERTY(QString home READ home CONSTANT)
    Q_PROPERTY(QString desktop READ desktop CONSTANT)
    Q_PROPERTY(QString documents READ documents CONSTANT)
    Q_PROPERTY(QString downloads READ downloads CONSTANT)
    Q_PROPERTY(QString pictures READ pictures CONSTANT)
    Q_PROPERTY(QString music READ music CONSTANT)
    Q_PROPERTY(QString videos READ videos CONSTANT)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        DirectoryRole,
        SizeTextRole,
        ModifiedTextRole,
        HiddenRole,
        ThumbnailCandidateRole,
    };
    Q_ENUM(Role)

    explicit DirectoryModel(QObject* parent = nullptr);
    DirectoryModel(bool startActive, QObject* parent);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool loading() const { return m_loading; }
    bool active() const { return m_active; }
    void setActive(bool active);

    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);

    QString filterQuery() const { return m_filterQuery; }
    void setFilterQuery(const QString& query);

    QString home() const;
    QString desktop() const;
    QString documents() const;
    QString downloads() const;
    QString pictures() const;
    QString music() const;
    QString videos() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void activate(int index);

    Q_INVOKABLE QString pathAt(int index) const;
    Q_INVOKABLE bool isDirectoryAt(int index) const;
    Q_INVOKABLE int indexOfPath(const QString& path) const;

signals:
    void pathChanged();
    void loadingChanged();
    void activeChanged();
    void showHiddenChanged();
    void filterQueryChanged();
    void countChanged();
    void errorOccurred(const QString& message);

private:
    struct Entry {
        QString name;
        QString path;
        QString sizeText;
        QString modifiedText;
        bool directory = false;
        bool hidden = false;
        bool thumbnailCandidate = false;
    };

    void scan();
    void watchCurrentDirectory();
    void clearWatchers();
    void setLoading(bool loading);
    void rebuildFilteredEntries();
    static QList<Entry> scanDirectory(const QString& path, bool showHidden, QString* error);
    static QList<Entry> filterEntries(const QList<Entry>& entries, const QString& query);
    static QString formatSize(qint64 bytes);
    static QString standardPath(int location);
    static bool thumbnailCandidateFor(const QFileInfo& info);

    QList<Entry> m_allEntries;
    QList<Entry> m_entries;
    QString m_path;
    QString m_filterQuery;
    bool m_loading = false;
    bool m_active = true;
    bool m_showHidden = false;
    quint64 m_generation = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshDebounce;
};
