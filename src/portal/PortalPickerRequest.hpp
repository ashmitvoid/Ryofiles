// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

struct PortalFilterCondition {
    quint32 type = 0; // 0 = glob, 1 = MIME type
    QString pattern;
};

struct PortalFilter {
    QString name;
    QList<PortalFilterCondition> conditions;
};

enum class PortalPickerKind {
    OpenFile,
    SaveFile,
    SaveFiles,
};

struct PortalPickerRequest {
    bool valid = false;
    QString error;
    PortalPickerKind kind = PortalPickerKind::OpenFile;
    QString mode;
    bool multiple = false;
    QString initialDirectory;
    QString suggestedName;
    QString acceptLabel;
    QString title;
    bool modal = true;
    QList<PortalFilter> filters;
    QStringList saveFiles;

    static PortalPickerRequest openFile(
        const QString& title,
        const QVariantMap& options);
    static PortalPickerRequest saveFile(
        const QString& title,
        const QVariantMap& options);
    static PortalPickerRequest saveFiles(
        const QString& title,
        const QVariantMap& options);

    QStringList pickerArguments() const;
    bool pathMatchesFilters(const QString& path) const;
};

struct PortalPickerResult {
    bool valid = false;
    QString error;
    QStringList uris;

    static PortalPickerResult fromPickerStdout(
        const PortalPickerRequest& request,
        const QByteArray& standardOutput);
};

namespace PortalPickerParsing {

QString decodeNullTerminatedPath(const QByteArray& bytes, QString* error = nullptr);
QString decodeNullTerminatedLeafName(const QByteArray& bytes, QString* error = nullptr);
QList<PortalFilter> decodeFilters(const QVariant& value, QString* error = nullptr);
PortalFilter decodeFilter(const QVariant& value, QString* error = nullptr);
QStringList decodeFileNames(const QVariant& value, QString* error = nullptr);
QString uniqueDestinationName(
    const QString& directory,
    const QString& requestedName,
    const QStringList& alreadyReserved = {});

} // namespace PortalPickerParsing
