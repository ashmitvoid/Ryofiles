// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    required property var files
    property real uiScale: 1
    property bool compact: false

    property bool restoring: false
    property bool pointerSelection: false
    readonly property bool previewOpen: root.session && root.session.previewVisible
    readonly property real previewWidth: Math.min(
        330 * root.uiScale,
        Math.max(220 * root.uiScale, root.width * 0.34))

    signal contextRequested(real sceneX, real sceneY, string path, bool isDirectory)

    clip: true

    function restoreState() {
        if (!root.session || !root.files || root.session.model !== root.files)
            return

        root.restoring = true
        if (root.files.loading)
            return

        Qt.callLater(function() {
            if (!root.session || !root.files || root.session.model !== root.files) {
                root.restoring = false
                return
            }

            var idx = root.files.indexOfPath(root.session.selectedPath)
            if (idx < 0 && root.files.count > 0)
                idx = 0

            view.currentIndex = idx
            var maxY = Math.max(0, view.contentHeight - view.height)
            view.contentY = Math.max(0, Math.min(root.session.scrollPosition, maxY))

            Qt.callLater(function() {
                root.restoring = false
            })
        })
    }

    onSessionChanged: {
        restoring = true
        restoreState()
    }
    onFilesChanged: {
        restoring = true
        restoreState()
    }

    Shortcut {
        sequence: "Ctrl+Shift+P"
        enabled: root.session !== null
        onActivated: root.session.previewVisible = !root.session.previewVisible
    }

    FolderFilterBar {
        id: filterBar
        z: 60
        anchors.left: parent.left
        anchors.right: previewPanel.left
        anchors.top: parent.top
        session: root.session
        files: root.files
        uiScale: root.uiScale
    }

    ListView {
        id: view

        anchors.left: parent.left
        anchors.top: filterBar.bottom
        anchors.bottom: parent.bottom
        anchors.right: previewPanel.left

        clip: true
        model: root.files
        spacing: root.compact ? 0 : 1 * root.uiScale
        currentIndex: -1
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true

        onCurrentIndexChanged: {
            if (root.restoring || root.pointerSelection || !activeFocus || !root.session || !root.files ||
                root.files.loading || currentIndex < 0)
                return
            root.session.selectSingle(currentIndex)
        }

        onContentYChanged: {
            if (!root.restoring && root.session && root.files && !root.files.loading)
                root.session.scrollPosition = Math.max(0, contentY)
        }

        delegate: Rectangle {
            id: row

            required property int index
            required property string name
            required property string filePath
            required property bool isDir
            required property string sizeText
            required property string modifiedText

            readonly property bool selected: {
                var revision = root.session ? root.session.selectionRevision : 0
                return revision >= 0 && root.session && root.session.isSelectedPath(filePath)
            }

            width: view.width
            height: (root.compact ? 34 : 44) * root.uiScale
            radius: 6 * root.uiScale
            color: selected
                ? Ryoku.bone
                : (mouse.containsMouse ? Ryoku.tint5 : "transparent")

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12 * root.uiScale
                anchors.rightMargin: 12 * root.uiScale
                spacing: 12 * root.uiScale

                Text {
                    width: 22 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.isDir ? "▰" : "·"
                    color: row.selected ? Ryoku.inkOnBoneDim : Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 13 * root.uiScale
                }

                GitStatusBadge {
                    anchors.verticalCenter: parent.verticalCenter
                    filePath: row.filePath
                    uiScale: root.uiScale
                    selected: row.selected
                }

                Text {
                    width: root.compact
                        ? Math.max(100, parent.width - (22 + 12 + 20 + 12 + 105) * root.uiScale)
                        : Math.max(120, parent.width - (22 + 12 + 20 + 12 + 118 + 12 + 154) * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.name
                    elide: Text.ElideMiddle
                    color: row.selected ? Ryoku.inkOnBone : Ryoku.ink
                    font.family: Ryoku.uiFont
                    font.pixelSize: (root.compact ? 12 : 13) * root.uiScale
                }

                Text {
                    width: (root.compact ? 105 : 118) * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: row.isDir ? "DIR" : row.sizeText
                    color: row.selected ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 10 * root.uiScale
                }

                Text {
                    visible: !root.compact
                    width: 154 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: row.modifiedText
                    color: row.selected ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 10 * root.uiScale
                }
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onClicked: function(event) {
                    root.pointerSelection = true
                    view.currentIndex = row.index

                    if (event.button === Qt.RightButton) {
                        if (!row.selected)
                            root.session.selectSingle(row.index)

                        var point = row.mapToItem(null, event.x, event.y)
                        root.contextRequested(point.x, point.y, row.filePath, row.isDir)
                        root.pointerSelection = false
                        view.forceActiveFocus()
                        return
                    }

                    if (event.modifiers & Qt.ShiftModifier)
                        root.session.selectRange(row.index)
                    else if (event.modifiers & Qt.ControlModifier)
                        root.session.toggleSelection(row.index)
                    else
                        root.session.selectSingle(row.index)

                    root.pointerSelection = false
                    view.forceActiveFocus()
                }

                onDoubleClicked: function(event) {
                    view.currentIndex = row.index
                    root.session.activate(row.index)
                }
            }
        }

        Keys.onReturnPressed: if (root.session) root.session.activate(currentIndex)
        Keys.onEnterPressed: if (root.session) root.session.activate(currentIndex)
    }

    PreviewPanel {
        id: previewPanel
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.previewOpen ? root.previewWidth : 0
        visible: root.previewOpen
        session: root.session
        desktop: Desktop
        thumbnails: Thumbnails
        uiScale: root.uiScale
    }

    Rectangle {
        id: previewToggle
        z: 50
        anchors.top: filterBar.bottom
        anchors.topMargin: 8 * root.uiScale
        anchors.right: parent.right
        anchors.rightMargin: 8 * root.uiScale
        width: previewLabel.implicitWidth + 18 * root.uiScale
        height: 28 * root.uiScale
        visible: !root.previewOpen
        radius: 6 * root.uiScale
        color: previewHover.hovered ? Ryoku.tint10 : Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.line

        Text {
            id: previewLabel
            anchors.centerIn: parent
            text: "PREVIEW"
            color: Ryoku.inkDim
            font.family: Ryoku.uiFont
            font.pixelSize: 9 * root.uiScale
            font.weight: Font.Medium
            font.letterSpacing: 1.0
        }

        HoverHandler {
            id: previewHover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: if (root.session) root.session.previewVisible = true
        }
    }

    Connections {
        target: root.session
        function onPathChanged() { root.restoring = true }
    }

    Connections {
        target: root.files
        function onCountChanged() { root.restoreState() }
    }

    Component.onCompleted: {
        view.forceActiveFocus()
        restoreState()
    }
}
