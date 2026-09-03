// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QVector>

class DirectoryModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int unfilteredCount READ unfilteredCount NOTIFY countChanged)

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

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool loading() const { return m_loading; }
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);
    int unfilteredCount() const { return m_allEntries.size(); }

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
    Q_INVOKABLE void clearFilter();

    Q_INVOKABLE QString pathAt(int index) const;
    Q_INVOKABLE bool isDirectoryAt(int index) const;
    Q_INVOKABLE int indexOfPath(const QString& path) const;

signals:
    void pathChanged();
    void loadingChanged();
    void showHiddenChanged();
    void filterTextChanged();
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
    void setLoading(bool loading);
    void rebuildFilterIndexes();
    const Entry* entryForRow(int row) const;

    static QList<Entry> scanDirectory(const QString& path, bool showHidden, QString* error);
    static QString formatSize(qint64 bytes);
    static QString standardPath(int location);
    static bool thumbnailCandidateFor(const QFileInfo& info);

    QList<Entry> m_allEntries;
    QVector<int> m_visibleIndexes;
    QString m_path;
    QString m_filterText;
    bool m_loading = false;
    bool m_showHidden = false;
    quint64 m_generation = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshDebounce;
};
