// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core
import "components"

Window {
    id: root
    width: 1500
    height: 850
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: "Ryofiles"
    color: Ryoku.paper

    readonly property real u: Ryoku.uiScaleFor(Screen.name)
    readonly property var session: tabs.currentSession
    readonly property var files: session ? session.model : null
    readonly property bool modalOpen:
        conflictSheet.visible
        || renameSheet.visible
        || contextMenu.visible
        || openWithSheet.visible
        || propertiesSheet.visible
        || commandPalette.visible
    readonly property bool fileShortcutsEnabled:
        !root.modalOpen && !root.trashMode && !location.activeFocus
    readonly property bool localFileActionsEnabled:
        root.fileShortcutsEnabled && root.session !== null && !root.session.remote

    property bool trashMode: false
    property string lastError: ""
    property string conflictJobId: ""
    property string restoreConflictItemId: ""
    property string pendingRenamePath: ""
    property bool pendingCreateFolder: false
    property var cutPasteJobs: ({})
    property var trashDeleteJobs: ({})

    TabManager { id: tabs }
    OperationManager { id: operations }
    TrashManager { id: trash }

    function syncLocation() {
        location.text = root.trashMode
            ? "trash://"
            : (root.session ? root.session.path : "")
    }

    onTrashModeChanged: root.syncLocation()

    function selectedPaths() {
        return root.session ? root.session.selectedPaths : []
    }

    function copySelection() {
        if (!root.session || root.session.remote || root.session.selectionCount <= 0)
            return
        FileClipboard.copyFiles(root.selectedPaths())
    }

    function cutSelection() {
        if (!root.session || root.session.remote || root.session.selectionCount <= 0)
            return
        FileClipboard.cutFiles(root.selectedPaths())
    }

    function pasteClipboard(destinationPath) {
        if (root.trashMode || !root.session || root.session.remote || !FileClipboard.hasFiles)
            return

        var destination = destinationPath && destinationPath !== ""
            ? destinationPath
            : root.session.path

        var paths = FileClipboard.filePaths()
        if (!paths || paths.length === 0)
            return

        var id = FileClipboard.cut
            ? operations.move(paths, destination)
            : operations.copy(paths, destination)

        if (id === "") {
            root.lastError = "Could not start paste operation"
            errorClear.restart()
            return
        }

        if (FileClipboard.cut)
            root.cutPasteJobs[id] = paths
    }

    function trashSelection() {
        if (root.trashMode || !root.session || root.session.remote || root.session.selectionCount <= 0)
            return

        var id = trash.trash(root.selectedPaths())
        if (id === "") {
            root.lastError = "Could not move selection to Trash"
            errorClear.restart()
            return
        }

        root.trashDeleteJobs[id] = root.session
    }

    function beginRename() {
        if (root.trashMode || !root.session || root.session.remote || root.session.selectionCount !== 1)
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
        if (root.trashMode || !root.session || root.session.remote)
            return

        root.pendingRenamePath = ""
        root.pendingCreateFolder = true
        renameSheet.openFor("New Folder", "// NEW FOLDER", "CREATE", false)
    }

    function duplicateSelection() {
        if (root.trashMode || !root.session || root.session.remote || root.session.selectionCount <= 0)
            return

        var id = operations.duplicate(root.selectedPaths())
        if (id === "") {
            root.lastError = "Could not start duplicate operation"
            errorClear.restart()
        }
    }

    function paletteCommands() {
        var hasSession = root.session !== null
        var browsing = hasSession && !root.trashMode
        var localBrowsing = browsing && !root.session.remote
        var selectionCount = hasSession ? root.session.selectionCount : 0
        var hasSelection = selectionCount > 0
        var oneSelected = selectionCount === 1
        var hiddenVisible = root.files ? root.files.showHidden : false
        var previewVisible = localBrowsing ? root.session.previewVisible : false

        return [
            { id: "nav.location", label: "Go to Location", detail: "Focus the path field", keywords: "path address folder location", shortcut: "Ctrl+L", category: "NAV", enabled: browsing },
            { id: "nav.back", label: "Go Back", detail: "Previous folder in this pane", keywords: "history previous", shortcut: "Alt+Left", category: "NAV", enabled: browsing && root.session.canGoBack },
            { id: "nav.forward", label: "Go Forward", detail: "Next folder in this pane", keywords: "history next", shortcut: "Alt+Right", category: "NAV", enabled: browsing && root.session.canGoForward },
            { id: "nav.up", label: "Go Up", detail: "Parent directory", keywords: "parent directory", shortcut: "Alt+Up", category: "NAV", enabled: browsing },
            { id: "folder.refresh", label: "Refresh", detail: root.trashMode ? "Refresh Trash" : "Refresh active pane", keywords: "reload rescan", shortcut: "Ctrl+R", category: "NAV", enabled: hasSession || root.trashMode },

            { id: "tab.new", label: "New Tab", detail: "Open another directory tab", keywords: "create tab", shortcut: "Ctrl+T", category: "TAB", enabled: true },
            { id: "tab.duplicate", label: "Duplicate Tab", detail: "Open this folder in another tab", keywords: "clone tab", shortcut: "", category: "TAB", enabled: hasSession },
            { id: "tab.close", label: "Close Tab", detail: "Close the active tab", keywords: "remove tab", shortcut: "Ctrl+W", category: "TAB", enabled: tabs.count > 0 },
            { id: "tab.reopen", label: "Reopen Closed Tab", detail: "Restore the most recently closed tab", keywords: "restore undo tab", shortcut: "Ctrl+Shift+T", category: "TAB", enabled: true },
            { id: "tab.next", label: "Next Tab", detail: "Activate the next tab", keywords: "cycle tab", shortcut: "Ctrl+Tab", category: "TAB", enabled: tabs.count > 1 },
            { id: "tab.previous", label: "Previous Tab", detail: "Activate the previous tab", keywords: "cycle tab", shortcut: "Ctrl+Shift+Tab", category: "TAB", enabled: tabs.count > 1 },

            { id: "view.list", label: "List View", detail: "Compact file rows in active pane", keywords: "compact view mode", shortcut: "Ctrl+1", category: "VIEW", enabled: browsing && root.session.viewMode !== 0 },
            { id: "view.grid", label: "Grid View", detail: "Thumbnail grid in active pane", keywords: "tiles view mode", shortcut: "Ctrl+2", category: "VIEW", enabled: browsing && root.session.viewMode !== 1 },
            { id: "view.details", label: "Details View", detail: "Detailed rows in active pane", keywords: "columns view mode", shortcut: "Ctrl+3", category: "VIEW", enabled: browsing && root.session.viewMode !== 2 },
            { id: "view.hidden", label: hiddenVisible ? "Hide Hidden Files" : "Show Hidden Files", detail: "Toggle dotfiles in active pane", keywords: "dotfiles hidden visibility", shortcut: "Ctrl+H", category: "VIEW", enabled: browsing && root.files !== null },
            { id: "view.preview", label: previewVisible ? "Close Preview" : "Open Preview", detail: "Toggle the active pane preview", keywords: "preview inspect pane", shortcut: "Ctrl+Shift+P", category: "VIEW", enabled: localBrowsing },
            { id: "view.split", label: tabs.split ? "Close Split View" : "Open Split View", detail: tabs.split ? "Return this tab to one pane" : "Open a second independent pane", keywords: "dual two pane split", shortcut: "F3", category: "VIEW", enabled: browsing },
            { id: "view.otherPane", label: "Activate Other Pane", detail: "Move keyboard focus to the other split pane", keywords: "switch pane focus", shortcut: "F6", category: "VIEW", enabled: browsing && tabs.split },
            { id: "view.swapPanes", label: "Swap Split Panes", detail: "Exchange the complete left and right pane sessions", keywords: "reverse exchange pane", shortcut: "", category: "VIEW", enabled: browsing && tabs.split },

            { id: "selection.all", label: "Select All", detail: "Select every visible item", keywords: "selection files", shortcut: "Ctrl+A", category: "FILE", enabled: browsing && root.files !== null && root.files.count > 0 },
            { id: "selection.copy", label: "Copy Selection", detail: "Copy selected files", keywords: "clipboard files", shortcut: "Ctrl+C", category: "FILE", enabled: localBrowsing && hasSelection },
            { id: "selection.cut", label: "Cut Selection", detail: "Move selected files on paste", keywords: "clipboard move files", shortcut: "Ctrl+X", category: "FILE", enabled: localBrowsing && hasSelection },
            { id: "selection.paste", label: "Paste", detail: "Paste clipboard into the active pane", keywords: "clipboard files", shortcut: "Ctrl+V", category: "FILE", enabled: localBrowsing && FileClipboard.hasFiles },
            { id: "folder.new", label: "New Folder", detail: "Create a folder in the active pane", keywords: "mkdir create directory", shortcut: "Ctrl+Shift+N", category: "FILE", enabled: localBrowsing },
            { id: "selection.rename", label: "Rename", detail: "Rename the selected item", keywords: "name file folder", shortcut: "F2", category: "FILE", enabled: localBrowsing && oneSelected },
            { id: "selection.duplicate", label: "Duplicate Selection", detail: "Create copies beside selected items", keywords: "clone copy files", shortcut: "Ctrl+Shift+D", category: "FILE", enabled: localBrowsing && hasSelection },
            { id: "selection.trash", label: "Move Selection to Trash", detail: "Trash selected files", keywords: "delete remove recycle", shortcut: "Delete", category: "FILE", enabled: localBrowsing && hasSelection },

            { id: "trash.open", label: "Open Trash", detail: "Browse deleted items", keywords: "recycle deleted restore", shortcut: "", category: "NAV", enabled: !root.trashMode },
            { id: "trash.leave", label: "Leave Trash", detail: "Return to the active directory pane", keywords: "back files folder", shortcut: "", category: "NAV", enabled: root.trashMode && hasSession },

            { id: "dev.terminal", label: "Open Terminal Here", detail: "Use Ryoku's configured terminal", keywords: "shell console ryoku-app", shortcut: "", category: "DEV", enabled: localBrowsing },
            { id: "dev.copyPath", label: root.session && root.session.remote ? "Copy Current Location URI" : "Copy Current Folder Path", detail: hasSession ? root.session.path : "", keywords: "clipboard directory path uri", shortcut: "", category: "DEV", enabled: browsing },
            { id: "dev.gitRefresh", label: "Refresh Git Status", detail: "Refresh branch and file badges", keywords: "repository git status rescan", shortcut: "", category: "DEV", enabled: localBrowsing && GitStatus.repository && !GitStatus.loading }
        ]
    }

    function runPaletteCommand(commandId) {
        if (commandId === "nav.location") {
            Qt.callLater(function() { location.forceActiveFocus() })
        } else if (commandId === "nav.back") {
            if (root.session) root.session.goBack()
        } else if (commandId === "nav.forward") {
            if (root.session) root.session.goForward()
        } else if (commandId === "nav.up") {
            if (root.session) root.session.goUp()
        } else if (commandId === "folder.refresh") {
            if (root.trashMode) trash.refresh()
            else if (root.session) root.session.refresh()
        } else if (commandId === "tab.new") {
            root.trashMode = false
            tabs.newTab("")
        } else if (commandId === "tab.duplicate") {
            tabs.duplicateCurrentTab()
        } else if (commandId === "tab.close") {
            tabs.closeCurrentTab()
        } else if (commandId === "tab.reopen") {
            tabs.reopenClosedTab()
        } else if (commandId === "tab.next") {
            tabs.nextTab()
        } else if (commandId === "tab.previous") {
            tabs.previousTab()
        } else if (commandId === "view.list") {
            if (root.session) root.session.viewMode = 0
        } else if (commandId === "view.grid") {
            if (root.session) root.session.viewMode = 1
        } else if (commandId === "view.details") {
            if (root.session) root.session.viewMode = 2
        } else if (commandId === "view.hidden") {
            if (root.files) root.files.showHidden = !root.files.showHidden
        } else if (commandId === "view.preview") {
            if (root.session && !root.session.remote)
                root.session.previewVisible = !root.session.previewVisible
        } else if (commandId === "view.split") {
            tabs.toggleSplitView()
        } else if (commandId === "view.otherPane") {
            if (tabs.split) tabs.activePane = tabs.activePane === 0 ? 1 : 0
        } else if (commandId === "view.swapPanes") {
            tabs.swapPanes()
        } else if (commandId === "selection.all") {
            if (root.session) root.session.selectAll()
        } else if (commandId === "selection.copy") {
            root.copySelection()
        } else if (commandId === "selection.cut") {
            root.cutSelection()
        } else if (commandId === "selection.paste") {
            root.pasteClipboard()
        } else if (commandId === "folder.new") {
            root.beginNewFolder()
        } else if (commandId === "selection.rename") {
            root.beginRename()
        } else if (commandId === "selection.duplicate") {
            root.duplicateSelection()
        } else if (commandId === "selection.trash") {
            root.trashSelection()
        } else if (commandId === "trash.open") {
            root.trashMode = true
            trash.refresh()
            root.syncLocation()
        } else if (commandId === "trash.leave") {
            root.trashMode = false
            root.syncLocation()
        } else if (commandId === "dev.terminal") {
            if (!root.session || root.session.remote || !GitActions.openTerminal(root.session.path)) {
                root.lastError = "Could not open the configured terminal"
                errorClear.restart()
            }
        } else if (commandId === "dev.copyPath") {
            if (root.session) FileClipboard.copyText(root.session.path)
        } else if (commandId === "dev.gitRefresh") {
            if (root.session && !root.session.remote)
                GitStatus.refresh()
        }
    }

    function openContextMenu(sceneX, sceneY, path, isDirectory) {
        if (root.trashMode || !root.session || root.session.remote)
            return

        contextMenu.openAt(
            sceneX,
            sceneY,
            path,
            isDirectory,
            root.session.selectionCount,
            FileClipboard.hasFiles)
    }

    function openContextTarget() {
        if (!root.session || root.session.remote || contextMenu.targetPath === "")
            return

        if (contextMenu.targetIsDirectory) {
            root.session.navigate(contextMenu.targetPath)
        } else if (!Desktop.openDefault(contextMenu.targetPath)) {
            root.lastError = "Could not open file with the default application"
            errorClear.restart()
        }
    }

    function closeTransientUi() {
        contextMenu.visible = false
        openWithSheet.visible = false
        propertiesSheet.visible = false
        renameSheet.visible = false
        commandPalette.close()
    }

    Timer {
        id: errorClear
        interval: 5000
        onTriggered: root.lastError = ""
    }

    Connections {
        target: root.session

        function onErrorOccurred(message) {
            root.lastError = message
            errorClear.restart()
        }

        function onPathChanged() {
            root.syncLocation()
        }

        function onLocationKindChanged() {
            if (root.session && root.session.remote) {
                contextMenu.visible = false
                openWithSheet.visible = false
                propertiesSheet.visible = false
                renameSheet.visible = false
                root.pendingRenamePath = ""
                root.pendingCreateFolder = false
            }
        }
    }

    Connections {
        target: tabs
        function onCurrentSessionChanged() {
            root.trashMode = false
            root.syncLocation()
        }
    }

    Connections {
        target: operations

        function onConflictRaised(jobId, source, destination) {
            root.conflictJobId = jobId
            root.restoreConflictItemId = ""
            conflictSheet.restoreMode = false
            conflictSheet.allowApplyToAll = true
            conflictSheet.applyToAll = false
            conflictSheet.sourcePath = source
            conflictSheet.destinationPath = destination
            conflictSheet.visible = true
        }

        function onJobFinished(jobId, success) {
            if (root.cutPasteJobs[jobId] !== undefined) {
                if (success)
                    FileClipboard.clearIfMatches(root.cutPasteJobs[jobId], true)
                delete root.cutPasteJobs[jobId]
            }

            if (root.conflictJobId === jobId) {
                conflictSheet.visible = false
                root.conflictJobId = ""
            }

            if (!success) {
                var error = operations.errorFor(jobId)
                if (error !== "") {
                    root.lastError = error
                    errorClear.restart()
                }
            }

            if (success && root.session && !root.trashMode)
                root.session.refresh()
        }
    }

    Connections {
        target: trash

        function onOperationFinished(operationId, success, error) {
            var deleteSession = root.trashDeleteJobs[operationId]
            if (deleteSession !== undefined) {
                if (success && deleteSession)
                    deleteSession.clearSelection()
                delete root.trashDeleteJobs[operationId]
            }

            if (!success && error !== "") {
                root.lastError = error
                errorClear.restart()
            }

            if (success && root.session && !root.trashMode)
                root.session.refresh()
        }

        function onRestoreConflict(itemId, originalPath) {
            root.restoreConflictItemId = itemId
            root.conflictJobId = ""
            conflictSheet.restoreMode = true
            conflictSheet.allowApplyToAll = false
            conflictSheet.applyToAll = false
            conflictSheet.sourcePath = "Trash item"
            conflictSheet.destinationPath = originalPath
            conflictSheet.visible = true
        }
    }

    Shortcut {
        sequence: "Ctrl+K"
        enabled: !root.modalOpen
        onActivated: commandPalette.open()
    }

    Shortcut {
        sequence: "F3"
        enabled: !root.modalOpen && !root.trashMode && root.session !== null
        onActivated: tabs.toggleSplitView()
    }

    Shortcut {
        sequence: "Ctrl+T"
        enabled: !root.modalOpen
        onActivated: {
            root.trashMode = false
            tabs.newTab("")
        }
    }
    Shortcut { sequence: "Ctrl+W"; enabled: !root.modalOpen; onActivated: tabs.closeCurrentTab() }
    Shortcut { sequence: "Ctrl+Shift+T"; enabled: !root.modalOpen; onActivated: tabs.reopenClosedTab() }
    Shortcut { sequence: "Ctrl+Tab"; enabled: !root.modalOpen; onActivated: tabs.nextTab() }
    Shortcut { sequence: "Ctrl+Shift+Tab"; enabled: !root.modalOpen; onActivated: tabs.previousTab() }

    Shortcut { sequence: "Alt+Left"; enabled: !root.modalOpen && !root.trashMode; onActivated: if (root.session) root.session.goBack() }
    Shortcut { sequence: "Alt+Right"; enabled: !root.modalOpen && !root.trashMode; onActivated: if (root.session) root.session.goForward() }
    Shortcut { sequence: "Alt+Up"; enabled: !root.modalOpen && !root.trashMode; onActivated: if (root.session) root.session.goUp() }

    Shortcut {
        sequence: "Ctrl+H"
        enabled: !root.modalOpen && !root.trashMode
        onActivated: if (root.files) root.files.showHidden = !root.files.showHidden
    }
    Shortcut { sequence: "Ctrl+R"; enabled: !root.modalOpen; onActivated: root.trashMode ? trash.refresh() : (root.session ? root.session.refresh() : undefined) }
    Shortcut { sequence: "Ctrl+L"; enabled: !root.modalOpen && !root.trashMode; onActivated: location.forceActiveFocus() }

    Shortcut { sequence: "Ctrl+A"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.selectAll() }
    Shortcut { sequence: "Escape"; enabled: root.modalOpen; onActivated: root.closeTransientUi() }
    Shortcut { sequence: "Escape"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.clearSelection() }

    Shortcut { sequence: "Ctrl+1"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.viewMode = 0 }
    Shortcut { sequence: "Ctrl+2"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.viewMode = 1 }
    Shortcut { sequence: "Ctrl+3"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.viewMode = 2 }

    Shortcut { sequence: "Ctrl+C"; enabled: root.localFileActionsEnabled; onActivated: root.copySelection() }
    Shortcut { sequence: "Ctrl+X"; enabled: root.localFileActionsEnabled; onActivated: root.cutSelection() }
    Shortcut { sequence: "Ctrl+V"; enabled: root.localFileActionsEnabled; onActivated: root.pasteClipboard() }
    Shortcut { sequence: "Delete"; enabled: root.localFileActionsEnabled; onActivated: root.trashSelection() }
    Shortcut { sequence: "F2"; enabled: root.localFileActionsEnabled; onActivated: root.beginRename() }
    Shortcut { sequence: "Ctrl+Shift+N"; enabled: root.localFileActionsEnabled; onActivated: root.beginNewFolder() }
    Shortcut { sequence: "Ctrl+Shift+D"; enabled: root.localFileActionsEnabled; onActivated: root.duplicateSelection() }

    Rectangle {
        anchors.fill: parent
        color: Ryoku.paper

        RyokuRail {
            id: rail
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            uiScale: root.u
            fs: root.files
            trashCount: trash.count
            trashActive: root.trashMode

            onNavigate: path => {
                root.trashMode = false
                if (root.session)
                    root.session.navigate(path)
            }

            onOpenTrash: {
                root.trashMode = true
                trash.refresh()
                root.syncLocation()
            }
        }

        Item {
            id: stage
            anchors.left: rail.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            TabStrip {
                id: tabStrip
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                tabs: tabs
                uiScale: root.u
            }

            Rectangle {
                id: toolbar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: tabStrip.bottom
                height: 58 * root.u
                color: "transparent"

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Ryoku.line
                }

                Row {
                    id: navButtons
                    anchors.left: parent.left
                    anchors.leftMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6 * root.u

                    Repeater {
                        model: [
                            { label: "←", action: "back" },
                            { label: "→", action: "forward" },
                            { label: "↑", action: "up" }
                        ]

                        delegate: Rectangle {
                            id: navButton
                            required property var modelData

                            readonly property bool available: {
                                if (root.trashMode || !root.session)
                                    return false
                                if (modelData.action === "back")
                                    return root.session.canGoBack
                                if (modelData.action === "forward")
                                    return root.session.canGoForward
                                return true
                            }

                            width: 34 * root.u
                            height: 34 * root.u
                            radius: 6 * root.u
                            color: navHover.hovered && available ? Ryoku.tint10 : "transparent"
                            border.width: 1
                            border.color: available ? Ryoku.line : Ryoku.lineSoft
                            opacity: available ? 1.0 : 0.45

                            Text {
                                anchors.centerIn: parent
                                text: navButton.modelData.label
                                color: Ryoku.ink
                                font.family: Ryoku.uiFont
                                font.pixelSize: 16 * root.u
                            }

                            HoverHandler {
                                id: navHover
                                enabled: navButton.available
                                cursorShape: Qt.PointingHandCursor
                            }

                            TapHandler {
                                enabled: navButton.available
                                onTapped: {
                                    if (!root.session)
                                        return
                                    if (navButton.modelData.action === "back")
                                        root.session.goBack()
                                    else if (navButton.modelData.action === "forward")
                                        root.session.goForward()
                                    else
                                        root.session.goUp()
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.left: navButtons.right
                    anchors.leftMargin: 10 * root.u
                    anchors.right: actionButtons.left
                    anchors.rightMargin: 10 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    height: 34 * root.u
                    radius: 6 * root.u
                    color: "transparent"
                    border.width: location.activeFocus ? 2 : 1
                    border.color: location.activeFocus ? Ryoku.ink : Ryoku.line

                    TextInput {
                        id: location
                        anchors.fill: parent
                        anchors.leftMargin: 10 * root.u
                        anchors.rightMargin: 10 * root.u
                        verticalAlignment: Text.AlignVCenter
                        color: Ryoku.ink
                        selectionColor: Ryoku.bone
                        selectedTextColor: Ryoku.inkOnBone
                        font.family: Ryoku.monoFont
                        font.pixelSize: 11 * root.u
                        text: root.trashMode
                            ? "trash://"
                            : (root.session ? root.session.path : "")
                        readOnly: root.trashMode
                        selectByMouse: true

                        onAccepted: {
                            if (root.trashMode)
                                return
                            if (root.session && !root.session.navigate(text))
                                text = root.session.path
                            focus = false
                        }
                    }
                }

                Row {
                    id: actionButtons
                    anchors.right: viewModes.left
                    anchors.rightMargin: 8 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4 * root.u
                    visible: !root.trashMode && root.session && !root.session.remote

                    Repeater {
                        model: [
                            { label: "NEW", action: "new" },
                            { label: "DUP", action: "duplicate" }
                        ]

                        delegate: Rectangle {
                            id: actionButton
                            required property var modelData

                            readonly property bool available:
                                root.session && !root.session.remote &&
                                (modelData.action === "new"
                                    || root.session.selectionCount > 0)

                            width: actionLabel.implicitWidth + 14 * root.u
                            height: 30 * root.u
                            radius: 6 * root.u
                            color: actionHover.hovered && available
                                ? Ryoku.tint10
                                : "transparent"
                            border.width: 1
                            border.color: available ? Ryoku.line : Ryoku.lineSoft
                            opacity: available ? 1.0 : 0.45

                            Text {
                                id: actionLabel
                                anchors.centerIn: parent
                                text: actionButton.modelData.label
                                color: Ryoku.inkDim
                                font.family: Ryoku.uiFont
                                font.pixelSize: 9 * root.u
                                font.weight: Font.Medium
                                font.letterSpacing: 1.0
                            }

                            HoverHandler {
                                id: actionHover
                                enabled: actionButton.available
                                cursorShape: Qt.PointingHandCursor
                            }

                            TapHandler {
                                enabled: actionButton.available
                                onTapped: {
                                    if (actionButton.modelData.action === "new")
                                        root.beginNewFolder()
                                    else
                                        root.duplicateSelection()
                                }
                            }
                        }
                    }
                }

                Row {
                    id: viewModes
                    anchors.right: hiddenBadge.left
                    anchors.rightMargin: 8 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4 * root.u
                    visible: !root.trashMode

                    Repeater {
                        model: [
                            { label: "LIST", mode: 0 },
                            { label: "GRID", mode: 1 },
                            { label: "DETAILS", mode: 2 }
                        ]

                        delegate: Rectangle {
                            id: modeButton
                            required property var modelData

                            readonly property bool selected:
                                root.session && root.session.viewMode === modelData.mode

                            width: modeLabel.implicitWidth + 14 * root.u
                            height: 30 * root.u
                            radius: 6 * root.u
                            color: selected
                                ? Ryoku.bone
                                : (modeHover.hovered ? Ryoku.tint10 : "transparent")
                            border.width: 1
                            border.color: selected ? Ryoku.bone : Ryoku.line

                            Text {
                                id: modeLabel
                                anchors.centerIn: parent
                                text: modeButton.modelData.label
                                color: modeButton.selected ? Ryoku.inkOnBone : Ryoku.inkDim
                                font.family: Ryoku.uiFont
                                font.pixelSize: 9 * root.u
                                font.weight: Font.Medium
                                font.letterSpacing: 1.1
                            }

                            HoverHandler {
                                id: modeHover
                                cursorShape: Qt.PointingHandCursor
                            }
                            TapHandler {
                                onTapped: if (root.session)
                                    root.session.viewMode = modeButton.modelData.mode
                            }
                        }
                    }
                }

                Rectangle {
                    id: hiddenBadge
                    anchors.right: parent.right
                    anchors.rightMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    width: hiddenText.implicitWidth + 20 * root.u
                    height: 30 * root.u
                    radius: 6 * root.u
                    visible: !root.trashMode
                    color: root.files && root.files.showHidden ? Ryoku.bone : "transparent"
                    border.width: 1
                    border.color: root.files && root.files.showHidden ? Ryoku.bone : Ryoku.line

                    Text {
                        id: hiddenText
                        anchors.centerIn: parent
                        text: "HIDDEN"
                        color: root.files && root.files.showHidden ? Ryoku.inkOnBone : Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.u
                        font.weight: Font.Medium
                        font.letterSpacing: 1.2
                    }

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: if (root.files) root.files.showHidden = !root.files.showHidden
                    }
                }
            }

            Item {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: toolbar.bottom
                anchors.bottom: status.top
                anchors.margins: 20 * root.u

                SplitPaneContainer {
                    anchors.fill: parent
                    visible: !root.trashMode
                    tabs: tabs
                    uiScale: root.u
                    onContextRequested: function(sceneX, sceneY, path, isDirectory) {
                        root.openContextMenu(sceneX, sceneY, path, isDirectory)
                    }
                }

                TrashView {
                    anchors.fill: parent
                    visible: root.trashMode
                    trash: trash
                    uiScale: root.u
                    onRestoreRequested: itemId => trash.restore(itemId, 0)
                }
            }

            Rectangle {
                id: status
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 38 * root.u
                color: "transparent"

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Ryoku.line
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.lastError !== ""
                        ? root.lastError
                        : (root.trashMode
                            ? trash.count + (trash.count === 1 ? " trashed item" : " trashed items")
                            : ((root.session && root.session.selectionCount > 0
                                ? root.session.selectionCount + " selected  // "
                                : "")
                               + (root.files ? root.files.count : 0) + " items"
                               + (root.files && root.files.showHidden ? "  // hidden visible" : "")
                               + (tabs.split ? "  // pane " + (tabs.activePane + 1) + "/2" : "")
                               + "  // " + tabs.count + (tabs.count === 1 ? " tab" : " tabs")))
                    color: root.lastError !== "" ? Ryoku.sun : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 10 * root.u
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.trashMode
                        ? "RESTORE ITEMS   CTRL+R REFRESH"
                        : (root.session && root.session.remote
                            ? "CTRL+K COMMANDS   F3 SPLIT   CTRL+L LOCATION"
                            : "CTRL+K COMMANDS   F3 SPLIT   F6 PANE   F2 RENAME")
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.u
                }
            }
        }

        FileContextMenu {
            id: contextMenu
            uiScale: root.u
            operationManager: operations

            onOpenRequested: root.openContextTarget()

            onOpenNewTabRequested: {
                if (targetIsDirectory && targetPath !== "")
                    tabs.newTab(targetPath)
            }

            onOpenWithRequested: {
                if (targetPath !== "" && root.session && !root.session.remote)
                    openWithSheet.openFor(targetPath)
            }

            onCopyRequested: root.copySelection()
            onCutRequested: root.cutSelection()

            onPasteIntoRequested: {
                if (targetIsDirectory && targetPath !== "")
                    root.pasteClipboard(targetPath)
            }

            onDuplicateRequested: root.duplicateSelection()

            onExtractHereRequested: {
                if (targetPath === "" || !root.session || root.session.remote)
                    return

                var id = operations.extractArchiveHere(targetPath)
                if (id === "") {
                    root.lastError = "Could not start archive extraction"
                    errorClear.restart()
                }
            }

            onRenameRequested: root.beginRename()
            onTrashRequested: root.trashSelection()

            onPropertiesRequested: {
                if (targetPath !== "" && root.session && !root.session.remote)
                    propertiesSheet.openFor(targetPath)
            }
        }

        CommandPalette {
            id: commandPalette
            anchors.fill: parent
            uiScale: root.u
            commands: root.paletteCommands()
            onCommandTriggered: commandId => root.runPaletteCommand(commandId)
        }

        OpenWithSheet {
            id: openWithSheet
            desktop: Desktop
            uiScale: root.u
        }

        PropertiesSheet {
            id: propertiesSheet
            desktop: Desktop
            uiScale: root.u
        }

        OperationDrawer {
            anchors.right: parent.right
            anchors.rightMargin: 20 * root.u
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 54 * root.u
            operations: operations
            uiScale: root.u
            z: 600
        }

        ConflictSheet {
            id: conflictSheet
            uiScale: root.u

            onChoose: function(decision, applyToAll) {
                visible = false

                if (restoreMode) {
                    if (decision === 1 || decision === 2)
                        trash.restore(root.restoreConflictItemId, decision)
                    root.restoreConflictItemId = ""
                    return
                }

                if (root.conflictJobId !== "")
                    operations.resolveConflict(root.conflictJobId, decision, applyToAll)
            }
        }

        RenameSheet {
            id: renameSheet
            uiScale: root.u

            onAccepted: function(newName) {
                visible = false

                if (!root.session || root.session.remote) {
                    root.pendingRenamePath = ""
                    root.pendingCreateFolder = false
                    return
                }

                var id = ""
                if (root.pendingCreateFolder) {
                    id = operations.createFolder(root.session.path, newName)
                } else if (root.pendingRenamePath !== "") {
                    id = operations.rename(root.pendingRenamePath, newName)
                }

                if (id === "") {
                    root.lastError = root.pendingCreateFolder
                        ? "Could not start new-folder operation"
                        : "Could not start rename operation"
                    errorClear.restart()
                }

                root.pendingRenamePath = ""
                root.pendingCreateFolder = false
            }

            onCancelled: {
                visible = false
                root.pendingRenamePath = ""
                root.pendingCreateFolder = false
            }
        }
    }
}
