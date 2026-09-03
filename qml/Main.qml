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
    readonly property bool modalOpen: conflictSheet.visible || renameSheet.visible
    readonly property bool fileShortcutsEnabled:
        !root.modalOpen && !root.trashMode && !location.activeFocus

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
        if (!root.session || root.session.selectionCount <= 0)
            return
        FileClipboard.copyFiles(root.selectedPaths())
    }

    function cutSelection() {
        if (!root.session || root.session.selectionCount <= 0)
            return
        FileClipboard.cutFiles(root.selectedPaths())
    }

    function pasteClipboard() {
        if (root.trashMode || !root.session || !FileClipboard.hasFiles)
            return

        var paths = FileClipboard.filePaths()
        if (!paths || paths.length === 0)
            return

        var id = FileClipboard.cut
            ? operations.move(paths, root.session.path)
            : operations.copy(paths, root.session.path)

        if (id === "") {
            root.lastError = "Could not start paste operation"
            errorClear.restart()
            return
        }

        if (FileClipboard.cut)
            root.cutPasteJobs[id] = paths
    }

    function trashSelection() {
        if (root.trashMode || !root.session || root.session.selectionCount <= 0)
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
        if (root.trashMode || !root.session || root.session.selectionCount !== 1)
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
        if (root.trashMode || !root.session)
            return

        root.pendingRenamePath = ""
        root.pendingCreateFolder = true
        renameSheet.openFor("New Folder", "// NEW FOLDER", "CREATE", false)
    }

    function duplicateSelection() {
        if (root.trashMode || !root.session || root.session.selectionCount <= 0)
            return

        var id = operations.duplicate(root.selectedPaths())
        if (id === "") {
            root.lastError = "Could not start duplicate operation"
            errorClear.restart()
        }
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
    Shortcut { sequence: "Escape"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.clearSelection() }

    Shortcut { sequence: "Ctrl+1"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.viewMode = 0 }
    Shortcut { sequence: "Ctrl+2"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.viewMode = 1 }
    Shortcut { sequence: "Ctrl+3"; enabled: root.fileShortcutsEnabled; onActivated: if (root.session) root.session.viewMode = 2 }

    Shortcut { sequence: "Ctrl+C"; enabled: root.fileShortcutsEnabled; onActivated: root.copySelection() }
    Shortcut { sequence: "Ctrl+X"; enabled: root.fileShortcutsEnabled; onActivated: root.cutSelection() }
    Shortcut { sequence: "Ctrl+V"; enabled: root.fileShortcutsEnabled; onActivated: root.pasteClipboard() }
    Shortcut { sequence: "Delete"; enabled: root.fileShortcutsEnabled; onActivated: root.trashSelection() }
    Shortcut { sequence: "F2"; enabled: root.fileShortcutsEnabled; onActivated: root.beginRename() }
    Shortcut { sequence: "Ctrl+Shift+N"; enabled: root.fileShortcutsEnabled; onActivated: root.beginNewFolder() }
    Shortcut { sequence: "Ctrl+Shift+D"; enabled: root.fileShortcutsEnabled; onActivated: root.duplicateSelection() }

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
                    visible: !root.trashMode

                    Repeater {
                        model: [
                            { label: "NEW", action: "new" },
                            { label: "DUP", action: "duplicate" }
                        ]

                        delegate: Rectangle {
                            id: actionButton
                            required property var modelData

                            readonly property bool available:
                                modelData.action === "new"
                                    || (root.session && root.session.selectionCount > 0)

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

                Component {
                    id: compactView
                    FileListView {
                        session: root.session
                        files: root.files
                        uiScale: root.u
                        compact: true
                    }
                }

                Component {
                    id: gridView
                    FileGridView {
                        session: root.session
                        files: root.files
                        uiScale: root.u
                    }
                }

                Component {
                    id: detailsView
                    FileListView {
                        session: root.session
                        files: root.files
                        uiScale: root.u
                        compact: false
                    }
                }

                Loader {
                    anchors.fill: parent
                    active: !root.trashMode && root.session !== null && root.files !== null
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

                TrashView {
                    anchors.fill: parent
                    visible: root.trashMode
                    trash: trash
                    uiScale: root.u
                    onRestoreRequested: itemId => trash.restore(itemId, 0)
                }

                Text {
                    anchors.centerIn: parent
                    visible: !root.trashMode
                        && root.files
                        && !root.files.loading
                        && root.files.count === 0
                    text: "// EMPTY_\nThis folder has no visible items"
                    color: Ryoku.inkMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Ryoku.monoFont
                    font.pixelSize: 11 * root.u
                    lineHeight: 1.7
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8 * root.u
                    width: loadingLabel.implicitWidth + 18 * root.u
                    height: 26 * root.u
                    visible: !root.trashMode && root.files && root.files.loading
                    radius: 6 * root.u
                    color: Ryoku.paperLift
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        id: loadingLabel
                        anchors.centerIn: parent
                        text: "READING…"
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.u
                        font.letterSpacing: 1.1
                    }
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
                        : "CTRL+SHIFT+N NEW   CTRL+SHIFT+D DUP   F2 RENAME"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.u
                }
            }
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

                var id = ""
                if (root.pendingCreateFolder && root.session) {
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
