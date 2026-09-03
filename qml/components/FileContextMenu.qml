// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    property real uiScale: 1
    property string targetPath: ""
    property bool targetIsDirectory: false
    property int selectionCount: 0
    property bool clipboardHasFiles: false

    signal openRequested()
    signal openNewTabRequested()
    signal openWithRequested()
    signal copyRequested()
    signal cutRequested()
    signal pasteIntoRequested()
    signal duplicateRequested()
    signal renameRequested()
    signal trashRequested()
    signal propertiesRequested()

    visible: false
    anchors.fill: parent
    z: 900

    function openAt(sceneX, sceneY, path, isDirectory, selectedCount, hasClipboard) {
        targetPath = path
        targetIsDirectory = isDirectory
        selectionCount = selectedCount
        clipboardHasFiles = hasClipboard
        visible = true

        Qt.callLater(function() {
            menu.x = Math.max(
                8 * root.uiScale,
                Math.min(sceneX, root.width - menu.width - 8 * root.uiScale))
            menu.y = Math.max(
                8 * root.uiScale,
                Math.min(sceneY, root.height - menu.height - 8 * root.uiScale))
        })
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.visible = false
    }

    Rectangle {
        id: menu
        width: 230 * root.uiScale
        height: items.implicitHeight + 16 * root.uiScale
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Column {
            id: items
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8 * root.uiScale
            spacing: 2 * root.uiScale

            Repeater {
                model: [
                    { label: root.targetIsDirectory ? "OPEN" : "OPEN", action: "open", enabled: root.selectionCount === 1, visible: true },
                    { label: "OPEN IN NEW TAB", action: "newtab", enabled: root.selectionCount === 1, visible: root.targetIsDirectory },
                    { label: "OPEN WITH…", action: "openwith", enabled: root.selectionCount === 1, visible: !root.targetIsDirectory },
                    { label: "COPY", action: "copy", enabled: root.selectionCount > 0, visible: true },
                    { label: "CUT", action: "cut", enabled: root.selectionCount > 0, visible: true },
                    { label: "PASTE INTO", action: "pasteinto", enabled: root.clipboardHasFiles, visible: root.targetIsDirectory && root.selectionCount === 1 },
                    { label: "DUPLICATE", action: "duplicate", enabled: root.selectionCount > 0, visible: true },
                    { label: "RENAME", action: "rename", enabled: root.selectionCount === 1, visible: true },
                    { label: "MOVE TO TRASH", action: "trash", enabled: root.selectionCount > 0, visible: true },
                    { label: "PROPERTIES", action: "properties", enabled: root.selectionCount === 1, visible: true }
                ]

                delegate: Rectangle {
                    id: actionRow
                    required property var modelData

                    width: items.width
                    height: modelData.visible ? 32 * root.uiScale : 0
                    visible: modelData.visible
                    radius: 5 * root.uiScale
                    color: actionHover.hovered && modelData.enabled
                        ? Ryoku.tint10
                        : "transparent"
                    opacity: modelData.enabled ? 1.0 : 0.4

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: actionRow.modelData.label
                        color: actionRow.modelData.action === "trash"
                            ? Ryoku.sun
                            : Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10 * root.uiScale
                        font.weight: Font.Medium
                        font.letterSpacing: 0.8
                    }

                    HoverHandler {
                        id: actionHover
                        enabled: actionRow.modelData.enabled
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        enabled: actionRow.modelData.enabled
                        onTapped: {
                            root.visible = false
                            switch (actionRow.modelData.action) {
                            case "open": root.openRequested(); break
                            case "newtab": root.openNewTabRequested(); break
                            case "openwith": root.openWithRequested(); break
                            case "copy": root.copyRequested(); break
                            case "cut": root.cutRequested(); break
                            case "pasteinto": root.pasteIntoRequested(); break
                            case "duplicate": root.duplicateRequested(); break
                            case "rename": root.renameRequested(); break
                            case "trash": root.trashRequested(); break
                            case "properties": root.propertiesRequested(); break
                            }
                        }
                    }
                }
            }
        }
    }
}
