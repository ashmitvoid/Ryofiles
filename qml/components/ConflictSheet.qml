// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    property real uiScale: 1
    property string sourcePath: ""
    property string destinationPath: ""
    property bool allowApplyToAll: true
    property bool allowReplace: true
    property bool applyToAll: false
    property bool restoreMode: false

    signal choose(int decision, bool applyToAll)

    visible: false
    anchors.fill: parent
    z: 1000

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.48)

        TapHandler { }
    }

    Rectangle {
        width: Math.min(560 * root.uiScale, parent.width - 48 * root.uiScale)
        height: body.implicitHeight + 44 * root.uiScale
        anchors.centerIn: parent
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong

        Column {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 22 * root.uiScale
            spacing: 14 * root.uiScale

            Text {
                text: root.restoreMode
                    ? "// RESTORE DESTINATION EXISTS"
                    : "// DESTINATION EXISTS"
                color: Ryoku.ink
                font.family: Ryoku.monoFont
                font.pixelSize: 11 * root.uiScale
                font.letterSpacing: 1.2
            }

            Text {
                width: parent.width
                text: "Source\n" + root.sourcePath + "\n\nDestination\n" + root.destinationPath
                wrapMode: Text.Wrap
                color: Ryoku.inkDim
                font.family: Ryoku.uiFont
                font.pixelSize: 12 * root.uiScale
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Ryoku.line
            }

            Row {
                visible: root.allowApplyToAll
                spacing: 9 * root.uiScale

                Rectangle {
                    width: 18 * root.uiScale
                    height: 18 * root.uiScale
                    radius: 4 * root.uiScale
                    color: root.applyToAll ? Ryoku.bone : "transparent"
                    border.width: 1
                    border.color: root.applyToAll ? Ryoku.bone : Ryoku.lineStrong

                    Text {
                        anchors.centerIn: parent
                        text: root.applyToAll ? "✓" : ""
                        color: Ryoku.inkOnBone
                        font.pixelSize: 11 * root.uiScale
                    }

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.applyToAll = !root.applyToAll }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Apply this choice to remaining conflicts"
                    color: Ryoku.inkDim
                    font.family: Ryoku.uiFont
                    font.pixelSize: 11 * root.uiScale

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.applyToAll = !root.applyToAll }
                }
            }

            Row {
                width: parent.width
                spacing: 8 * root.uiScale

                Repeater {
                    id: actionRepeater
                    model: root.restoreMode
                        ? [
                            { label: "KEEP BOTH", decision: 1, strong: false },
                            { label: "REPLACE", decision: 2, strong: true },
                            { label: "CANCEL", decision: 3, strong: false }
                        ]
                        : (root.allowReplace
                            ? [
                                { label: "SKIP", decision: 0, strong: false },
                                { label: "KEEP BOTH", decision: 1, strong: false },
                                { label: "REPLACE", decision: 2, strong: true },
                                { label: "CANCEL", decision: 3, strong: false }
                            ]
                            : [
                                { label: "SKIP", decision: 0, strong: false },
                                { label: "KEEP BOTH", decision: 1, strong: true },
                                { label: "CANCEL", decision: 3, strong: false }
                            ])

                    delegate: Rectangle {
                        id: button
                        required property var modelData

                        width: actionRepeater.count > 0
                            ? (body.width - Math.max(0, actionRepeater.count - 1) * 8 * root.uiScale)
                                / actionRepeater.count
                            : body.width
                        height: 34 * root.uiScale
                        radius: 6 * root.uiScale
                        color: modelData.strong
                            ? Ryoku.bone
                            : (buttonHover.hovered ? Ryoku.tint10 : "transparent")
                        border.width: 1
                        border.color: modelData.strong ? Ryoku.bone : Ryoku.line

                        Text {
                            anchors.centerIn: parent
                            text: button.modelData.label
                            color: button.modelData.strong ? Ryoku.inkOnBone : Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 9 * root.uiScale
                            font.weight: Font.Medium
                            font.letterSpacing: 0.7
                        }

                        HoverHandler { id: buttonHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: root.choose(
                                button.modelData.decision,
                                root.allowApplyToAll && root.applyToAll)
                        }
                    }
                }
            }
        }
    }
}
