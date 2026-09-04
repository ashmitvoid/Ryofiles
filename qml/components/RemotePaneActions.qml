// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    required property var files
    property real uiScale: 1
    property bool paneActive: true

    property string pendingRenamePath: ""
    property bool pendingCreateFolder: false
    property string conflictJobId: ""
    property string message: ""
    property var ownedJobs: ({})
    property var cutJobBatches: ({})
    property var clearSelectionJobs: ({})

    readonly property bool active:
        root.session && root.session.remote && root.paneActive
    readonly property bool modalOpen:
        remoteMenu.visible || renameSheet.visible || conflictSheet.visible

    anchors.fill: parent
    visible: root.session && root.session.remote
    z: 700

    function selectedLocations() {
        return root.session ? root.session.selectedPaths : []
    }

    function rememberJob(jobId, clearSelectionOnSuccess) {
        if (!jobId || jobId === "")
            return false
        root.ownedJobs[jobId] = true
        if (clearSelectionOnSuccess)
            root.clearSelectionJobs[jobId] = true
        return true
    }

    function showMessage(text) {
        root.message = text
        messageTimer.restart()
    }

    function copySelection() {
        if (!root.active || root.session.selectionCount <= 0)
            return
        FileClipboard.copyLocations(root.selectedLocations())
    }

    function cutSelection() {
        if (!root.active || root.session.selectionCount <= 0)
            return
        FileClipboard.cutLocations(root.selectedLocations())
    }

    function pasteInto(destination) {
        if (!root.active || !FileClipboard.hasLocations)
            return

        var sources = FileClipboard.locations()
        if (!sources || sources.length === 0)
            return

        var ids = []
        for (var i = 0; i < sources.length; ++i) {
            var id = FileClipboard.cut
                ? RemoteOperations.moveFile(sources[i], destination)
                : RemoteOperations.copyFile(sources[i], destination)
            if (root.rememberJob(id, FileClipboard.cut))
                ids.push(id)
        }

        if (ids.length === 0) {
            root.showMessage("Could not start remote paste")
            return
        }

        if (FileClipboard.cut) {
            var batch = {
                remaining: ids.length,
                failed: false,
                locations: sources
            }
            for (var j = 0; j < ids.length; ++j)
                root.cutJobBatches[ids[j]] = batch
        }
    }

    function trashSelection() {
        if (!root.active || root.session.selectionCount <= 0)
            return

        var paths = root.selectedLocations()
        var started = false
        for (var i = 0; i < paths.length; ++i) {
            var id = RemoteOperations.trash(paths[i])
            if (root.rememberJob(id, true))
                started = true
        }
        if (!started)
            root.showMessage("Could not start remote Trash operation")
    }

    function beginRename() {
        if (!root.active || root.session.selectionCount !== 1)
            return
        root.pendingCreateFolder = false
        root.pendingRenamePath = root.session.selectedPath
        var slash = root.pendingRenamePath.lastIndexOf("/")
        var name = slash >= 0
            ? root.pendingRenamePath.substring(slash + 1)
            : root.pendingRenamePath
        renameSheet.open(name)
    }

    function beginNewFolder() {
        if (!root.active)
            return
        root.pendingRenamePath = ""
        root.pendingCreateFolder = true
        renameSheet.openFor("New Folder", "// NEW REMOTE FOLDER", "CREATE", false)
    }

    function openContext(sceneX, sceneY, path, isDirectory) {
        if (!root.active)
            return
        var localPoint = root.mapFromItem(null, sceneX, sceneY)
        remoteMenu.openAt(
            localPoint.x,
            localPoint.y,
            path,
            isDirectory,
            root.session.selectionCount,
            FileClipboard.hasLocations)
    }

    function openContextTarget() {
        if (!root.session || remoteMenu.targetPath === "")
            return
        if (remoteMenu.targetIsDirectory) {
            root.session.navigate(remoteMenu.targetPath)
            return
        }
        var idx = root.files ? root.files.indexOfPath(remoteMenu.targetPath) : -1
        if (idx >= 0)
            root.session.activate(idx)
    }

    function finishCutBatch(jobId, success) {
        var batch = root.cutJobBatches[jobId]
        if (batch === undefined)
            return

        delete root.cutJobBatches[jobId]
        batch.remaining = Math.max(0, batch.remaining - 1)
        if (!success)
            batch.failed = true

        if (batch.remaining === 0 && !batch.failed)
            FileClipboard.clearIfMatchesLocations(batch.locations, true)
    }

    Timer {
        id: messageTimer
        interval: 5000
        onTriggered: root.message = ""
    }

    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.active && !root.modalOpen && root.session.selectionCount > 0
        onActivated: root.copySelection()
    }
    Shortcut {
        sequence: "Ctrl+X"
        enabled: root.active && !root.modalOpen && root.session.selectionCount > 0
        onActivated: root.cutSelection()
    }
    Shortcut {
        sequence: "Ctrl+V"
        enabled: root.active && !root.modalOpen && FileClipboard.hasLocations
        onActivated: root.pasteInto(root.session.path)
    }
    Shortcut {
        sequence: "Delete"
        enabled: root.active && !root.modalOpen && root.session.selectionCount > 0
        onActivated: root.trashSelection()
    }
    Shortcut {
        sequence: "F2"
        enabled: root.active && !root.modalOpen && root.session.selectionCount === 1
        onActivated: root.beginRename()
    }
    Shortcut {
        sequence: "Ctrl+Shift+N"
        enabled: root.active && !root.modalOpen
        onActivated: root.beginNewFolder()
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.active && root.modalOpen
        onActivated: {
            remoteMenu.visible = false
            renameSheet.visible = false
            if (conflictSheet.visible && root.conflictJobId !== "")
                RemoteOperations.cancel(root.conflictJobId)
            conflictSheet.visible = false
        }
    }

    RemoteFileContextMenu {
        id: remoteMenu
        uiScale: root.uiScale

        onOpenRequested: root.openContextTarget()
        onCopyLocationRequested: FileClipboard.copyText(targetPath)
        onCopyRequested: root.copySelection()
        onCutRequested: root.cutSelection()
        onPasteIntoRequested: {
            if (targetIsDirectory && targetPath !== "")
                root.pasteInto(targetPath)
        }
        onRenameRequested: root.beginRename()
        onTrashRequested: root.trashSelection()
    }

    RenameSheet {
        id: renameSheet
        uiScale: root.uiScale

        onAccepted: function(newName) {
            visible = false
            var id = ""
            if (root.pendingCreateFolder)
                id = RemoteOperations.createFolder(root.session.path, newName)
            else if (root.pendingRenamePath !== "")
                id = RemoteOperations.rename(root.pendingRenamePath, newName)

            if (!root.rememberJob(id, true))
                root.showMessage(root.pendingCreateFolder
                    ? "Could not start remote folder creation"
                    : "Could not start remote rename")

            root.pendingRenamePath = ""
            root.pendingCreateFolder = false
        }

        onCancelled: {
            visible = false
            root.pendingRenamePath = ""
            root.pendingCreateFolder = false
        }
    }

    ConflictSheet {
        id: conflictSheet
        uiScale: root.uiScale
        allowApplyToAll: true

        onChoose: function(decision, applyToAll) {
            visible = false
            if (root.conflictJobId !== "")
                RemoteOperations.resolveConflict(root.conflictJobId, decision, applyToAll)
        }
    }

    OperationDrawer {
        anchors.right: parent.right
        anchors.rightMargin: 10 * root.uiScale
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10 * root.uiScale
        operations: RemoteOperations
        uiScale: root.uiScale
        z: 650
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 10 * root.uiScale
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10 * root.uiScale
        width: Math.min(parent.width - 20 * root.uiScale, messageText.implicitWidth + 20 * root.uiScale)
        height: message !== "" ? 32 * root.uiScale : 0
        visible: message !== ""
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.sun
        z: 660

        Text {
            id: messageText
            anchors.centerIn: parent
            text: root.message
            color: Ryoku.sun
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * root.uiScale
            elide: Text.ElideRight
        }
    }

    Connections {
        target: RemoteOperations

        function onConflictRaised(jobId, source, destination) {
            if (root.ownedJobs[jobId] === undefined)
                return
            root.conflictJobId = jobId
            conflictSheet.restoreMode = false
            conflictSheet.applyToAll = false
            conflictSheet.sourcePath = source
            conflictSheet.destinationPath = destination
            conflictSheet.visible = true
        }

        function onJobFinished(jobId, success) {
            if (root.ownedJobs[jobId] === undefined)
                return

            delete root.ownedJobs[jobId]
            root.finishCutBatch(jobId, success)

            if (root.conflictJobId === jobId) {
                conflictSheet.visible = false
                root.conflictJobId = ""
            }

            if (!success) {
                var error = RemoteOperations.errorFor(jobId)
                if (error !== "")
                    root.showMessage(error)
            } else {
                if (root.clearSelectionJobs[jobId] !== undefined)
                    root.session.clearSelection()
                root.session.refresh()
            }

            delete root.clearSelectionJobs[jobId]
        }
    }
}
