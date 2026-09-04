// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Rectangle {
    id: root

    property real uiScale: 1

    visible: GitStatus.repository && GitStatus.branchName !== ""
    width: visible ? content.implicitWidth + 18 * uiScale : 0
    height: visible ? 28 * uiScale : 0
    radius: 6 * uiScale
    color: GitStatus.changedCount > 0 ? Ryoku.tint5 : "transparent"
    border.width: visible ? 1 : 0
    border.color: GitStatus.truncated
        ? Ryoku.sun
        : (GitStatus.changedCount > 0 ? Ryoku.lineStrong : Ryoku.line)

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 7 * root.uiScale

        Text {
            text: "⌘"
            color: Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 10 * root.uiScale
        }

        Text {
            text: GitStatus.detached ? "DETACHED " + GitStatus.branchName : GitStatus.branchName
            color: Ryoku.inkDim
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * root.uiScale
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Text {
            visible: GitStatus.changedCount > 0
            text: GitStatus.changedCount.toString()
            color: GitStatus.truncated ? Ryoku.sun : Ryoku.inkMuted
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * root.uiScale
            font.weight: Font.DemiBold
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: GitStatus.refresh()
    }
}
