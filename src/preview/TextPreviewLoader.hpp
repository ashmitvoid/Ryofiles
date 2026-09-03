// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "TextPreviewStore.hpp"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <memory>

class TextPreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY resultChanged)
    Q_PROPERTY(QString text READ text NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit TextPreviewLoader(QObject* parent = nullptr);
    ~TextPreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool active() const { return m_active; }
    void setActive(bool active);

    bool loading() const { return m_loading; }
    bool supported() const { return m_result.supported; }
    bool truncated() const { return m_result.truncated; }
    QString text() const { return m_result.text; }
    QString error() const { return m_result.error; }

    Q_INVOKABLE bool isCandidate(const QString& path) const;

signals:
    void pathChanged();
    void activeChanged();
    void loadingChanged();
    void resultChanged();

private:
    void scheduleLoad();
    void startLoad();
    void clearResult();
    void setLoading(bool loading);

    QString m_path;
    bool m_active = false;
    bool m_loading = false;
    quint64 m_generation = 0;
    TextPreviewResult m_result;
    QTimer m_debounce;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
};
