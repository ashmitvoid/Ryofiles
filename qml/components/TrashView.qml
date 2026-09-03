// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var trash
    property real uiScale: 1

    signal restoreRequested(string itemId)

    Text {
        anchors.centerIn: parent
        visible: !root.trash.busy && root.trash.count === 0
        text: "// TRASH EMPTY_\nNothing to restore"
        horizontalAlignment: Text.AlignHCenter
        color: Ryoku.inkMuted
        font.family: Ryoku.monoFont
        font.pixelSize: 11 * root.uiScale
        lineHeight: 1.7
    }

    ListView {
        anchors.fill: parent
        clip: true
        model: root.trash
        reuseItems: true
        spacing: 1 * root.uiScale
        boundsBehavior: Flickable.StopAtBounds

        delegate: Rectangle {
            id: row

            required property string itemId
            required property string name
            required property string originalPath
            required property string deletionDate
            required property bool orphaned

            width: ListView.view.width
            height: 54 * root.uiScale
            radius: 6 * root.uiScale
            color: rowHover.hovered ? Ryoku.tint5 : "transparent"

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12 * root.uiScale
                anchors.top: parent.top
                anchors.topMargin: 9 * root.uiScale
                width: parent.width - 150 * root.uiScale
                text: row.name
                elide: Text.ElideMiddle
                color: row.orphaned ? Ryoku.sun : Ryoku.ink
                font.family: Ryoku.uiFont
                font.pixelSize: 12 * root.uiScale
                font.weight: Font.Medium
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12 * root.uiScale
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8 * root.uiScale
                width: parent.width - 150 * root.uiScale
                text: row.orphaned
                    ? "Metadata unavailable"
                    : row.originalPath + (row.deletionDate !== "" ? "  //  " + row.deletionDate : "")
                elide: Text.ElideMiddle
                color: row.orphaned ? Ryoku.sun : Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 12 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                width: 92 * root.uiScale
                height: 30 * root.uiScale
                radius: 6 * root.uiScale
                color: restoreHover.hovered && !row.orphaned ? Ryoku.bone : "transparent"
                border.width: 1
                border.color: row.orphaned ? Ryoku.lineSoft : Ryoku.line
                opacity: row.orphaned ? 0.45 : 1.0

                Text {
                    anchors.centerIn: parent
                    text: "RESTORE"
                    color: restoreHover.hovered && !row.orphaned
                        ? Ryoku.inkOnBone
                        : Ryoku.inkDim
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * root.uiScale
                    font.weight: Font.Medium
                }

                HoverHandler {
                    id: restoreHover
                    enabled: !row.orphaned
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    enabled: !row.orphaned
                    onTapped: root.restoreRequested(row.itemId)
                }
            }

            HoverHandler { id: rowHover }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Ryoku.lineSoft
            }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8 * root.uiScale
        width: busyLabel.implicitWidth + 18 * root.uiScale
        height: 26 * root.uiScale
        visible: root.trash.busy
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.line

        Text {
            id: busyLabel
            anchors.centerIn: parent
            text: "UPDATING…"
            color: Ryoku.inkMuted
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * root.uiScale
        }
    }
}
