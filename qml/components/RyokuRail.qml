// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: rail

    required property var fs
    property real uiScale: 1
    property int trashCount: 0
    property bool trashActive: false
    signal navigate(string path)
    signal openTrash()

    width: 268 * uiScale

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Ryoku.line
    }

    Column {
        anchors.fill: parent
        anchors.margins: 24 * rail.uiScale
        spacing: 18 * rail.uiScale

        Rectangle {
            width: parent.width
            height: 64 * rail.uiScale
            color: "transparent"
            radius: 6 * rail.uiScale
            border.width: 1
            border.color: Ryoku.line

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16 * rail.uiScale
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10 * rail.uiScale

                Text {
                    text: "力"
                    color: Ryoku.ink
                    font.family: "Noto Sans CJK JP"
                    font.pixelSize: 22 * rail.uiScale
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        text: "RYOFILES"
                        color: Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 14 * rail.uiScale
                        font.weight: Font.Medium
                        font.letterSpacing: 2.2
                    }
                    Text {
                        text: "//FILES_"
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 10 * rail.uiScale
                        font.letterSpacing: 1.3
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: 2 * rail.uiScale

            Text {
                text: "01  PLACES"
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * rail.uiScale
                font.letterSpacing: 1.5
                bottomPadding: 5 * rail.uiScale
            }

            Repeater {
                model: [
                    { label: "Home", path: rail.fs.home },
                    { label: "Desktop", path: rail.fs.desktop },
                    { label: "Documents", path: rail.fs.documents },
                    { label: "Downloads", path: rail.fs.downloads },
                    { label: "Pictures", path: rail.fs.pictures },
                    { label: "Music", path: rail.fs.music },
                    { label: "Videos", path: rail.fs.videos }
                ]

                delegate: Rectangle {
                    id: place
                    required property var modelData
                    width: parent.width
                    height: 34 * rail.uiScale
                    radius: 6 * rail.uiScale
                    color: rail.fs.path === modelData.path
                        ? Ryoku.bone
                        : (hover.hovered ? Ryoku.tint10 : "transparent")

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12 * rail.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: (rail.fs.path === place.modelData.path ? "//  " : "    ")
                            + place.modelData.label
                        color: rail.fs.path === place.modelData.path
                            ? Ryoku.inkOnBone
                            : Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 13 * rail.uiScale
                    }

                    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: rail.navigate(place.modelData.path) }
                }
            }
        }

        Column {
            width: parent.width
            spacing: 2 * rail.uiScale

            Text {
                text: "02  SYSTEM"
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * rail.uiScale
                font.letterSpacing: 1.5
                bottomPadding: 5 * rail.uiScale
            }

            Rectangle {
                id: trashButton
                width: parent.width
                height: 34 * rail.uiScale
                radius: 6 * rail.uiScale
                color: rail.trashActive
                    ? Ryoku.bone
                    : (trashHover.hovered ? Ryoku.tint10 : "transparent")

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 12 * rail.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: (rail.trashActive ? "//  " : "    ") + "Trash"
                    color: rail.trashActive ? Ryoku.inkOnBone : Ryoku.inkDim
                    font.family: Ryoku.uiFont
                    font.pixelSize: 13 * rail.uiScale
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 12 * rail.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: rail.trashCount > 0 ? String(rail.trashCount) : ""
                    color: rail.trashActive ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * rail.uiScale
                }

                HoverHandler { id: trashHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: rail.openTrash() }
            }
        }

        Item { width: 1; height: 1 }

        Text {
            width: parent.width
            text: "Ryoku-native file manager\nPhase 2 // daily actions"
            color: Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * rail.uiScale
            lineHeight: 1.4
            wrapMode: Text.WordWrap
        }
    }
}
