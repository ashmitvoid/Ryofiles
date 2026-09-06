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

    function focusView() {
        if (viewLoader.item && viewLoader.item.focusView)
            viewLoader.item.focusView()
    }

    function activateAndFocus() {
        root.activated(root.paneIndex)
        Qt.callLater(root.focusView)
    }

    onPaneActiveChanged: {
        if (root.paneActive)
            Qt.callLater(root.focusView)
    }

    Item {
        id: keyboardController
        visible: false
        width: 0
        height: 0

        property string typeAheadBuffer: ""
        property double typeAheadDeadlineMs: 0

        function currentOrInitial(view) {
            if (!root.files || root.files.count <= 0)
                return -1
            if (view.currentIndex >= 0 && view.currentIndex < root.files.count)
                return view.currentIndex
            if (root.session && root.session.selectedPath !== "") {
                var selected = root.files.indexOfPath(root.session.selectedPath)
                if (selected >= 0)
                    return selected
            }
            return 0
        }

        function moveTo(view, index, modifiers, positionMode) {
            if (!root.files || root.files.count <= 0 || !root.session)
                return false

            var target = Math.max(0, Math.min(root.files.count - 1, index))
            view.currentIndex = target
            view.positionViewAtIndex(target, positionMode)

            if (modifiers & Qt.ShiftModifier)
                root.session.selectRange(target)
            else if ((modifiers & Qt.ControlModifier) === 0)
                root.session.selectSingle(target)
            return true
        }

        function findPrefix(prefix, startIndex) {
            if (!root.files || root.files.count <= 0 || prefix === "")
                return -1
            var needle = prefix.toLowerCase()
            var count = root.files.count
            for (var offset = 0; offset < count; ++offset) {
                var index = (startIndex + offset) % count
                var name = root.files.nameAt(index)
                if (name && name.toLowerCase().indexOf(needle) === 0)
                    return index
            }
            return -1
        }

        function typeAhead(text, view, positionMode) {
            if (!text || text.length === 0 || !root.files || root.files.count <= 0)
                return false

            var now = Date.now()
            var nextBuffer = now <= typeAheadDeadlineMs
                ? typeAheadBuffer + text
                : text
            var current = currentOrInitial(view)
            var start = current >= 0 ? (current + 1) % root.files.count : 0
            var match = findPrefix(nextBuffer, start)

            if (match < 0 && nextBuffer.length > 1) {
                nextBuffer = text
                match = findPrefix(nextBuffer, start)
            }
            if (match < 0)
                return false

            typeAheadBuffer = nextBuffer
            typeAheadDeadlineMs = now + 900
            return moveTo(view, match, 0, positionMode)
        }

        function handleKey(event, view, gridMode, columns, pageStep, positionMode) {
            if (!root.paneActive || !root.session || !root.files || root.files.loading)
                return false

            var current = currentOrInitial(view)
            var modifiers = event.modifiers
            var target = current
            var handled = true

            if (event.key === Qt.Key_Up)
                target = current - Math.max(1, columns)
            else if (event.key === Qt.Key_Down)
                target = current + Math.max(1, columns)
            else if (event.key === Qt.Key_Left && gridMode)
                target = current - 1
            else if (event.key === Qt.Key_Right && gridMode)
                target = current + 1
            else if (event.key === Qt.Key_Home)
                target = 0
            else if (event.key === Qt.Key_End)
                target = root.files.count - 1
            else if (event.key === Qt.Key_PageUp)
                target = current - Math.max(1, pageStep)
            else if (event.key === Qt.Key_PageDown)
                target = current + Math.max(1, pageStep)
            else if (event.key === Qt.Key_Space && (modifiers & Qt.ControlModifier)) {
                if (current >= 0) {
                    view.currentIndex = current
                    view.positionViewAtIndex(current, positionMode)
                    root.session.toggleSelection(current)
                }
                return true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (current >= 0)
                    root.session.activate(current)
                return true
            } else if (event.key === Qt.Key_Escape) {
                root.session.clearSelection()
                return true
            } else {
                handled = false
            }

            if (handled) {
                if (current < 0)
                    target = 0
                return moveTo(view, target, modifiers, positionMode)
            }

            var text = event.text
            var blockedModifiers = Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier
            if (text && text.length > 0 && (modifiers & blockedModifiers) === 0
                    && text.charCodeAt(0) >= 32) {
                return typeAhead(text, view, positionMode)
            }
            return false
        }
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
        TapHandler { onTapped: root.activateAndFocus() }
    }

    Component {
        id: compactView
        FileListView {
            session: root.session
            files: root.files
            keyboardController: keyboardController
            uiScale: root.uiScale
            compact: true
            paneActive: root.paneActive
            onPaneActivated: root.activateAndFocus()
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
            keyboardController: keyboardController
            uiScale: root.uiScale
            paneActive: root.paneActive
            onPaneActivated: root.activateAndFocus()
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
            keyboardController: keyboardController
            uiScale: root.uiScale
            compact: false
            paneActive: root.paneActive
            onPaneActivated: root.activateAndFocus()
            onContextRequested: function(sceneX, sceneY, path, isDirectory) {
                root.routeContext(sceneX, sceneY, path, isDirectory)
            }
        }
    }

    Loader {
        id: viewLoader
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
        onLoaded: {
            if (root.paneActive)
                Qt.callLater(root.focusView)
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

        function markCutConflictSkipped(jobId) {
            var batch = cutJobBatches[jobId]
            if (batch !== undefined)
                batch.failed = true
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
                if (remoteToLocal.conflictJobId !== "") {
                    if (decision === 0)
                        remoteToLocal.markCutConflictSkipped(remoteToLocal.conflictJobId)
                    RemoteOperations.resolveConflict(remoteToLocal.conflictJobId, decision, false)
                }
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
