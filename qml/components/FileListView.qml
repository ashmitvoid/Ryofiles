// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    required property var files
    required property var keyboardController
    property real uiScale: 1
    property bool compact: false
    property bool paneActive: true

    property bool restoring: false
    readonly property bool remote: root.session && root.session.remote
    readonly property bool previewOpen: !root.remote && root.session && root.session.previewVisible
    readonly property real previewWidth: Math.min(
        360 * root.uiScale,
        Math.max(240 * root.uiScale, root.width * 0.36))
    readonly property real rowHeight: (root.compact ? 38 : 42) * root.uiScale
    readonly property real iconColumnWidth: 34 * root.uiScale
    readonly property real gitColumnWidth: root.remote ? 0 : 24 * root.uiScale
    readonly property real sizeColumnWidth: (root.compact ? 92 : 104) * root.uiScale
    readonly property real modifiedColumnWidth: root.compact ? 0 : 154 * root.uiScale

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
        if (["png", "jpg", "jpeg", "webp", "gif", "bmp", "svg", "avif"].indexOf(ext) >= 0)
            return "image"
        if (["mp4", "mkv", "webm", "mov", "avi", "m4v"].indexOf(ext) >= 0)
            return "video"
        if (["mp3", "flac", "wav", "ogg", "opus", "m4a", "aac"].indexOf(ext) >= 0)
            return "audio"
        if (["zip", "7z", "rar", "tar", "gz", "xz", "bz2", "zst"].indexOf(ext) >= 0)
            return "archive"
        if (["ttf", "otf", "woff", "woff2"].indexOf(ext) >= 0)
            return "font"
        if (ext === "pdf")
            return "pdf"
        if (["cpp", "cc", "c", "h", "hpp", "py", "rs", "js", "ts", "qml", "json", "toml", "yaml", "yml", "sh"].indexOf(ext) >= 0)
            return "code"
        return "file"
    }

    function kindLabel(kind) {
        if (kind === "folder") return "DIR"
        if (kind === "image") return "IMG"
        if (kind === "video") return "VID"
        if (kind === "audio") return "AUD"
        if (kind === "archive") return "ARC"
        if (kind === "font") return "Aa"
        if (kind === "pdf") return "PDF"
        if (kind === "code") return "<>"
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
        color: previewHover.hovered ? Ryoku.tint10 : "transparent"

        Text {
            anchors.centerIn: parent
            text: "Preview"
            color: previewHover.hovered ? Ryoku.ink : Ryoku.inkMuted
            font.family: Ryoku.uiFont
            font.pixelSize: 10 * root.uiScale
        }

        HoverHandler { id: previewHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
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

    Item {
        id: detailsHeader
        anchors.left: parent.left
        anchors.right: previewPanel.left
        anchors.top: filterBar.bottom
        height: root.compact ? 0 : 32 * root.uiScale
        visible: !root.compact
        clip: true

        Rectangle {
            anchors.fill: parent
            color: Ryoku.paperLift
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 10 * root.uiScale
            anchors.rightMargin: 10 * root.uiScale
            spacing: 8 * root.uiScale

            Item { width: root.iconColumnWidth; height: parent.height }
            Item { width: root.gitColumnWidth; height: parent.height; visible: !root.remote }

            Text {
                width: Math.max(100, parent.width
                    - root.iconColumnWidth - root.gitColumnWidth
                    - root.sizeColumnWidth - root.modifiedColumnWidth
                    - 42 * root.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                text: "Name"
                color: Ryoku.inkMuted
                font.family: Ryoku.uiFont
                font.pixelSize: 10 * root.uiScale
                font.weight: Font.Medium
            }

            Text {
                width: root.sizeColumnWidth
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                text: "Size"
                color: Ryoku.inkMuted
                font.family: Ryoku.uiFont
                font.pixelSize: 10 * root.uiScale
                font.weight: Font.Medium
            }

            Text {
                width: root.modifiedColumnWidth
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                text: "Modified"
                color: Ryoku.inkMuted
                font.family: Ryoku.uiFont
                font.pixelSize: 10 * root.uiScale
                font.weight: Font.Medium
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

    ListView {
        id: view
        anchors.left: parent.left
        anchors.top: detailsHeader.bottom
        anchors.bottom: parent.bottom
        anchors.right: previewPanel.left
        clip: true
        model: root.files
        spacing: 0
        currentIndex: -1
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        activeFocusOnTab: root.paneActive

        readonly property int keyboardPageStep: Math.max(
            1,
            Math.floor(height / Math.max(1, root.rowHeight)))

        onContentYChanged: {
            if (!root.restoring && root.session && root.files && !root.files.loading)
                root.session.scrollPosition = Math.max(0, contentY)
        }

        delegate: Item {
            id: row
            required property int index
            required property string name
            required property string filePath
            required property bool isDir
            required property string sizeText
            required property string modifiedText

            readonly property string kind: root.fileKind(name, isDir)
            readonly property bool selected: {
                var revision = root.session ? root.session.selectionRevision : 0
                return revision >= 0 && root.session && root.session.isSelectedPath(filePath)
            }
            readonly property bool currentItem:
                root.paneActive && view.activeFocus && view.currentIndex === row.index

            width: view.width
            height: root.rowHeight

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 4 * root.uiScale
                anchors.rightMargin: 4 * root.uiScale
                anchors.topMargin: 2 * root.uiScale
                anchors.bottomMargin: 2 * root.uiScale
                radius: 6 * root.uiScale
                color: row.selected
                    ? Ryoku.tint10
                    : (mouse.containsMouse ? Ryoku.tint5 : "transparent")
                border.width: row.currentItem ? 1 : 0
                border.color: Ryoku.lineStrong

                Behavior on color {
                    enabled: !Ryoku.reduceMotion
                    ColorAnimation { duration: Ryoku.duration(100) }
                }
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: 10 * root.uiScale
                anchors.rightMargin: 10 * root.uiScale
                spacing: 8 * root.uiScale

                Item {
                    width: root.iconColumnWidth
                    height: parent.height

                    Rectangle {
                        anchors.centerIn: parent
                        width: 27 * root.uiScale
                        height: 24 * root.uiScale
                        radius: 5 * root.uiScale
                        color: row.isDir ? Ryoku.tint10 : Ryoku.paperLift
                        border.width: 1
                        border.color: row.currentItem ? Ryoku.lineStrong : Ryoku.lineSoft

                        Rectangle {
                            visible: row.isDir
                            anchors.left: parent.left
                            anchors.leftMargin: 4 * root.uiScale
                            anchors.top: parent.top
                            anchors.topMargin: -3 * root.uiScale
                            width: 11 * root.uiScale
                            height: 5 * root.uiScale
                            radius: 2 * root.uiScale
                            color: Ryoku.paperLift
                            border.width: 1
                            border.color: Ryoku.lineSoft
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: !row.isDir
                            text: root.kindLabel(row.kind)
                            color: Ryoku.inkMuted
                            font.family: Ryoku.monoFont
                            font.pixelSize: (row.kind === "file" ? 6 : 7) * root.uiScale
                            font.weight: Font.Medium
                        }
                    }
                }

                Item {
                    width: root.gitColumnWidth
                    height: parent.height
                    visible: !root.remote
                    GitStatusBadge {
                        anchors.centerIn: parent
                        filePath: row.filePath
                        uiScale: root.uiScale
                        selected: false
                    }
                }

                Text {
                    width: Math.max(100, parent.width
                        - root.iconColumnWidth - root.gitColumnWidth
                        - root.sizeColumnWidth - root.modifiedColumnWidth
                        - 42 * root.uiScale)
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.name
                    elide: Text.ElideMiddle
                    color: Ryoku.ink
                    font.family: Ryoku.uiFont
                    font.pixelSize: (root.compact ? 11.5 : 12) * root.uiScale
                    font.weight: row.isDir ? Font.Medium : Font.Normal
                }

                Text {
                    width: root.sizeColumnWidth
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: row.isDir ? "Folder" : row.sizeText
                    color: Ryoku.inkMuted
                    font.family: root.compact ? Ryoku.uiFont : Ryoku.monoFont
                    font.pixelSize: (root.compact ? 10 : 9) * root.uiScale
                }

                Text {
                    visible: !root.compact
                    width: root.modifiedColumnWidth
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: row.modifiedText
                    color: Ryoku.inkMuted
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
                    view.currentIndex = row.index

                    if (event.button === Qt.RightButton) {
                        if (!row.selected)
                            root.session.selectSingle(row.index)
                        var point = row.mapToItem(null, event.x, event.y)
                        root.contextRequested(point.x, point.y, row.filePath, row.isDir)
                        view.forceActiveFocus()
                        return
                    }

                    if (event.modifiers & Qt.ShiftModifier)
                        root.session.selectRange(row.index)
                    else if (event.modifiers & Qt.ControlModifier)
                        root.session.toggleSelection(row.index)
                    else
                        root.session.selectSingle(row.index)
                    view.forceActiveFocus()
                }

                onDoubleClicked: function(event) {
                    root.paneActivated()
                    view.currentIndex = row.index
                    root.session.activate(row.index)
                    view.forceActiveFocus()
                }
            }
        }

        Keys.onPressed: function(event) {
            if (root.keyboardController.handleKey(
                    event,
                    view,
                    false,
                    1,
                    view.keyboardPageStep,
                    ListView.Contain)) {
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
