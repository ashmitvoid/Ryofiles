// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QIdentityProxyModel>
#include <QMetaObject>
#include <QVector>

class DirectoryModel;
class RemoteDirectoryModel;

class SessionFileModel final : public QIdentityProxyModel {
    Q_OBJECT

    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString filterQuery READ filterQuery WRITE setFilterQuery NOTIFY filterQueryChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool remote READ remote NOTIFY sourceKindChanged)

    // Preserve the navigation-facing properties the QML rail consumed from DirectoryModel
    // before SessionFileModel became the stable local/remote proxy.
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

    explicit SessionFileModel(QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void useLocal(DirectoryModel* model);
    void useRemote(RemoteDirectoryModel* model);

    QString path() const;
    bool loading() const;
    bool showHidden() const;
    void setShowHidden(bool show);

    QString filterQuery() const;
    void setFilterQuery(const QString& query);

    bool remote() const { return m_remoteActive; }

    QString home() const;
    QString desktop() const;
    QString documents() const;
    QString downloads() const;
    QString pictures() const;
    QString music() const;
    QString videos() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString pathAt(int index) const;
    Q_INVOKABLE bool isDirectoryAt(int index) const;
    Q_INVOKABLE int indexOfPath(const QString& path) const;

signals:
    void pathChanged();
    void loadingChanged();
    void showHiddenChanged();
    void filterQueryChanged();
    void countChanged();
    void sourceKindChanged();

private:
    static QString standardPath(int location);
    void setBackend(QAbstractItemModel* model, bool remote);
    void clearBackendConnections();
    void connectLocal(DirectoryModel* model);
    void connectRemote(RemoteDirectoryModel* model);

    DirectoryModel* m_local = nullptr;
    RemoteDirectoryModel* m_remote = nullptr;
    bool m_remoteActive = false;
    QVector<QMetaObject::Connection> m_backendConnections;
};
