// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Rectangle {
    id: root

    required property string filePath
    property real uiScale: 1
    property bool selected: false

    readonly property string statusCode: {
        var currentRevision = GitStatus.revision
        return currentRevision >= 0 && filePath !== ""
            ? GitStatus.statusForPath(filePath)
            : ""
    }

    readonly property string mark: {
        switch (statusCode) {
        case "conflict": return "!"
        case "mixed": return "±"
        case "staged": return "+"
        case "modified": return "M"
        case "untracked": return "?"
        case "ignored": return "I"
        default: return ""
        }
    }

    readonly property string detail: GitStatus.statusLabelForPath(filePath)

    visible: mark !== ""
    width: visible ? 20 * uiScale : 0
    height: visible ? 20 * uiScale : 0
    radius: 5 * uiScale
    color: selected
        ? Qt.rgba(1, 1, 1, 0.12)
        : (statusCode === "conflict" ? Ryoku.tint16 : Ryoku.tint5)
    border.width: visible ? 1 : 0
    border.color: selected
        ? Ryoku.inkOnBoneDim
        : (statusCode === "conflict" ? Ryoku.sun : Ryoku.line)

    Text {
        anchors.centerIn: parent
        text: root.mark
        color: root.selected
            ? Ryoku.inkOnBone
            : (root.statusCode === "conflict" ? Ryoku.sun : Ryoku.inkMuted)
        font.family: Ryoku.monoFont
        font.pixelSize: 9 * root.uiScale
        font.weight: Font.DemiBold
    }
}
