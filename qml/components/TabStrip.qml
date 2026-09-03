// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var tabs
    property real uiScale: 1

    height: 42 * uiScale

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Ryoku.line
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        anchors.leftMargin: 12 * root.uiScale
        anchors.rightMargin: 12 * root.uiScale
        clip: true
        contentWidth: row.implicitWidth
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        interactive: contentWidth > width

        Row {
            id: row
            height: parent.height
            spacing: 6 * root.uiScale

            Repeater {
                model: root.tabs

                delegate: Rectangle {
                    id: plate

                    required property int index
                    required property string title
                    required property string path
                    required property bool active

                    width: Math.min(
                        Math.max(132 * root.uiScale, labelRow.implicitWidth + 30 * root.uiScale),
                        260 * root.uiScale
                    )
                    height: 32 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 6 * root.uiScale

                    color: active
                        ? Ryoku.bone
                        : (plateHover.hovered ? Ryoku.tint5 : "transparent")
                    border.width: 1
                    border.color: active ? Ryoku.bone : Ryoku.line

                    Behavior on color {
                        ColorAnimation { duration: Ryoku.duration(90) }
                    }

                    Row {
                        id: labelRow
                        anchors.left: parent.left
                        anchors.right: closeBox.left
                        anchors.leftMargin: 11 * root.uiScale
                        anchors.rightMargin: 4 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7 * root.uiScale

                        Text {
                            visible: plate.active
                            anchors.verticalCenter: parent.verticalCenter
                            text: "//"
                            color: Ryoku.inkOnBoneDim
                            font.family: Ryoku.monoFont
                            font.pixelSize: 9 * root.uiScale
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(20, labelRow.width - (plate.active ? 26 : 0) * root.uiScale)
                            text: plate.title.toUpperCase()
                            elide: Text.ElideRight
                            color: plate.active ? Ryoku.inkOnBone : Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 10 * root.uiScale
                            font.weight: Font.Medium
                            font.letterSpacing: 1.2
                        }
                    }

                    Rectangle {
                        id: closeBox
                        anchors.right: parent.right
                        anchors.rightMargin: 5 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22 * root.uiScale
                        height: 22 * root.uiScale
                        radius: 5 * root.uiScale
                        color: closeHover.hovered
                            ? (plate.active ? Ryoku.inkOnBoneDim : Ryoku.tint10)
                            : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "×"
                            color: plate.active ? Ryoku.inkOnBone : Ryoku.inkMuted
                            font.family: Ryoku.uiFont
                            font.pixelSize: 13 * root.uiScale
                        }

                        HoverHandler {
                            id: closeHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: root.tabs.closeTab(plate.index)
                        }
                    }

                    HoverHandler {
                        id: plateHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: root.tabs.currentIndex = plate.index
                    }
                }
            }

            Rectangle {
                width: 32 * root.uiScale
                height: 32 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                radius: 6 * root.uiScale
                color: addHover.hovered ? Ryoku.tint10 : "transparent"
                border.width: 1
                border.color: Ryoku.line

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: Ryoku.inkDim
                    font.family: Ryoku.uiFont
                    font.pixelSize: 16 * root.uiScale
                }

                HoverHandler {
                    id: addHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: root.tabs.newTab("")
                }
            }
        }
    }
}
