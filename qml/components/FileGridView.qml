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
        360 * root.uiScale,
        Math.max(240 * root.uiScale, root.width * 0.36))

    signal contextRequested(real sceneX, real sceneY, string path, bool isDirectory)
    signal paneActivated()

    clip: true

    function focusView() {
        if (!root.paneActive)
            return
        view.forceActiveFocus()
    }

    function fileKind(name, isDir) {
        if (isDir)
            return "folder"
        var dot = name.lastIndexOf(".")
        var ext = dot >= 0 ? name.substring(dot + 1).toLowerCase() : ""
        if (["png", "jpg", "jpeg", "webp", "gif", "bmp", "svg", "avif"].indexOf(ext) >= 0) return "IMG"
        if (["mp4", "mkv", "webm", "mov", "avi", "m4v"].indexOf(ext) >= 0) return "VID"
        if (["mp3", "flac", "wav", "ogg", "opus", "m4a", "aac"].indexOf(ext) >= 0) return "AUD"
        if (["zip", "7z", "rar", "tar", "gz", "xz", "bz2", "zst"].indexOf(ext) >= 0) return "ARC"
        if (["ttf", "otf", "woff", "woff2"].indexOf(ext) >= 0) return "Aa"
        if (ext === "pdf") return "PDF"
        if (["cpp", "cc", "c", "h", "hpp", "py", "rs", "js", "ts", "qml", "json", "toml", "yaml", "yml", "sh"].indexOf(ext) >= 0) return "<>"
        return "FILE"
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

    onSessionChanged: { restoring = true; restoreState() }
    onFilesChanged: { restoring = true; restoreState() }
    onPaneActiveChanged: if (root.paneActive) Qt.callLater(root.focusView)

    Shortcut {
        sequence: "Ctrl+Shift+P"
        enabled: root.paneActive && root.session !== null && !root.remote
        onActivated: root.session.previewVisible = !root.session.previewVisible
    }

    Item {
        id: paneHeader
        anchors.left: parent.left
        anchors.right: previewPanel.left
        anchors.top: parent.top
        height: 34 * root.uiScale

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 10 * root.uiScale
            anchors.right: previewToggle.left
            anchors.rightMargin: 8 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            text: root.session ? root.session.title : "Files"
            elide: Text.ElideRight
            color: root.paneActive ? Ryoku.ink : Ryoku.inkMuted
            font.family: Ryoku.uiFont
            font.pixelSize: 11 * root.uiScale
            font.weight: Font.Medium

            Behavior on color {
                enabled: !Ryoku.reduceMotion
                ColorAnimation { duration: Ryoku.duration(110) }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Ryoku.lineSoft
        }
    }

    Rectangle {
        id: previewToggle
        z: 50
        anchors.top: parent.top
        anchors.topMargin: 4 * root.uiScale
        anchors.right: previewPanel.left
        anchors.rightMargin: 78 * root.uiScale
        width: 66 * root.uiScale
        height: 26 * root.uiScale
        visible: !root.remote && !root.previewOpen
        radius: 6 * root.uiScale
        color: previewTap.pressed
            ? Ryoku.tint10
            : (previewHover.hovered ? Ryoku.tint5 : "transparent")

        Behavior on color {
            enabled: !Ryoku.reduceMotion
            ColorAnimation { duration: Ryoku.duration(90) }
        }

        Text {
            anchors.centerIn: parent
            text: "Preview"
            color: previewHover.hovered || previewTap.pressed ? Ryoku.ink : Ryoku.inkMuted
            font.family: Ryoku.uiFont
            font.pixelSize: 10 * root.uiScale
        }
        HoverHandler { id: previewHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            id: previewTap
            onTapped: {
                root.paneActivated()
                if (root.session && !root.remote)
                    root.session.previewVisible = true
                Qt.callLater(root.focusView)
            }
        }
    }

    FolderFilterBar {
        id: filterBar
        z: 60
        anchors.left: parent.left
        anchors.right: previewPanel.left
        anchors.top: paneHeader.bottom
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
        cellWidth: 164 * root.uiScale
        cellHeight: 138 * root.uiScale
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

        delegate: Item {
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

            width: view.cellWidth
            height: view.cellHeight
            opacity: mouse.pressed ? 0.82 : 1.0

            Behavior on opacity {
                enabled: !Ryoku.reduceMotion
                NumberAnimation { duration: Ryoku.duration(70) }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 5 * root.uiScale
                radius: 9 * root.uiScale
                color: tile.selected
                    ? Ryoku.tint10
                    : (mouse.containsMouse ? Ryoku.tint5 : "transparent")
                border.width: tile.currentItem ? 1 : 0
                border.color: Ryoku.lineStrong

                Behavior on color {
                    enabled: !Ryoku.reduceMotion
                    ColorAnimation { duration: Ryoku.duration(110) }
                }
            }

            Column {
                anchors.fill: parent
                anchors.leftMargin: 13 * root.uiScale
                anchors.rightMargin: 13 * root.uiScale
                anchors.topMargin: 13 * root.uiScale
                anchors.bottomMargin: 10 * root.uiScale
                spacing: 7 * root.uiScale

                Item {
                    width: parent.width
                    height: 70 * root.uiScale
                    clip: true

                    Image {
                        id: thumbnail
                        anchors.centerIn: parent
                        width: Math.min(parent.width, 112 * root.uiScale)
                        height: parent.height
                        visible: !root.remote && tile.thumbnailCandidate
                        source: visible
                            ? Thumbnails.urlForPath(
                                tile.filePath,
                                Math.max(64, Math.round(144 * root.uiScale)),
                                0)
                            : ""
                        sourceSize.width: Math.max(64, Math.round(144 * root.uiScale))
                        sourceSize.height: Math.max(64, Math.round(144 * root.uiScale))
                        fillMode: Image.PreserveAspectFit
                        cache: true
                        asynchronous: true
                        smooth: true
                        opacity: status === Image.Ready ? 1.0 : 0.0

                        Behavior on opacity {
                            enabled: !Ryoku.reduceMotion
                            NumberAnimation { duration: Ryoku.duration(130); easing.type: Easing.OutCubic }
                        }
                    }

                    Item {
                        anchors.centerIn: parent
                        width: 64 * root.uiScale
                        height: 54 * root.uiScale
                        visible: !thumbnail.visible || thumbnail.status !== Image.Ready
                        opacity: thumbnail.status === Image.Ready ? 0.0 : 1.0

                        Behavior on opacity {
                            enabled: !Ryoku.reduceMotion
                            NumberAnimation { duration: Ryoku.duration(100) }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: tile.isDir ? 44 * root.uiScale : 52 * root.uiScale
                            radius: 8 * root.uiScale
                            color: Ryoku.paperLift
                            border.width: 1
                            border.color: tile.currentItem ? Ryoku.lineStrong : Ryoku.lineSoft
                        }

                        Rectangle {
                            visible: tile.isDir
                            anchors.left: parent.left
                            anchors.leftMargin: 6 * root.uiScale
                            anchors.top: parent.top
                            width: 26 * root.uiScale
                            height: 11 * root.uiScale
                            radius: 3 * root.uiScale
                            color: Ryoku.paperLift
                            border.width: 1
                            border.color: Ryoku.lineSoft
                        }

                        Text {
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: tile.isDir ? 5 * root.uiScale : 0
                            text: tile.isDir
                                ? "Folder"
                                : (thumbnail.visible && thumbnail.status === Image.Loading
                                    ? "…" : root.fileKind(tile.name, false))
                            color: Ryoku.inkMuted
                            font.family: tile.isDir ? Ryoku.uiFont : Ryoku.monoFont
                            font.pixelSize: tile.isDir ? 9 * root.uiScale : 8 * root.uiScale
                            font.weight: Font.Medium
                        }
                    }

                    GitStatusBadge {
                        visible: !root.remote
                        anchors.top: parent.top
                        anchors.right: parent.right
                        filePath: tile.filePath
                        uiScale: root.uiScale
                        selected: false
                    }
                }

                Text {
                    width: parent.width
                    text: tile.name
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignHCenter
                    color: Ryoku.ink
                    font.family: Ryoku.uiFont
                    font.pixelSize: 11 * root.uiScale
                    font.weight: tile.isDir ? Font.Medium : Font.Normal
                }

                Text {
                    width: parent.width
                    text: tile.isDir ? "Folder" : tile.sizeText
                    horizontalAlignment: Text.AlignHCenter
                    color: Ryoku.inkMuted
                    font.family: Ryoku.uiFont
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
        visible: width > 0.5
        opacity: root.previewOpen ? 1.0 : 0.0
        session: root.session
        desktop: Desktop
        thumbnails: Thumbnails
        uiScale: root.uiScale

        Behavior on width {
            enabled: !Ryoku.reduceMotion
            NumberAnimation { duration: Ryoku.duration(150); easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            enabled: !Ryoku.reduceMotion
            NumberAnimation { duration: Ryoku.duration(110) }
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
