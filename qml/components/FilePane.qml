// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    property real uiScale: 1
    property bool paneActive: true
    property int paneIndex: 0

    readonly property var files: root.session ? root.session.model : null

    signal activated(int paneIndex)
    signal contextRequested(real sceneX, real sceneY, string path, bool isDirectory, int paneIndex)

    clip: true

    function routeContext(sceneX, sceneY, path, isDirectory) {
        if (root.session && root.session.remote) {
            remoteActions.openContext(sceneX, sceneY, path, isDirectory)
            return
        }
        root.contextRequested(sceneX, sceneY, path, isDirectory, root.paneIndex)
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: 6 * root.uiScale
        border.width: 1
        border.color: root.paneActive ? Ryoku.lineStrong : Ryoku.lineSoft
    }

    Rectangle {
        z: 90
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 2 * root.uiScale
        color: root.paneActive ? Ryoku.sun : "transparent"
    }

    Rectangle {
        id: paneMark
        z: 90
        anchors.top: parent.top
        anchors.topMargin: 6 * root.uiScale
        anchors.right: parent.right
        anchors.rightMargin: 8 * root.uiScale
        width: paneLabel.implicitWidth + 14 * root.uiScale
        height: 24 * root.uiScale
        radius: 6 * root.uiScale
        color: root.paneActive ? Ryoku.bone : Ryoku.paperLift
        border.width: 1
        border.color: root.paneActive ? Ryoku.bone : Ryoku.line

        Text {
            id: paneLabel
            anchors.centerIn: parent
            text: root.paneActive ? "ACTIVE" : "PANE " + (root.paneIndex + 1)
            color: root.paneActive ? Ryoku.inkOnBone : Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 8 * root.uiScale
            font.letterSpacing: 0.7
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.activated(root.paneIndex) }
    }

    Component {
        id: compactView
        FileListView {
            session: root.session
            files: root.files
            uiScale: root.uiScale
            compact: true
            paneActive: root.paneActive
            onPaneActivated: root.activated(root.paneIndex)
            onContextRequested: function(sceneX, sceneY, path, isDirectory) {
                root.routeContext(sceneX, sceneY, path, isDirectory)
            }
        }
    }

    Component {
        id: gridView
        FileGridView {
            session: root.session
            files: root.files
            uiScale: root.uiScale
            paneActive: root.paneActive
            onPaneActivated: root.activated(root.paneIndex)
            onContextRequested: function(sceneX, sceneY, path, isDirectory) {
                root.routeContext(sceneX, sceneY, path, isDirectory)
            }
        }
    }

    Component {
        id: detailsView
        FileListView {
            session: root.session
            files: root.files
            uiScale: root.uiScale
            compact: false
            paneActive: root.paneActive
            onPaneActivated: root.activated(root.paneIndex)
            onContextRequested: function(sceneX, sceneY, path, isDirectory) {
                root.routeContext(sceneX, sceneY, path, isDirectory)
            }
        }
    }

    Loader {
        anchors.fill: parent
        anchors.margins: 2 * root.uiScale
        active: root.session !== null && root.files !== null
        sourceComponent: {
            if (!root.session)
                return null
            if (root.session.viewMode === 0)
                return compactView
            if (root.session.viewMode === 1)
                return gridView
            return detailsView
        }
    }

    Text {
        anchors.centerIn: parent
        visible: root.files && !root.files.loading && root.files.count === 0
        text: "// EMPTY_\nThis folder has no visible items"
        color: Ryoku.inkMuted
        horizontalAlignment: Text.AlignHCenter
        font.family: Ryoku.monoFont
        font.pixelSize: 11 * root.uiScale
        lineHeight: 1.7
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: 8 * root.uiScale
        anchors.topMargin: 8 * root.uiScale
        width: loadingLabel.implicitWidth + 18 * root.uiScale
        height: 26 * root.uiScale
        visible: root.files && root.files.loading
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.line

        Text {
            id: loadingLabel
            anchors.centerIn: parent
            text: "READING…"
            color: Ryoku.inkMuted
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * root.uiScale
            font.letterSpacing: 1.1
        }
    }

    RemotePaneActions {
        id: remoteActions
        anchors.fill: parent
        session: root.session
        files: root.files
        uiScale: root.uiScale
        paneActive: root.paneActive
    }

    Item {
        id: remoteToLocal
        anchors.fill: parent
        z: 720
        visible: root.session && !root.session.remote

        property var ownedJobs: ({})
        property var cutJobBatches: ({})
        property string conflictJobId: ""
        property string message: ""
        property int ownedCount: 0

        readonly property bool available:
            visible && root.paneActive && FileClipboard.hasLocations && !FileClipboard.hasFiles

        function rememberJob(jobId) {
            if (!jobId || jobId === "")
                return false
            ownedJobs[jobId] = true
            ownedCount += 1
            return true
        }

        function pasteRemoteClipboard() {
            if (!available || !root.session)
                return

            var sources = FileClipboard.locations()
            if (!sources || sources.length === 0)
                return

            var ids = []
            for (var i = 0; i < sources.length; ++i) {
                var id = FileClipboard.cut
                    ? RemoteOperations.moveFile(sources[i], root.session.path)
                    : RemoteOperations.copyFile(sources[i], root.session.path)
                if (rememberJob(id))
                    ids.push(id)
            }

            if (ids.length === 0) {
                message = "Could not start remote transfer"
                messageTimer.restart()
                return
            }

            if (FileClipboard.cut) {
                var batch = {
                    remaining: ids.length,
                    failed: false,
                    locations: sources
                }
                for (var j = 0; j < ids.length; ++j)
                    cutJobBatches[ids[j]] = batch
            }
        }

        function finishCutBatch(jobId, success) {
            var batch = cutJobBatches[jobId]
            if (batch === undefined)
                return

            delete cutJobBatches[jobId]
            batch.remaining = Math.max(0, batch.remaining - 1)
            if (!success)
                batch.failed = true
            if (batch.remaining === 0 && !batch.failed)
                FileClipboard.clearIfMatchesLocations(batch.locations, true)
        }

        Shortcut {
            sequence: "Ctrl+Shift+V"
            enabled: remoteToLocal.available && remoteToLocal.conflictJobId === ""
            onActivated: remoteToLocal.pasteRemoteClipboard()
        }

        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 36 * root.uiScale
            anchors.right: parent.right
            anchors.rightMargin: 8 * root.uiScale
            width: pasteLabel.implicitWidth + 18 * root.uiScale
            height: 28 * root.uiScale
            visible: remoteToLocal.available
            radius: 6 * root.uiScale
            color: pasteHover.hovered ? Ryoku.tint10 : Ryoku.paperLift
            border.width: 1
            border.color: Ryoku.lineStrong

            Text {
                id: pasteLabel
                anchors.centerIn: parent
                text: FileClipboard.cut ? "MOVE REMOTE HERE" : "PASTE REMOTE HERE"
                color: Ryoku.inkDim
                font.family: Ryoku.uiFont
                font.pixelSize: 9 * root.uiScale
                font.weight: Font.Medium
                font.letterSpacing: 0.8
            }

            HoverHandler { id: pasteHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: remoteToLocal.pasteRemoteClipboard() }
        }

        ConflictSheet {
            id: remoteLocalConflict
            uiScale: root.uiScale
            allowApplyToAll: false
            allowReplace: false

            onChoose: function(decision, applyToAll) {
                visible = false
                if (remoteToLocal.conflictJobId !== "")
                    RemoteOperations.resolveConflict(remoteToLocal.conflictJobId, decision, false)
            }
        }

        OperationDrawer {
            anchors.right: parent.right
            anchors.rightMargin: 10 * root.uiScale
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10 * root.uiScale
            operations: RemoteOperations
            uiScale: root.uiScale
            visible: remoteToLocal.ownedCount > 0
            z: 650
        }

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 10 * root.uiScale
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10 * root.uiScale
            width: Math.min(parent.width - 20 * root.uiScale, localMessage.implicitWidth + 20 * root.uiScale)
            height: remoteToLocal.message !== "" ? 32 * root.uiScale : 0
            visible: remoteToLocal.message !== ""
            radius: 6 * root.uiScale
            color: Ryoku.paperLift
            border.width: 1
            border.color: Ryoku.sun

            Text {
                id: localMessage
                anchors.centerIn: parent
                text: remoteToLocal.message
                color: Ryoku.sun
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
                elide: Text.ElideRight
            }
        }

        Timer {
            id: messageTimer
            interval: 5000
            onTriggered: remoteToLocal.message = ""
        }

        Connections {
            target: RemoteOperations

            function onConflictRaised(jobId, source, destination) {
                if (remoteToLocal.ownedJobs[jobId] === undefined)
                    return
                remoteToLocal.conflictJobId = jobId
                remoteLocalConflict.restoreMode = false
                remoteLocalConflict.applyToAll = false
                remoteLocalConflict.sourcePath = source
                remoteLocalConflict.destinationPath = destination
                remoteLocalConflict.visible = true
            }

            function onJobFinished(jobId, success) {
                if (remoteToLocal.ownedJobs[jobId] === undefined)
                    return

                delete remoteToLocal.ownedJobs[jobId]
                remoteToLocal.ownedCount = Math.max(0, remoteToLocal.ownedCount - 1)
                remoteToLocal.finishCutBatch(jobId, success)

                if (remoteToLocal.conflictJobId === jobId) {
                    remoteLocalConflict.visible = false
                    remoteToLocal.conflictJobId = ""
                }

                if (!success) {
                    var error = RemoteOperations.errorFor(jobId)
                    if (error !== "") {
                        remoteToLocal.message = error
                        messageTimer.restart()
                    }
                } else if (root.session && !root.session.remote) {
                    root.session.refresh()
                }
            }
        }
    }
}
