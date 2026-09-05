// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QRegularExpression>
#include <QString>

#include <utility>

enum class PortalParentWindowKind {
    None,
    Wayland,
    X11,
};

struct PortalParentWindow {
    PortalParentWindowKind kind = PortalParentWindowKind::None;
    QString handle;

    bool isEmpty() const { return kind == PortalParentWindowKind::None; }

    QString identifier() const {
        switch (kind) {
        case PortalParentWindowKind::Wayland:
            return QStringLiteral("wayland:%1").arg(handle);
        case PortalParentWindowKind::X11:
            return QStringLiteral("x11:%1").arg(handle);
        case PortalParentWindowKind::None:
            return {};
        }
        return {};
    }

    static PortalParentWindow parse(const QString& value) {
        PortalParentWindow result;
        if (value.isEmpty())
            return result;

        constexpr qsizetype kMaximumWaylandHandleLength = 4096;
        const auto hasUnsafeControl = [](const QString& text) {
            for (const QChar character : text) {
                if (character.isNull() || character.category() == QChar::Other_Control)
                    return true;
            }
            return false;
        };

        if (value.startsWith(QStringLiteral("wayland:"))) {
            const QString handle = value.mid(8);
            if (handle.isEmpty()
                || handle.size() > kMaximumWaylandHandleLength
                || hasUnsafeControl(handle)) {
                return {};
            }
            result.kind = PortalParentWindowKind::Wayland;
            result.handle = handle;
            return result;
        }

        if (value.startsWith(QStringLiteral("x11:"))) {
            QString handle = value.mid(4);
            static const QRegularExpression xidPattern(
                QStringLiteral("^[0-9A-Fa-f]{1,16}$"));
            if (!xidPattern.match(handle).hasMatch())
                return {};

            bool ok = false;
            const qulonglong xid = handle.toULongLong(&ok, 16);
            if (!ok || xid == 0)
                return {};

            result.kind = PortalParentWindowKind::X11;
            result.handle = QString::number(xid, 16);
            return result;
        }

        return {};
    }
};
