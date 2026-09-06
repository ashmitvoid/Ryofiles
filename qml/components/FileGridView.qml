// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    required property var files
    required property var keyboardController
    property real uiScale: 1
    property bool paneActive: true

    property bool restoring: false
    readonly property bool remote: root.session && root.session.remote
    readonly property bool previewOpen: !root.remote && root.session && root.session.previewVisible
    readonly property real previewWidth: Math.min(
        330 * root.uiScale,
        Math.max(220 * root.uiScale, root.width * 0.34))

    signal contextRequested(real sceneX, real sceneY, string path, bool isDirectory)
    signal paneActivated()

    clip: true

    function focusView() {
        if (!root.paneActive)
            return
        view.forceActiveFocus()
    }

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
                if (root.paneActive)
                    root.focusView()
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
    onPaneActiveChanged: {
        if (root.paneActive)
            Qt.callLater(root.focusView)
    }

    Shortcut {
        sequence: "Ctrl+Shift+P"
        enabled: root.paneActive && root.session !== null && !root.remote
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
        paneActive: root.paneActive
    }

    GridView {
        id: view

        anchors.left: parent.left
        anchors.top: filterBar.bottom
        anchors.bottom: parent.bottom
        anchors.right: previewPanel.left

        clip: true
        model: root.files
        cellWidth: 158 * root.uiScale
        cellHeight: 126 * root.uiScale
        cacheBuffer: 0
        currentIndex: -1
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        activeFocusOnTab: root.paneActive

        readonly property int keyboardColumns: Math.max(1, Math.floor(width / Math.max(1, cellWidth)))
        readonly property int keyboardRowsPerPage: Math.max(1, Math.floor(height / Math.max(1, cellHeight)))
        readonly property int keyboardPageStep: keyboardColumns * keyboardRowsPerPage

        onContentYChanged: {
            if (!root.restoring && root.session && root.files && !root.files.loading)
                root.session.scrollPosition = Math.max(0, contentY)
        }

        delegate: Rectangle {
            id: tile

            required property int index
            required property string name
            required property string filePath
            required property bool isDir
            required property string sizeText
            required property bool thumbnailCandidate

            readonly property bool selected: {
                var revision = root.session ? root.session.selectionRevision : 0
                return revision >= 0 && root.session && root.session.isSelectedPath(filePath)
            }
            readonly property bool currentItem:
                root.paneActive && view.activeFocus && view.currentIndex === tile.index

            width: view.cellWidth - 8 * root.uiScale
            height: view.cellHeight - 8 * root.uiScale
            radius: 6 * root.uiScale
            color: selected
                ? Ryoku.bone
                : (mouse.containsMouse ? Ryoku.tint5 : "transparent")
            border.width: currentItem ? 1 : (selected ? 1 : 0)
            border.color: currentItem
                ? (selected ? Ryoku.inkOnBoneDim : Ryoku.lineStrong)
                : (selected ? Ryoku.bone : "transparent")

            Column {
                anchors.fill: parent
                anchors.margins: 10 * root.uiScale
                spacing: 6 * root.uiScale

                Item {
                    width: parent.width
                    height: 56 * root.uiScale
                    clip: true

                    Image {
                        id: thumbnail
                        anchors.centerIn: parent
                        width: Math.min(parent.width, 96 * root.uiScale)
                        height: parent.height
                        visible: !root.remote && tile.thumbnailCandidate
                        source: visible
                            ? Thumbnails.urlForPath(
                                tile.filePath,
                                Math.max(64, Math.round(128 * root.uiScale)),
                                0)
                            : ""
                        sourceSize.width: Math.max(64, Math.round(128 * root.uiScale))
                        sourceSize.height: Math.max(64, Math.round(128 * root.uiScale))
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: false
                        smooth: true
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !thumbnail.visible || thumbnail.status !== Image.Ready
                        text: tile.isDir
                            ? "▰"
                            : (thumbnail.visible && thumbnail.status === Image.Loading ? "···" : "□")
                        color: tile.selected ? Ryoku.inkOnBoneDim : Ryoku.inkDim
                        font.family: Ryoku.monoFont
                        font.pixelSize: thumbnail.visible ? 15 * root.uiScale : 30 * root.uiScale
                    }

                    GitStatusBadge {
                        visible: !root.remote
                        anchors.top: parent.top
                        anchors.right: parent.right
                        filePath: tile.filePath
                        uiScale: root.uiScale
                        selected: tile.selected
                    }
                }

                Text {
                    width: parent.width
                    text: tile.name
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignHCenter
                    color: tile.selected ? Ryoku.inkOnBone : Ryoku.ink
                    font.family: Ryoku.uiFont
                    font.pixelSize: 12 * root.uiScale
                    font.weight: Font.Medium
                }

                Text {
                    width: parent.width
                    text: tile.isDir ? "DIR" : tile.sizeText
                    horizontalAlignment: Text.AlignHCenter
                    color: tile.selected ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.uiScale
                }
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onClicked: function(event) {
                    root.paneActivated()
                    view.currentIndex = tile.index

                    if (event.button === Qt.RightButton) {
                        if (!tile.selected)
                            root.session.selectSingle(tile.index)

                        var point = tile.mapToItem(null, event.x, event.y)
                        root.contextRequested(point.x, point.y, tile.filePath, tile.isDir)
                        view.forceActiveFocus()
                        return
                    }

                    if (event.modifiers & Qt.ShiftModifier)
                        root.session.selectRange(tile.index)
                    else if (event.modifiers & Qt.ControlModifier)
                        root.session.toggleSelection(tile.index)
                    else
                        root.session.selectSingle(tile.index)

                    view.forceActiveFocus()
                }

                onDoubleClicked: function(event) {
                    root.paneActivated()
                    view.currentIndex = tile.index
                    root.session.activate(tile.index)
                    view.forceActiveFocus()
                }
            }
        }

        Keys.onPressed: function(event) {
            if (root.keyboardController.handleKey(
                    event,
                    view,
                    true,
                    view.keyboardColumns,
                    view.keyboardPageStep,
                    GridView.Contain)) {
                event.accepted = true
            }
        }
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
        visible: !root.remote && !root.previewOpen
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
            onTapped: {
                root.paneActivated()
                if (root.session && !root.remote)
                    root.session.previewVisible = true
                Qt.callLater(root.focusView)
            }
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
        restoreState()
        if (root.paneActive)
            Qt.callLater(root.focusView)
    }
}
