// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    property real uiScale: 1
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

    function openAt(sceneX, sceneY, path, isDirectory, selectedCount, hasClipboard) {
        if (diffPanel.visible)
            diffPanel.close()
        root.diffMode = false
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

    onVisibleChanged: {
        if (!visible && root.diffMode) {
            root.diffMode = false
            diffPanel.close()
        }
        if (!visible && !Desktop.ryokuActionBusy)
            root.selectedPaths = []
    }

    MouseArea {
        anchors.fill: parent
        visible: !root.diffMode
        acceptedButtons: Qt.AllButtons
        onPressed: {
            if (root.pendingGitOperation === "")
                root.visible = false
        }
    }

    Rectangle {
        id: menu
        visible: !root.diffMode
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
                    { label: "RYOKU · INSTALL", action: "ryokuinstall", enabled: root.ryokuInstallAvailable && !Desktop.ryokuActionBusy, visible: root.ryokuInstallAvailable },
                    { label: "RYOKU · COMPRESS", action: "ryokucompress", enabled: root.ryokuCompressAvailable && !Desktop.ryokuActionBusy, visible: root.ryokuCompressAvailable },
                    { label: "RENAME", action: "rename", enabled: root.selectionCount === 1, visible: true },
                    { label: "GIT · STAGE", action: "gitstage", enabled: root.gitStageAvailable && !GitActions.busy, visible: root.gitStageAvailable },
                    { label: "GIT · UNSTAGE", action: "gitunstage", enabled: root.gitUnstageAvailable && !GitActions.busy, visible: root.gitUnstageAvailable },
                    { label: "GIT · DIFF WORKTREE", action: "gitdiff", enabled: root.gitWorktreeDiffAvailable && !GitActions.busy, visible: root.gitWorktreeDiffAvailable },
                    { label: "GIT · DIFF STAGED", action: "gitdiffstaged", enabled: root.gitStagedDiffAvailable && !GitActions.busy, visible: root.gitStagedDiffAvailable },
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
