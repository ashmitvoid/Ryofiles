// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QVector>

struct _GVolumeMonitor;
typedef struct _GVolumeMonitor GVolumeMonitor;

struct NetworkMountSnapshot {
    QString name;
    QString rootUri;
    QString defaultUri;
    bool canUnmount = false;
    bool shadowed = false;
};

struct NetworkLocationItem {
    QString name;
    QString uri;
    QString rootUri;
    QString scheme;
    QString host;
    bool canUnmount = false;
};

class NetworkLocationModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        UriRole,
        RootUriRole,
        SchemeRole,
        HostRole,
        CanUnmountRole,
    };
    Q_ENUM(Role)

    explicit NetworkLocationModel(QObject* parent = nullptr);
    ~NetworkLocationModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

    static QVector<NetworkLocationItem> locationsFromSnapshots(
        const QVector<NetworkMountSnapshot>& snapshots);

signals:
    void countChanged();
    void unmounted(const QString& rootUri);

private:
    void scheduleRefresh();
    void rebuildFromMonitor();

    QVector<NetworkLocationItem> m_items;
    GVolumeMonitor* m_monitor = nullptr;
    bool m_refreshScheduled = false;
};
