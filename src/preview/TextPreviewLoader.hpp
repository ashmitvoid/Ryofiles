// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ArchivePreviewStore.hpp"
#include "TextPreviewStore.hpp"

#include <QObject>
#include <QTimer>
#include <QVariantList>

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

class ArchivePreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY resultChanged)
    Q_PROPERTY(QString formatName READ formatName NOTIFY resultChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY resultChanged)
    Q_PROPERTY(quint64 entriesSeen READ entriesSeen NOTIFY resultChanged)
    Q_PROPERTY(quint64 archiveBytesConsumed READ archiveBytesConsumed NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit ArchivePreviewLoader(QObject* parent = nullptr);
    ~ArchivePreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool active() const { return m_active; }
    void setActive(bool active);

    bool loading() const { return m_loading; }
    bool supported() const { return m_result.succeeded(); }
    bool truncated() const { return m_result.truncated; }
    QString formatName() const { return m_result.formatName; }
    QVariantList entries() const { return m_entries; }
    quint64 entriesSeen() const { return m_result.entriesSeen; }
    quint64 archiveBytesConsumed() const { return m_result.archiveBytesConsumed; }
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
    ArchivePreviewResult m_result;
    QVariantList m_entries;
    QTimer m_debounce;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
};

class PdfPreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY resultChanged)
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY resultChanged)
    Q_PROPERTY(QString title READ title NOTIFY resultChanged)
    Q_PROPERTY(QString author READ author NOTIFY resultChanged)
    Q_PROPERTY(QString subject READ subject NOTIFY resultChanged)
    Q_PROPERTY(QString keywords READ keywords NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit PdfPreviewLoader(QObject* parent = nullptr);
    ~PdfPreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool active() const { return m_active; }
    void setActive(bool active);

    bool loading() const { return m_loading; }
    bool supported() const { return m_supported; }
    int page() const { return m_page; }
    void setPage(int page);
    int pageCount() const { return m_pageCount; }
    QString imageSource() const { return m_imageSource; }
    QString title() const { return m_title; }
    QString author() const { return m_author; }
    QString subject() const { return m_subject; }
    QString keywords() const { return m_keywords; }
    QString error() const { return m_error; }

    Q_INVOKABLE bool isCandidate(const QString& path) const;

signals:
    void pathChanged();
    void activeChanged();
    void loadingChanged();
    void pageChanged();
    void resultChanged();

private:
    void scheduleLoad(bool clearExisting = true);
    void startLoad();
    void clearResult();
    void setLoading(bool loading);

    QString m_path;
    bool m_active = false;
    bool m_loading = false;
    bool m_supported = false;
    int m_page = 0;
    int m_pageCount = 0;
    quint64 m_generation = 0;
    QString m_imageSource;
    QString m_title;
    QString m_author;
    QString m_subject;
    QString m_keywords;
    QString m_error;
    QTimer m_debounce;
};
