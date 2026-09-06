// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var tabs
    property real uiScale: 1

    height: 44 * uiScale

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Ryoku.lineSoft
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        anchors.leftMargin: 14 * root.uiScale
        anchors.rightMargin: 14 * root.uiScale
        clip: true
        contentWidth: tabRow.implicitWidth
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        interactive: contentWidth > width

        Row {
            id: tabRow
            height: parent.height
            spacing: 2 * root.uiScale

            Repeater {
                model: root.tabs

                delegate: Item {
                    id: tab

                    required property int index
                    required property string title
                    required property string path
                    required property bool active

                    width: Math.min(
                        Math.max(118 * root.uiScale, titleText.implicitWidth + 54 * root.uiScale),
                        238 * root.uiScale)
                    height: parent.height

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 6 * root.uiScale
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 3 * root.uiScale
                        radius: 7 * root.uiScale
                        color: tab.active
                            ? Ryoku.paperLift
                            : (tabHover.hovered ? Ryoku.tint5 : "transparent")

                        Behavior on color {
                            enabled: !Ryoku.reduceMotion
                            ColorAnimation { duration: Ryoku.duration(120) }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 10 * root.uiScale
                        anchors.rightMargin: 10 * root.uiScale
                        anchors.bottom: parent.bottom
                        height: tab.active ? 2 * root.uiScale : 0
                        radius: 1 * root.uiScale
                        color: Ryoku.sun

                        Behavior on height {
                            enabled: !Ryoku.reduceMotion
                            NumberAnimation { duration: Ryoku.duration(120); easing.type: Easing.OutCubic }
                        }
                    }

                    Text {
                        id: titleText
                        anchors.left: parent.left
                        anchors.leftMargin: 13 * root.uiScale
                        anchors.right: closeButton.left
                        anchors.rightMargin: 6 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: tab.title
                        elide: Text.ElideRight
                        color: tab.active ? Ryoku.ink : Ryoku.inkMuted
                        font.family: Ryoku.uiFont
                        font.pixelSize: 11 * root.uiScale
                        font.weight: tab.active ? Font.Medium : Font.Normal
                    }

                    Item {
                        id: closeButton
                        anchors.right: parent.right
                        anchors.rightMargin: 8 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24 * root.uiScale
                        height: 24 * root.uiScale

                        Rectangle {
                            anchors.fill: parent
                            radius: 6 * root.uiScale
                            color: closeHover.hovered ? Ryoku.tint10 : "transparent"
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "×"
                            color: closeHover.hovered ? Ryoku.ink : Ryoku.inkFaint
                            font.family: Ryoku.uiFont
                            font.pixelSize: 14 * root.uiScale
                        }

                        HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: root.tabs.closeTab(tab.index)
                        }
                    }

                    HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: root.tabs.currentIndex = tab.index
                    }
                }
            }

            Item {
                width: 38 * root.uiScale
                height: parent.height

                Rectangle {
                    anchors.centerIn: parent
                    width: 28 * root.uiScale
                    height: 28 * root.uiScale
                    radius: 7 * root.uiScale
                    color: addHover.hovered ? Ryoku.tint10 : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        color: addHover.hovered ? Ryoku.ink : Ryoku.inkMuted
                        font.family: Ryoku.uiFont
                        font.pixelSize: 17 * root.uiScale
                    }
                }

                HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: root.tabs.newTab("")
                }
            }
        }
    }
}
