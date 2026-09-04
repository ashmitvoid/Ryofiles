// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QVector>

struct RemoteDirectoryEntry {
    QString name;
    QString uri;
    QString sizeText;
    QString modifiedText;
    bool directory = false;
    bool hidden = false;
};

class RemoteDirectoryModel final : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString uri READ uri WRITE setUri NOTIFY uriChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString filterQuery READ filterQuery WRITE setFilterQuery NOTIFY filterQueryChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        UriRole,
        DirectoryRole,
        SizeTextRole,
        ModifiedTextRole,
        HiddenRole,
    };
    Q_ENUM(Role)

    explicit RemoteDirectoryModel(QObject* parent = nullptr);
    ~RemoteDirectoryModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString uri() const { return m_uri; }
    void setUri(const QString& uri);

    bool loading() const { return m_loading; }
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);

    QString filterQuery() const { return m_filterQuery; }
    void setFilterQuery(const QString& query);

    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString uriAt(int index) const;
    Q_INVOKABLE bool isDirectoryAt(int index) const;
    Q_INVOKABLE int indexOfUri(const QString& uri) const;

    static QVector<RemoteDirectoryEntry> visibleEntries(
        QVector<RemoteDirectoryEntry> entries,
        bool showHidden,
        const QString& query);
    static QString formatSize(qint64 bytes);

signals:
    void uriChanged();
    void loadingChanged();
    void showHiddenChanged();
    void filterQueryChanged();
    void countChanged();
    void errorChanged();

private:
    struct ScanContext;

    void startScan();
    void requestNextBatch(ScanContext* context);
    void finishEnumeration(
        ScanContext* context,
        const QString& error,
        bool cancelled);
    void finishAfterClose(ScanContext* context);
    void publishInitialEntries(const QVector<RemoteDirectoryEntry>& entries, quint64 generation);
    void sortAndApplyFinal(
        QVector<RemoteDirectoryEntry> entries,
        quint64 generation,
        const QString& error);
    void rebuildVisibleEntries();
    void replaceVisibleEntries(QVector<RemoteDirectoryEntry> entries);
    void setLoading(bool loading);
    void setError(const QString& error);
    void supersedeCurrentScan();

    QVector<RemoteDirectoryEntry> m_allEntries;
    QVector<RemoteDirectoryEntry> m_entries;
    QString m_uri;
    QString m_filterQuery;
    QString m_error;
    bool m_loading = false;
    bool m_showHidden = false;
    quint64 m_generation = 0;
    ScanContext* m_scan = nullptr;
};
