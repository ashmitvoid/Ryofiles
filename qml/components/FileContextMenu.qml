// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    property real uiScale: 1
    property var operationManager: null
    property string targetPath: ""
    property bool targetIsDirectory: false
    property int selectionCount: 0
    property var selectedPaths: []
    property bool clipboardHasFiles: false
    property string gitCode: ""
    property bool gitStageAvailable: false
    property bool gitUnstageAvailable: false
    property bool gitWorktreeDiffAvailable: false
    property bool gitStagedDiffAvailable: false
    property string pendingGitOperation: ""
    property string gitMessage: ""
    property bool diffMode: false
    property bool ryokuInstallAvailable: false
    property bool ryokuCompressAvailable: false
    property string ryokuMessage: ""
    property bool archiveExtractAvailable: false

    property bool deleteMode: false
    property var deletePaths: []
    property var deleteSession: null
    property string deleteJobId: ""
    property string deleteMessage: ""
    readonly property bool deleteBusy: root.deleteJobId !== ""

    signal openRequested()
    signal openNewTabRequested()
    signal openWithRequested()
    signal copyRequested()
    signal cutRequested()
    signal pasteIntoRequested()
    signal duplicateRequested()
    signal extractHereRequested()
    signal renameRequested()
    signal trashRequested()
    signal propertiesRequested()

    visible: false
    anchors.fill: parent
    z: 900

    OperationManager {
        id: permanentOperations
    }

    function targetInsideRepository(path) {
        var repo = GitStatus.rootPath
        if (!repo || repo === "" || !path || path === "")
            return false
        if (repo === "/")
            return path.indexOf("/") === 0
        return path === repo || path.indexOf(repo + "/") === 0
    }

    function selectionSnapshot() {
        var window = root.Window.window
        if (!window || !window.session || window.session.remote)
            return []

        var paths = window.session.selectedPaths
        var snapshot = []
        if (!paths)
            return snapshot

        for (var i = 0; i < paths.length; ++i)
            snapshot.push(paths[i])
        return snapshot
    }

    function refreshGitCapabilities() {
        root.gitCode = ""
        root.gitStageAvailable = false
        root.gitUnstageAvailable = false
        root.gitWorktreeDiffAvailable = false
        root.gitStagedDiffAvailable = false

        if (root.selectionCount !== 1 || GitActions.busy || !GitStatus.repository
                || !root.targetInsideRepository(root.targetPath))
            return

        root.gitCode = GitStatus.statusForPath(root.targetPath)
        root.gitStageAvailable = root.gitCode === "modified"
            || root.gitCode === "untracked"
            || root.gitCode === "mixed"
            || root.gitCode === "conflict"
        root.gitUnstageAvailable = root.gitCode === "staged"
            || root.gitCode === "mixed"
            || root.gitCode === "conflict"

        if (!root.targetIsDirectory) {
            root.gitWorktreeDiffAvailable = root.gitCode === "modified"
                || root.gitCode === "mixed"
                || root.gitCode === "conflict"
            root.gitStagedDiffAvailable = root.gitCode === "staged"
                || root.gitCode === "mixed"
                || root.gitCode === "conflict"
        }
    }

    function refreshRyokuCapabilities() {
        var paths = root.selectedPaths
        var hasSelection = paths && paths.length > 0

        root.ryokuInstallAvailable = hasSelection
            && Desktop.canRyokuInstall(paths)
        root.ryokuCompressAvailable = hasSelection
            && Desktop.canRyokuCompress(paths)
    }

    function refreshArchiveCapabilities() {
        root.archiveExtractAvailable = root.operationManager !== null
            && root.selectionCount === 1
            && !root.targetIsDirectory
            && root.targetPath !== ""
            && root.operationManager.canExtractArchive(root.targetPath)
    }

    function openAt(sceneX, sceneY, path, isDirectory, selectedCount, hasClipboard) {
        if (root.deleteBusy)
            return
        if (diffPanel.visible)
            diffPanel.close()
        root.diffMode = false
        root.deleteMode = false
        root.deletePaths = []
        root.deleteSession = null
        root.deleteMessage = ""
        if (root.pendingGitOperation === "")
            root.gitMessage = ""
        if (!Desktop.ryokuActionBusy)
            root.ryokuMessage = ""
        root.targetPath = path
        root.targetIsDirectory = isDirectory
        root.selectionCount = selectedCount
        root.selectedPaths = root.selectionSnapshot()
        root.clipboardHasFiles = hasClipboard
        root.refreshGitCapabilities()
        root.refreshRyokuCapabilities()
        root.refreshArchiveCapabilities()
        root.visible = true

        Qt.callLater(function() {
            menu.x = Math.max(
                8 * root.uiScale,
                Math.min(sceneX, root.width - menu.width - 8 * root.uiScale))
            menu.y = Math.max(
                8 * root.uiScale,
                Math.min(sceneY, root.height - menu.height - 8 * root.uiScale))
        })
    }

    function startGitMutation(stage) {
        if (root.pendingGitOperation !== "" || GitActions.busy || !GitStatus.repository)
            return

        var id = stage
            ? GitActions.stage(GitStatus.rootPath, [root.targetPath])
            : GitActions.unstage(GitStatus.rootPath, [root.targetPath])

        if (id === "") {
            root.gitMessage = GitActions.error !== ""
                ? GitActions.error
                : (stage ? "Could not start Git stage" : "Could not start Git unstage")
            return
        }

        root.pendingGitOperation = id
        root.gitMessage = stage ? "// STAGING…" : "// UNSTAGING…"
    }

    function startRyokuAction(install) {
        var paths = root.selectedPaths
        if (Desktop.ryokuActionBusy || !paths || paths.length === 0)
            return

        var started = install
            ? Desktop.installWithRyoku(paths)
            : Desktop.compressWithRyoku(paths)

        if (!started) {
            root.ryokuMessage = Desktop.ryokuActionError !== ""
                ? Desktop.ryokuActionError
                : (install
                    ? "Could not start Ryoku install"
                    : "Could not start Ryoku compression")
            root.refreshRyokuCapabilities()
            return
        }

        root.ryokuMessage = install
            ? "// INSTALLING WITH RYOKU…"
            : "// COMPRESSING WITH RYOKU…"
        root.refreshRyokuCapabilities()
    }

    function openGitDiff(staged) {
        root.gitMessage = ""
        root.diffMode = true
        if (!diffPanel.openFor(root.targetPath, staged)) {
            root.diffMode = false
            root.gitMessage = GitActions.error !== ""
                ? GitActions.error
                : "Could not start Git diff"
        }
    }

    function leafName(path) {
        if (!path || path === "")
            return ""
        var slash = path.lastIndexOf("/")
        return slash >= 0 ? path.substring(slash + 1) : path
    }

    function deletePreviewText() {
        var paths = root.deletePaths
        if (!paths || paths.length === 0)
            return ""

        var names = []
        var limit = Math.min(3, paths.length)
        for (var i = 0; i < limit; ++i)
            names.push(root.leafName(paths[i]))
        if (paths.length > limit)
            names.push("+" + (paths.length - limit) + " more")
        return names.join("\n")
    }

    function beginPermanentDelete(paths) {
        if (root.deleteBusy)
            return

        var window = root.Window.window
        if (!window || window.modalOpen && !root.visible
                || window.trashMode || !window.session || window.session.remote)
            return

        var snapshot = []
        if (paths) {
            for (var i = 0; i < paths.length; ++i) {
                if (paths[i] && paths[i] !== "")
                    snapshot.push(paths[i])
            }
        }
        if (snapshot.length === 0)
            return

        root.diffMode = false
        root.deletePaths = snapshot
        root.deleteSession = window.session
        root.deleteMessage = ""
        root.deleteMode = true
        root.visible = true
    }

    function cancelPermanentDelete() {
        if (root.deleteBusy)
            return
        root.deleteMode = false
        root.deletePaths = []
        root.deleteSession = null
        root.deleteMessage = ""
        root.visible = false
    }

    function confirmPermanentDelete() {
        if (!root.deleteMode || root.deleteBusy
                || !root.deletePaths || root.deletePaths.length === 0)
            return

        var id = permanentOperations.removePermanently(root.deletePaths)
        if (id === "") {
            root.deletePaths = []
            root.deleteMessage = "Permanent delete was rejected before it started"
            return
        }

        root.deleteJobId = id
        root.deleteMessage = "// DELETING PERMANENTLY…"
    }

    onVisibleChanged: {
        if (!visible && root.deleteBusy) {
            Qt.callLater(function() {
                if (root.deleteBusy)
                    root.visible = true
            })
            return
        }

        if (!visible && root.diffMode) {
            root.diffMode = false
            diffPanel.close()
        }
        if (!visible && root.deleteMode) {
            root.deleteMode = false
            root.deletePaths = []
            root.deleteSession = null
            root.deleteMessage = ""
        }
        if (!visible && !Desktop.ryokuActionBusy)
            root.selectedPaths = []
    }

    Shortcut {
        sequence: "Shift+Delete"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: {
            var window = root.Window.window
            return !root.visible
                && !root.deleteBusy
                && window !== null
                && !window.modalOpen
                && !window.trashMode
                && window.session !== null
                && !window.session.remote
                && window.session.selectionCount > 0
        }
        onActivated: root.beginPermanentDelete(root.selectionSnapshot())
    }

    MouseArea {
        anchors.fill: parent
        visible: !root.diffMode
        acceptedButtons: Qt.AllButtons
        onPressed: {
            if (root.deleteMode) {
                if (!root.deleteBusy)
                    root.cancelPermanentDelete()
                return
            }
            if (root.pendingGitOperation === "")
                root.visible = false
        }
    }

    Rectangle {
        id: menu
        visible: !root.diffMode && !root.deleteMode
        width: 250 * root.uiScale
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
                    { label: "OPEN", action: "open", enabled: root.selectionCount === 1, visible: true },
                    { label: "OPEN IN NEW TAB", action: "newtab", enabled: root.selectionCount === 1, visible: root.targetIsDirectory },
                    { label: "OPEN WITH…", action: "openwith", enabled: root.selectionCount === 1, visible: !root.targetIsDirectory },
                    { label: "OPEN TERMINAL", action: "terminal", enabled: root.selectionCount === 1 && root.targetPath !== "", visible: root.selectionCount === 1 },
                    { label: "COPY PATH", action: "copypath", enabled: root.selectionCount === 1 && root.targetPath !== "", visible: root.selectionCount === 1 },
                    { label: "COPY", action: "copy", enabled: root.selectionCount > 0, visible: true },
                    { label: "CUT", action: "cut", enabled: root.selectionCount > 0, visible: true },
                    { label: "PASTE INTO", action: "pasteinto", enabled: root.clipboardHasFiles, visible: root.targetIsDirectory && root.selectionCount === 1 },
                    { label: "DUPLICATE", action: "duplicate", enabled: root.selectionCount > 0, visible: true },
                    { label: "EXTRACT HERE", action: "extracthere", enabled: root.archiveExtractAvailable, visible: root.archiveExtractAvailable },
                    { label: "RYOKU · INSTALL", action: "ryokuinstall", enabled: root.ryokuInstallAvailable && !Desktop.ryokuActionBusy, visible: root.ryokuInstallAvailable },
                    { label: "RYOKU · COMPRESS", action: "ryokucompress", enabled: root.ryokuCompressAvailable && !Desktop.ryokuActionBusy, visible: root.ryokuCompressAvailable },
                    { label: "RENAME", action: "rename", enabled: root.selectionCount === 1, visible: true },
                    { label: "GIT · STAGE", action: "gitstage", enabled: root.gitStageAvailable && !GitActions.busy, visible: root.gitStageAvailable },
                    { label: "GIT · UNSTAGE", action: "gitunstage", enabled: root.gitUnstageAvailable && !GitActions.busy, visible: root.gitUnstageAvailable },
                    { label: "GIT · DIFF WORKTREE", action: "gitdiff", enabled: root.gitWorktreeDiffAvailable && !GitActions.busy, visible: root.gitWorktreeDiffAvailable },
                    { label: "GIT · DIFF STAGED", action: "gitdiffstaged", enabled: root.gitStagedDiffAvailable && !GitActions.busy, visible: root.gitStagedDiffAvailable },
                    { label: "MOVE TO TRASH", action: "trash", enabled: root.selectionCount > 0, visible: true },
                    { label: "DELETE PERMANENTLY…", action: "permadelete", enabled: root.selectionCount > 0 && !root.deleteBusy, visible: true },
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
                                || actionRow.modelData.action === "permadelete"
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
                            var action = actionRow.modelData.action

                            if (action === "gitstage") {
                                root.startGitMutation(true)
                                return
                            }
                            if (action === "gitunstage") {
                                root.startGitMutation(false)
                                return
                            }
                            if (action === "gitdiff") {
                                root.openGitDiff(false)
                                return
                            }
                            if (action === "gitdiffstaged") {
                                root.openGitDiff(true)
                                return
                            }
                            if (action === "ryokuinstall") {
                                root.startRyokuAction(true)
                                return
                            }
                            if (action === "ryokucompress") {
                                root.startRyokuAction(false)
                                return
                            }
                            if (action === "permadelete") {
                                root.beginPermanentDelete(root.selectedPaths)
                                return
                            }
                            if (action === "terminal") {
                                if (GitActions.openTerminal(root.targetPath))
                                    root.visible = false
                                else
                                    root.gitMessage = "Could not open the configured terminal"
                                return
                            }
                            if (action === "copypath") {
                                FileClipboard.copyText(root.targetPath)
                                root.visible = false
                                return
                            }

                            root.visible = false
                            switch (action) {
                            case "open": root.openRequested(); break
                            case "newtab": root.openNewTabRequested(); break
                            case "openwith": root.openWithRequested(); break
                            case "copy": root.copyRequested(); break
                            case "cut": root.cutRequested(); break
                            case "pasteinto": root.pasteIntoRequested(); break
                            case "duplicate": root.duplicateRequested(); break
                            case "extracthere": root.extractHereRequested(); break
                            case "rename": root.renameRequested(); break
                            case "trash": root.trashRequested(); break
                            case "properties": root.propertiesRequested(); break
                            }
                        }
                    }
                }
            }

            Item {
                width: items.width
                height: visible ? Math.max(32 * root.uiScale, statusMessage.implicitHeight + 12 * root.uiScale) : 0
                visible: root.pendingGitOperation !== ""
                    || root.gitMessage !== ""
                    || root.ryokuMessage !== ""

                Text {
                    id: statusMessage
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 8 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.ryokuMessage !== "" ? root.ryokuMessage : root.gitMessage
                    wrapMode: Text.Wrap
                    color: root.pendingGitOperation !== "" || Desktop.ryokuActionBusy
                        ? Ryoku.inkMuted
                        : Ryoku.sun
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.uiScale
                    lineHeight: 1.2
                }
            }
        }
    }

    Rectangle {
        id: deleteSheet
        visible: root.deleteMode
        anchors.centerIn: parent
        width: Math.min(460 * root.uiScale, root.width - 40 * root.uiScale)
        height: deleteColumn.implicitHeight + 32 * root.uiScale
        radius: 8 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Column {
            id: deleteColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16 * root.uiScale
            spacing: 12 * root.uiScale

            Text {
                width: parent.width
                text: "DELETE PERMANENTLY?"
                color: Ryoku.sun
                font.family: Ryoku.uiFont
                font.pixelSize: 15 * root.uiScale
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
            }

            Text {
                width: parent.width
                text: root.deletePaths.length + (root.deletePaths.length === 1 ? " ITEM" : " ITEMS")
                color: Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
                font.letterSpacing: 0.8
            }

            Text {
                width: parent.width
                visible: root.deletePaths.length > 0
                text: root.deletePreviewText()
                color: Ryoku.ink
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                font.family: Ryoku.monoFont
                font.pixelSize: 10 * root.uiScale
                lineHeight: 1.25
            }

            Rectangle {
                width: parent.width
                height: warningText.implicitHeight + 18 * root.uiScale
                radius: 6 * root.uiScale
                color: Ryoku.tint5
                border.width: 1
                border.color: Ryoku.sun

                Text {
                    id: warningText
                    anchors.fill: parent
                    anchors.margins: 9 * root.uiScale
                    text: "This bypasses Trash and cannot be undone."
                    color: Ryoku.sun
                    wrapMode: Text.Wrap
                    font.family: Ryoku.uiFont
                    font.pixelSize: 10 * root.uiScale
                    font.weight: Font.Medium
                }
            }

            Text {
                width: parent.width
                visible: root.deleteMessage !== ""
                text: root.deleteMessage
                color: root.deleteBusy ? Ryoku.inkMuted : Ryoku.sun
                wrapMode: Text.Wrap
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
            }

            Row {
                spacing: 8 * root.uiScale

                Rectangle {
                    width: cancelDeleteLabel.implicitWidth + 24 * root.uiScale
                    height: 32 * root.uiScale
                    radius: 6 * root.uiScale
                    visible: !root.deleteBusy
                    color: cancelDeleteHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        id: cancelDeleteLabel
                        anchors.centerIn: parent
                        text: root.deletePaths.length > 0 ? "CANCEL" : "CLOSE"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.Medium
                        font.letterSpacing: 0.8
                    }

                    HoverHandler {
                        id: cancelDeleteHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: root.cancelPermanentDelete()
                    }
                }

                Rectangle {
                    width: deleteConfirmLabel.implicitWidth + 24 * root.uiScale
                    height: 32 * root.uiScale
                    radius: 6 * root.uiScale
                    visible: !root.deleteBusy && root.deletePaths.length > 0
                    color: confirmDeleteHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.sun

                    Text {
                        id: deleteConfirmLabel
                        anchors.centerIn: parent
                        text: "DELETE"
                        color: Ryoku.sun
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                    }

                    HoverHandler {
                        id: confirmDeleteHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: root.confirmPermanentDelete()
                    }
                }
            }
        }
    }

    GitDiffPanel {
        id: diffPanel
        anchors.fill: parent
        uiScale: root.uiScale
        actions: GitActions

        onVisibleChanged: {
            if (!visible && root.diffMode) {
                root.diffMode = false
                root.visible = false
            }
        }
    }

    Connections {
        target: permanentOperations

        function onJobFinished(jobId, success) {
            if (jobId !== root.deleteJobId)
                return

            var session = root.deleteSession
            var error = permanentOperations.errorFor(jobId)
            root.deleteJobId = ""
            if (session)
                session.refresh()

            if (success) {
                root.deleteMode = false
                root.deletePaths = []
                root.deleteSession = null
                root.deleteMessage = ""
                root.visible = false
                return
            }

            root.deletePaths = []
            root.deleteMessage = error !== "" ? error : "Permanent delete failed"
            root.deleteMode = true
            root.visible = true
        }
    }

    Connections {
        target: GitActions

        function onOperationFinished(operationId, success, error) {
            if (operationId !== root.pendingGitOperation)
                return

            root.pendingGitOperation = ""
            if (success) {
                root.gitMessage = ""
                GitStatus.refresh()
                root.visible = false
                return
            }

            root.gitMessage = error !== "" ? error : "Git action failed"
            root.refreshGitCapabilities()
        }
    }

    Connections {
        target: Desktop

        function onRyokuActionBusyChanged() {
            root.refreshRyokuCapabilities()
        }

        function onRyokuActionStarted(action, count) {
            root.ryokuMessage = "// RYOKU " + action.toUpperCase() + " · " + count
            root.refreshRyokuCapabilities()
        }

        function onRyokuActionFinished(action, succeeded, failed, error) {
            root.refreshRyokuCapabilities()

            if (failed === 0) {
                root.ryokuMessage = ""
                root.visible = false
                return
            }

            root.ryokuMessage = error !== ""
                ? error
                : ("Ryoku " + action + " failed for " + failed + " item(s)")
        }
    }
}
