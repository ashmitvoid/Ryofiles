// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    property real uiScale: 1
    property string originalName: ""
    property string heading: "// RENAME"
    property string actionLabel: "RENAME"
    property bool selectBaseName: true
    signal accepted(string newName)
    signal cancelled()

    visible: false
    anchors.fill: parent
    z: 1000

    function openFor(name, headingText, actionText, selectBase) {
        originalName = name
        heading = headingText
        actionLabel = actionText
        selectBaseName = selectBase
        field.text = name
        visible = true

        Qt.callLater(function() {
            field.forceActiveFocus()
            if (selectBaseName) {
                var dot = name.lastIndexOf(".")
                field.select(0, dot > 0 ? dot : name.length)
            } else {
                field.selectAll()
            }
        })
    }

    function open(name) {
        openFor(name, "// RENAME", "RENAME", true)
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.48)
        TapHandler { onTapped: root.cancelled() }
    }

    Rectangle {
        width: Math.min(460 * root.uiScale, parent.width - 48 * root.uiScale)
        height: 156 * root.uiScale
        anchors.centerIn: parent
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong

        Column {
            anchors.fill: parent
            anchors.margins: 20 * root.uiScale
            spacing: 14 * root.uiScale

            Text {
                text: root.heading
                color: Ryoku.ink
                font.family: Ryoku.monoFont
                font.pixelSize: 11 * root.uiScale
                font.letterSpacing: 1.2
            }

            Rectangle {
                width: parent.width
                height: 38 * root.uiScale
                radius: 6 * root.uiScale
                color: "transparent"
                border.width: field.activeFocus ? 2 : 1
                border.color: field.activeFocus ? Ryoku.ink : Ryoku.line

                TextInput {
                    id: field
                    anchors.fill: parent
                    anchors.leftMargin: 10 * root.uiScale
                    anchors.rightMargin: 10 * root.uiScale
                    verticalAlignment: Text.AlignVCenter
                    color: Ryoku.ink
                    selectionColor: Ryoku.bone
                    selectedTextColor: Ryoku.inkOnBone
                    selectByMouse: true
                    font.family: Ryoku.uiFont
                    font.pixelSize: 12 * root.uiScale
                    onAccepted: {
                        if (text.trim() !== "")
                            root.accepted(text.trim())
                    }
                }
            }

            Row {
                anchors.right: parent.right
                spacing: 8 * root.uiScale

                Rectangle {
                    width: 82 * root.uiScale
                    height: 30 * root.uiScale
                    radius: 6 * root.uiScale
                    color: cancelHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line
                    Text {
                        anchors.centerIn: parent
                        text: "CANCEL"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.Medium
                    }
                    HoverHandler { id: cancelHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.cancelled() }
                }

                Rectangle {
                    width: 82 * root.uiScale
                    height: 30 * root.uiScale
                    radius: 6 * root.uiScale
                    color: Ryoku.bone
                    border.width: 1
                    border.color: Ryoku.bone
                    Text {
                        anchors.centerIn: parent
                        text: root.actionLabel
                        color: Ryoku.inkOnBone
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.Medium
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: if (field.text.trim() !== "")
                            root.accepted(field.text.trim())
                    }
                }
            }
        }
    }
}
