// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var desktop
    property real uiScale: 1
    property var details: ({})

    visible: false
    anchors.fill: parent
    z: 1000

    function openFor(path) {
        details = desktop.propertiesForPath(path)
        visible = Object.keys(details).length > 0
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.48)
        TapHandler { onTapped: root.visible = false }
    }

    Rectangle {
        width: Math.min(580 * root.uiScale, parent.width - 48 * root.uiScale)
        height: Math.min(600 * root.uiScale, parent.height - 64 * root.uiScale)
        anchors.centerIn: parent
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong

        Column {
            anchors.fill: parent
            anchors.margins: 22 * root.uiScale
            spacing: 12 * root.uiScale

            Row {
                width: parent.width

                Text {
                    width: parent.width - close.width
                    text: "// PROPERTIES"
                    color: Ryoku.ink
                    font.family: Ryoku.monoFont
                    font.pixelSize: 11 * root.uiScale
                    font.letterSpacing: 1.2
                }

                Text {
                    id: close
                    text: "×"
                    color: closeHover.hovered ? Ryoku.ink : Ryoku.inkMuted
                    font.family: Ryoku.uiFont
                    font.pixelSize: 18 * root.uiScale
                    HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.visible = false }
                }
            }

            Text {
                width: parent.width
                text: root.details.name || ""
                elide: Text.ElideMiddle
                color: Ryoku.ink
                font.family: Ryoku.uiFont
                font.pixelSize: 19 * root.uiScale
                font.weight: Font.Medium
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Ryoku.line
            }

            ListView {
                width: parent.width
                height: parent.height - 92 * root.uiScale
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: [
                    { label: "TYPE", value: root.details.type || "" },
                    { label: "MIME", value: root.details.mime || "" },
                    { label: "LOCATION", value: root.details.parent || "" },
                    { label: "PATH", value: root.details.path || "" },
                    { label: "SIZE", value: root.details.sizeText || "" },
                    { label: "MODIFIED", value: root.details.modified || "" },
                    { label: "CREATED", value: root.details.created || "" },
                    { label: "OWNER", value: root.details.owner || "" },
                    { label: "GROUP", value: root.details.group || "" },
                    { label: "PERMISSIONS", value: root.details.permissions || "" },
                    { label: "LINK TARGET", value: root.details.symlinkTarget || "" }
                ]

                delegate: Item {
                    id: propertyRow
                    required property var modelData

                    width: ListView.view.width
                    height: modelData.value !== "" ? 52 * root.uiScale : 0
                    visible: modelData.value !== ""

                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: 7 * root.uiScale
                        width: 110 * root.uiScale
                        text: propertyRow.modelData.label
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.uiScale
                        font.letterSpacing: 0.9
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 122 * root.uiScale
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 7 * root.uiScale
                        text: propertyRow.modelData.value
                        elide: Text.ElideMiddle
                        color: Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 11 * root.uiScale
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Ryoku.lineSoft
                    }
                }
            }

            Text {
                visible: root.details.isDirectory === true
                width: parent.width
                text: "// Folder size is intentionally not calculated recursively."
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
            }
        }
    }
}
