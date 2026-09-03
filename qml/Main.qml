// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core
import "components"

Window {
    id: root
    width: 1500
    height: 850
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: "Ryofiles"
    color: Ryoku.paper

    readonly property real u: Ryoku.uiScaleFor(Screen.name)
    property string lastError: ""

    DirectoryModel {
        id: files
        onErrorOccurred: message => {
            root.lastError = message
            errorClear.restart()
        }
    }

    Timer {
        id: errorClear
        interval: 4000
        onTriggered: root.lastError = ""
    }

    Shortcut { sequence: "Ctrl+H"; onActivated: files.showHidden = !files.showHidden }
    Shortcut { sequence: "Alt+Up"; onActivated: files.goUp() }
    Shortcut { sequence: "Backspace"; onActivated: files.goUp() }
    Shortcut { sequence: "Ctrl+R"; onActivated: files.refresh() }
    Shortcut { sequence: "Ctrl+L"; onActivated: location.forceActiveFocus() }

    Rectangle {
        anchors.fill: parent
        color: Ryoku.paper

        RyokuRail {
            id: rail
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            uiScale: root.u
            fs: files
            onNavigate: path => files.path = path
        }

        Item {
            id: stage
            anchors.left: rail.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            Rectangle {
                id: toolbar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 58 * root.u
                color: "transparent"

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Ryoku.line
                }

                Rectangle {
                    id: upButton
                    anchors.left: parent.left
                    anchors.leftMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34 * root.u
                    height: 34 * root.u
                    radius: 6 * root.u
                    color: upHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        anchors.centerIn: parent
                        text: "↑"
                        color: Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 16 * root.u
                    }
                    HoverHandler { id: upHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: files.goUp() }
                }

                Rectangle {
                    anchors.left: upButton.right
                    anchors.leftMargin: 10 * root.u
                    anchors.right: hiddenBadge.left
                    anchors.rightMargin: 10 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    height: 34 * root.u
                    radius: 6 * root.u
                    color: "transparent"
                    border.width: location.activeFocus ? 2 : 1
                    border.color: location.activeFocus ? Ryoku.ink : Ryoku.line

                    TextInput {
                        id: location
                        anchors.fill: parent
                        anchors.leftMargin: 10 * root.u
                        anchors.rightMargin: 10 * root.u
                        verticalAlignment: Text.AlignVCenter
                        color: Ryoku.ink
                        selectionColor: Ryoku.bone
                        selectedTextColor: Ryoku.inkOnBone
                        font.family: Ryoku.monoFont
                        font.pixelSize: 11 * root.u
                        text: files.path
                        selectByMouse: true
                        onAccepted: {
                            files.path = text
                            focus = false
                        }

                        Connections {
                            target: files
                            function onPathChanged() { location.text = files.path }
                        }
                    }
                }

                Rectangle {
                    id: hiddenBadge
                    anchors.right: parent.right
                    anchors.rightMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    width: hiddenText.implicitWidth + 20 * root.u
                    height: 30 * root.u
                    radius: 6 * root.u
                    color: files.showHidden ? Ryoku.bone : "transparent"
                    border.width: 1
                    border.color: files.showHidden ? Ryoku.bone : Ryoku.line

                    Text {
                        id: hiddenText
                        anchors.centerIn: parent
                        text: "HIDDEN"
                        color: files.showHidden ? Ryoku.inkOnBone : Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.u
                        font.weight: Font.Medium
                        font.letterSpacing: 1.2
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: files.showHidden = !files.showHidden }
                }
            }

            Item {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: toolbar.bottom
                anchors.bottom: status.top
                anchors.margins: 20 * root.u

                Text {
                    anchors.centerIn: parent
                    visible: !files.loading && files.count === 0
                    text: "// EMPTY_\nThis folder has no visible items"
                    color: Ryoku.inkMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Ryoku.monoFont
                    font.pixelSize: 11 * root.u
                    lineHeight: 1.7
                }

                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true
                    model: files
                    spacing: 1 * root.u
                    currentIndex: files.count > 0 ? 0 : -1
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string name
                        required property string filePath
                        required property bool isDir
                        required property string sizeText
                        required property string modifiedText

                        width: list.width
                        height: 44 * root.u
                        radius: 6 * root.u
                        color: list.currentIndex === index
                            ? Ryoku.bone
                            : (rowHover.hovered ? Ryoku.tint5 : "transparent")

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12 * root.u
                            anchors.rightMargin: 12 * root.u
                            spacing: 12 * root.u

                            Text {
                                width: 22 * root.u
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.isDir ? "▰" : "·"
                                color: list.currentIndex === row.index
                                    ? Ryoku.inkOnBoneDim
                                    : Ryoku.inkFaint
                                font.family: Ryoku.monoFont
                                font.pixelSize: 13 * root.u
                            }

                            Text {
                                width: Math.max(120, parent.width - (22 + 12 + 118 + 12 + 154) * root.u)
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.name
                                elide: Text.ElideMiddle
                                color: list.currentIndex === row.index
                                    ? Ryoku.inkOnBone
                                    : Ryoku.ink
                                font.family: Ryoku.uiFont
                                font.pixelSize: 13 * root.u
                            }

                            Text {
                                width: 118 * root.u
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignRight
                                text: row.isDir ? "DIR" : row.sizeText
                                color: list.currentIndex === row.index
                                    ? Ryoku.inkOnBoneDim
                                    : Ryoku.inkMuted
                                font.family: Ryoku.monoFont
                                font.pixelSize: 10 * root.u
                            }

                            Text {
                                width: 154 * root.u
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignRight
                                text: row.modifiedText
                                color: list.currentIndex === row.index
                                    ? Ryoku.inkOnBoneDim
                                    : Ryoku.inkMuted
                                font.family: Ryoku.monoFont
                                font.pixelSize: 10 * root.u
                            }
                        }

                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: list.currentIndex = row.index
                            onDoubleTapped: files.activate(row.index)
                        }
                    }

                    Keys.onReturnPressed: files.activate(currentIndex)
                    Keys.onEnterPressed: files.activate(currentIndex)
                    Component.onCompleted: forceActiveFocus()
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8 * root.u
                    width: loadingLabel.implicitWidth + 18 * root.u
                    height: 26 * root.u
                    visible: files.loading
                    radius: 6 * root.u
                    color: Ryoku.paperLift
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        id: loadingLabel
                        anchors.centerIn: parent
                        text: "READING…"
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.u
                        font.letterSpacing: 1.1
                    }
                }
            }

            Rectangle {
                id: status
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 38 * root.u
                color: "transparent"

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Ryoku.line
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.lastError !== ""
                        ? root.lastError
                        : (files.count + " items" + (files.showHidden ? "  // hidden visible" : ""))
                    color: root.lastError !== "" ? Ryoku.sun : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 10 * root.u
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    text: "CTRL+L LOCATION   CTRL+H HIDDEN   ALT+↑ UP"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.u
                }
            }
        }
    }
}
