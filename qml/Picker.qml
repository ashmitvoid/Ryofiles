// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Window {
    id: root

    property string dialogTitle: ""
    property string customAcceptLabel: ""

    width: 1000
    height: 650
    minimumWidth: 760
    minimumHeight: 500
    visible: true
    title: dialogTitle !== "" ? dialogTitle : "Ryofiles Picker"
    color: Ryoku.paper

    readonly property real u: Ryoku.uiScaleFor(Screen.name)
    readonly property var session: tabs.currentSession
    readonly property var files: session ? session.model : null
    readonly property bool canAccept: Picker.saveMode
        ? Picker.canSave(
            session ? session.path : "",
            saveName.text)
        : Picker.canAccept(
            session ? session.selectedPaths : [],
            session ? session.path : "")
    readonly property string validationMessage: Picker.saveMode
        ? Picker.saveValidationError(
            session ? session.path : "",
            saveName.text)
        : Picker.validationError(
            session ? session.selectedPaths : [],
            session ? session.path : "")
    readonly property bool saveOverwriteRequired:
        Picker.saveMode
        && root.canAccept
        && Picker.saveNeedsOverwrite(
            session ? session.path : "",
            saveName.text)

    property string locationError: ""

    TabManager { id: tabs }

    function syncLocation() {
        if (root.session && !location.activeFocus)
            location.text = root.session.path
    }

    function acceptCurrent() {
        if (!root.session)
            return
        if (Picker.saveMode) {
            Picker.requestSave(root.session.path, saveName.text)
            return
        }
        Picker.accept(root.session.selectedPaths, root.session.path)
    }

    function setSaveName(name) {
        if (!Picker.saveMode)
            return
        Picker.clearOverwriteConfirmation()
        saveName.text = name
    }

    Component.onCompleted: {
        if (root.session) {
            root.session.viewMode = 2
            root.session.navigate(Picker.initialDirectory)
        }
        if (Picker.saveMode)
            saveName.text = Picker.suggestedName
        root.syncLocation()
    }

    Connections {
        target: root.session
        function onPathChanged() {
            root.locationError = ""
            Picker.clearOverwriteConfirmation()
            root.syncLocation()
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: !location.activeFocus && !filter.activeFocus && !saveName.activeFocus
        onActivated: {
            if (Picker.overwriteConfirmationRequired)
                Picker.clearOverwriteConfirmation()
            else
                Picker.cancel()
        }
    }
    Shortcut {
        sequence: "Alt+Left"
        enabled: !Picker.overwriteConfirmationRequired
        onActivated: if (root.session) root.session.goBack()
    }
    Shortcut {
        sequence: "Alt+Right"
        enabled: !Picker.overwriteConfirmationRequired
        onActivated: if (root.session) root.session.goForward()
    }
    Shortcut {
        sequence: "Alt+Up"
        enabled: !Picker.overwriteConfirmationRequired
        onActivated: if (root.session) root.session.goUp()
    }
    Shortcut {
        sequence: "Ctrl+L"
        enabled: !Picker.overwriteConfirmationRequired
        onActivated: {
            location.forceActiveFocus()
            location.selectAll()
        }
    }
    Shortcut {
        sequence: "Ctrl+F"
        enabled: !Picker.overwriteConfirmationRequired
        onActivated: {
            filter.forceActiveFocus()
            filter.selectAll()
        }
    }
    Shortcut {
        sequence: "Ctrl+H"
        enabled: !Picker.overwriteConfirmationRequired
        onActivated: if (root.files) root.files.showHidden = !root.files.showHidden
    }
    Shortcut {
        sequence: "Ctrl+S"
        enabled: Picker.saveMode && root.canAccept && !Picker.overwriteConfirmationRequired
        onActivated: root.acceptCurrent()
    }

    Rectangle {
        anchors.fill: parent
        color: Ryoku.paper

        Rectangle {
            id: topBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 64 * root.u
            color: "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Ryoku.line
            }

            Row {
                id: navigation
                anchors.left: parent.left
                anchors.leftMargin: 18 * root.u
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
                            if (!root.session || Picker.overwriteConfirmationRequired)
                                return false
                            if (modelData.action === "back")
                                return root.session.canGoBack
                            if (modelData.action === "forward")
                                return root.session.canGoForward
                            return root.session.path !== "/"
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
                id: locationBox
                anchors.left: navigation.right
                anchors.leftMargin: 10 * root.u
                anchors.right: hiddenButton.left
                anchors.rightMargin: 10 * root.u
                anchors.verticalCenter: parent.verticalCenter
                height: 36 * root.u
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
                    text: root.session ? root.session.path : ""
                    color: Ryoku.ink
                    selectionColor: Ryoku.bone
                    selectedTextColor: Ryoku.inkOnBone
                    font.family: Ryoku.monoFont
                    font.pixelSize: 10 * root.u
                    selectByMouse: true
                    clip: true
                    enabled: !Picker.overwriteConfirmationRequired

                    onAccepted: {
                        if (!root.session)
                            return
                        if (text.indexOf("://") >= 0) {
                            root.locationError = "Picker supports local folders only"
                            text = root.session.path
                            focus = false
                            return
                        }
                        if (!root.session.navigate(text))
                            text = root.session.path
                        focus = false
                    }

                    Keys.onEscapePressed: function(event) {
                        text = root.session ? root.session.path : ""
                        focus = false
                        root.locationError = ""
                        event.accepted = true
                    }
                }
            }

            Rectangle {
                id: hiddenButton
                anchors.right: parent.right
                anchors.rightMargin: 18 * root.u
                anchors.verticalCenter: parent.verticalCenter
                width: hiddenLabel.implicitWidth + 18 * root.u
                height: 32 * root.u
                radius: 6 * root.u
                color: root.files && root.files.showHidden ? Ryoku.bone : "transparent"
                border.width: 1
                border.color: root.files && root.files.showHidden ? Ryoku.bone : Ryoku.line
                opacity: Picker.overwriteConfirmationRequired ? 0.45 : 1.0

                Text {
                    id: hiddenLabel
                    anchors.centerIn: parent
                    text: "HIDDEN"
                    color: root.files && root.files.showHidden ? Ryoku.inkOnBone : Ryoku.inkDim
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * root.u
                    font.weight: Font.Medium
                    font.letterSpacing: 0.8
                }

                HoverHandler {
                    enabled: !Picker.overwriteConfirmationRequired
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    enabled: !Picker.overwriteConfirmationRequired
                    onTapped: if (root.files) root.files.showHidden = !root.files.showHidden
                }
            }
        }

        Rectangle {
            id: filterBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            height: 46 * root.u
            color: Ryoku.paper

            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: 18 * root.u
                anchors.right: matchLabel.left
                anchors.rightMargin: 12 * root.u
                anchors.verticalCenter: parent.verticalCenter
                height: 30 * root.u
                radius: 6 * root.u
                color: "transparent"
                border.width: filter.activeFocus ? 2 : 1
                border.color: filter.activeFocus ? Ryoku.ink : Ryoku.line

                TextInput {
                    id: filter
                    anchors.fill: parent
                    anchors.leftMargin: 10 * root.u
                    anchors.rightMargin: 10 * root.u
                    verticalAlignment: Text.AlignVCenter
                    color: Ryoku.ink
                    selectionColor: Ryoku.bone
                    selectedTextColor: Ryoku.inkOnBone
                    font.family: Ryoku.uiFont
                    font.pixelSize: 10 * root.u
                    selectByMouse: true
                    clip: true
                    enabled: !Picker.overwriteConfirmationRequired

                    onTextEdited: if (root.files) root.files.filterQuery = text
                    Keys.onEscapePressed: function(event) {
                        text = ""
                        if (root.files) root.files.filterQuery = ""
                        focus = false
                        event.accepted = true
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    visible: filter.text === "" && !filter.activeFocus
                    text: "Filter this folder…"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 10 * root.u
                }
            }

            Text {
                id: matchLabel
                anchors.right: parent.right
                anchors.rightMargin: 18 * root.u
                anchors.verticalCenter: parent.verticalCenter
                width: 120 * root.u
                horizontalAlignment: Text.AlignRight
                text: root.files ? root.files.count + " ITEMS" : ""
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.u
            }
        }

        Rectangle {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: filterBar.bottom
            height: 32 * root.u
            color: Ryoku.tint5

            Row {
                anchors.fill: parent
                anchors.leftMargin: 16 * root.u
                anchors.rightMargin: 16 * root.u
                spacing: 12 * root.u

                Text {
                    width: Math.max(200, parent.width - 300 * root.u)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "NAME"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.u
                }
                Text {
                    width: 110 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: "SIZE"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.u
                }
                Text {
                    width: 150 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: "MODIFIED"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.u
                }
            }
        }

        ListView {
            id: view
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: footer.top
            anchors.leftMargin: 8 * root.u
            anchors.rightMargin: 8 * root.u
            clip: true
            model: root.files
            currentIndex: -1
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            enabled: !Picker.overwriteConfirmationRequired

            onCurrentIndexChanged: {
                if (!activeFocus || !root.session || currentIndex < 0)
                    return
                root.session.selectSingle(currentIndex)
            }

            delegate: Rectangle {
                id: fileRow
                required property int index
                required property string name
                required property string filePath
                required property bool isDir
                required property string sizeText
                required property string modifiedText

                readonly property bool selected: {
                    var revision = root.session ? root.session.selectionRevision : 0
                    return revision >= 0 && root.session && root.session.isSelectedPath(filePath)
                }

                width: view.width
                height: 42 * root.u
                radius: 6 * root.u
                color: selected
                    ? Ryoku.bone
                    : (rowMouse.containsMouse ? Ryoku.tint5 : "transparent")

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10 * root.u
                    anchors.rightMargin: 10 * root.u
                    spacing: 12 * root.u

                    Text {
                        width: 20 * root.u
                        anchors.verticalCenter: parent.verticalCenter
                        text: fileRow.isDir ? "▰" : "·"
                        color: fileRow.selected ? Ryoku.inkOnBoneDim : Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 12 * root.u
                    }
                    Text {
                        width: Math.max(160, parent.width - 20 * root.u - 24 * root.u - 110 * root.u - 150 * root.u)
                        anchors.verticalCenter: parent.verticalCenter
                        text: fileRow.name
                        elide: Text.ElideMiddle
                        color: fileRow.selected ? Ryoku.inkOnBone : Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 12 * root.u
                    }
                    Text {
                        width: 110 * root.u
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: fileRow.isDir ? "DIR" : fileRow.sizeText
                        color: fileRow.selected ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.u
                    }
                    Text {
                        width: 150 * root.u
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: fileRow.modifiedText
                        color: fileRow.selected ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.u
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton

                    onClicked: function(event) {
                        view.currentIndex = fileRow.index
                        if (!Picker.multiple || Picker.folderMode || Picker.saveMode) {
                            root.session.selectSingle(fileRow.index)
                        } else if (event.modifiers & Qt.ShiftModifier) {
                            root.session.selectRange(fileRow.index)
                        } else if (event.modifiers & Qt.ControlModifier) {
                            root.session.toggleSelection(fileRow.index)
                        } else {
                            root.session.selectSingle(fileRow.index)
                        }
                        if (Picker.saveMode && !fileRow.isDir)
                            root.setSaveName(fileRow.name)
                        view.forceActiveFocus()
                    }

                    onDoubleClicked: {
                        view.currentIndex = fileRow.index
                        if (fileRow.isDir) {
                            root.session.navigate(fileRow.filePath)
                            return
                        }
                        if (Picker.saveMode) {
                            root.session.selectSingle(fileRow.index)
                            root.setSaveName(fileRow.name)
                            Qt.callLater(root.acceptCurrent)
                            return
                        }
                        if (!Picker.folderMode) {
                            if (!fileRow.selected)
                                root.session.selectSingle(fileRow.index)
                            Qt.callLater(root.acceptCurrent)
                        }
                    }
                }
            }

            Keys.onReturnPressed: function(event) {
                if (!root.session || currentIndex < 0) {
                    event.accepted = true
                    return
                }
                if (root.files.isDirectoryAt(currentIndex)) {
                    root.session.navigate(root.files.pathAt(currentIndex))
                } else if (Picker.saveMode) {
                    root.session.selectSingle(currentIndex)
                    root.setSaveName(root.files.data(root.files.index(currentIndex, 0), 0))
                    root.acceptCurrent()
                } else if (!Picker.folderMode) {
                    root.session.selectSingle(currentIndex)
                    root.acceptCurrent()
                }
                event.accepted = true
            }
            Keys.onEnterPressed: function(event) {
                if (!root.session || currentIndex < 0) {
                    event.accepted = true
                    return
                }
                if (root.files.isDirectoryAt(currentIndex)) {
                    root.session.navigate(root.files.pathAt(currentIndex))
                } else if (Picker.saveMode) {
                    root.session.selectSingle(currentIndex)
                    root.setSaveName(root.files.data(root.files.index(currentIndex, 0), 0))
                    root.acceptCurrent()
                } else if (!Picker.folderMode) {
                    root.session.selectSingle(currentIndex)
                    root.acceptCurrent()
                }
                event.accepted = true
            }
        }

        Rectangle {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: (Picker.saveMode ? 126 : 76) * root.u
            color: Ryoku.paperLift

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Ryoku.line
            }

            Rectangle {
                id: saveNameBox
                visible: Picker.saveMode
                anchors.left: parent.left
                anchors.leftMargin: 18 * root.u
                anchors.right: actions.left
                anchors.rightMargin: 20 * root.u
                anchors.top: parent.top
                anchors.topMargin: 12 * root.u
                height: 36 * root.u
                radius: 6 * root.u
                color: "transparent"
                border.width: saveName.activeFocus ? 2 : 1
                border.color: saveName.activeFocus ? Ryoku.ink : Ryoku.line

                Text {
                    id: saveNameLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 10 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    text: "NAME"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.u
                    font.letterSpacing: 0.8
                }

                TextInput {
                    id: saveName
                    anchors.left: saveNameLabel.right
                    anchors.leftMargin: 12 * root.u
                    anchors.right: parent.right
                    anchors.rightMargin: 10 * root.u
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    verticalAlignment: Text.AlignVCenter
                    color: Ryoku.ink
                    selectionColor: Ryoku.bone
                    selectedTextColor: Ryoku.inkOnBone
                    font.family: Ryoku.uiFont
                    font.pixelSize: 10 * root.u
                    selectByMouse: true
                    clip: true
                    enabled: Picker.saveMode

                    onTextEdited: Picker.clearOverwriteConfirmation()
                    onAccepted: root.acceptCurrent()
                    Keys.onEscapePressed: function(event) {
                        if (Picker.overwriteConfirmationRequired)
                            Picker.clearOverwriteConfirmation()
                        else
                            focus = false
                        event.accepted = true
                    }
                }
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 18 * root.u
                anchors.right: actions.left
                anchors.rightMargin: 20 * root.u
                y: Picker.saveMode
                    ? saveNameBox.y + saveNameBox.height + 7 * root.u
                    : (footer.height - height) / 2
                spacing: 5 * root.u

                Text {
                    width: parent.width
                    text: Picker.saveMode
                        ? "SAVE FILE · " + (root.session ? root.session.path : "")
                        : (Picker.folderMode
                            ? "SELECT FOLDER · " + (root.session ? root.session.path : "")
                            : (Picker.multiple ? "OPEN FILES · MULTI-SELECT" : "OPEN FILE"))
                    elide: Text.ElideMiddle
                    color: Ryoku.ink
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.u
                    font.letterSpacing: 0.8
                }

                Text {
                    width: parent.width
                    text: {
                        if (root.locationError !== "")
                            return root.locationError
                        if (Picker.error !== "")
                            return Picker.error
                        if (Picker.saveMode) {
                            if (root.validationMessage !== "")
                                return Picker.mimeTypes.length > 0
                                    ? "FILTER · " + Picker.mimeTypes.join(", ") + " · " + root.validationMessage
                                    : root.validationMessage
                            if (root.saveOverwriteRequired)
                                return "Existing file · explicit replacement confirmation required"
                            return Picker.mimeTypes.length > 0
                                ? "FILTER · " + Picker.mimeTypes.join(", ") + " · READY"
                                : "Ready"
                        }
                        if (!Picker.folderMode && Picker.mimeTypes.length > 0)
                            return "FILTER · " + Picker.mimeTypes.join(", ") + (root.canAccept ? "" : " · " + root.validationMessage)
                        return root.canAccept ? "Ready" : root.validationMessage
                    }
                    elide: Text.ElideRight
                    color: root.canAccept
                        && !root.saveOverwriteRequired
                        && root.locationError === ""
                        && Picker.error === ""
                        ? Ryoku.inkMuted
                        : Ryoku.sun
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * root.u
                }
            }

            Row {
                id: actions
                anchors.right: parent.right
                anchors.rightMargin: 18 * root.u
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8 * root.u

                Rectangle {
                    width: cancelLabel.implicitWidth + 22 * root.u
                    height: 34 * root.u
                    radius: 6 * root.u
                    color: cancelHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        id: cancelLabel
                        anchors.centerIn: parent
                        text: "CANCEL"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.u
                        font.weight: Font.Medium
                        font.letterSpacing: 0.8
                    }
                    HoverHandler { id: cancelHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: Picker.cancel() }
                }

                Rectangle {
                    id: acceptButton
                    readonly property bool enabledState: root.canAccept && !Picker.overwriteConfirmationRequired
                    width: acceptLabel.implicitWidth + 24 * root.u
                    height: 34 * root.u
                    radius: 6 * root.u
                    color: enabledState ? Ryoku.bone : "transparent"
                    border.width: 1
                    border.color: enabledState ? Ryoku.bone : Ryoku.lineSoft
                    opacity: enabledState ? 1.0 : 0.45

                    Text {
                        id: acceptLabel
                        anchors.centerIn: parent
                        text: root.customAcceptLabel !== ""
                            ? root.customAcceptLabel
                            : (Picker.saveMode
                                ? "SAVE"
                                : (Picker.folderMode
                                    ? "SELECT THIS FOLDER"
                                    : (Picker.multiple && root.session && root.session.selectionCount > 1
                                        ? "OPEN " + root.session.selectionCount + " FILES"
                                        : "OPEN")))
                        color: acceptButton.enabledState ? Ryoku.inkOnBone : Ryoku.inkMuted
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.u
                        font.weight: Font.Medium
                        font.letterSpacing: 0.8
                    }
                    HoverHandler {
                        cursorShape: acceptButton.enabledState ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                    TapHandler {
                        enabled: acceptButton.enabledState
                        onTapped: root.acceptCurrent()
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: header.bottom
            anchors.leftMargin: 16 * root.u
            anchors.topMargin: 10 * root.u
            width: loadingLabel.implicitWidth + 18 * root.u
            height: 26 * root.u
            visible: root.files && root.files.loading
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
                font.pixelSize: 8 * root.u
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: Picker.overwriteConfirmationRequired
            z: 100
            color: "#66000000"

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
            }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48 * root.u, 540 * root.u)
                height: 190 * root.u
                radius: 8 * root.u
                color: Ryoku.paperLift
                border.width: 1
                border.color: Ryoku.line

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 20 * root.u
                    spacing: 10 * root.u

                    Text {
                        width: parent.width
                        text: "REPLACE EXISTING FILE?"
                        color: Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 13 * root.u
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: "A file already exists at this exact location. Replacing it cannot be authorized by pressing Save again."
                        wrapMode: Text.WordWrap
                        color: Ryoku.inkMuted
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10 * root.u
                    }

                    Text {
                        width: parent.width
                        text: Picker.pendingOverwritePath
                        elide: Text.ElideMiddle
                        color: Ryoku.inkDim
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.u
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 18 * root.u
                    spacing: 8 * root.u

                    Rectangle {
                        width: replaceCancelText.implicitWidth + 22 * root.u
                        height: 34 * root.u
                        radius: 6 * root.u
                        color: replaceCancelHover.hovered ? Ryoku.tint10 : "transparent"
                        border.width: 1
                        border.color: Ryoku.line

                        Text {
                            id: replaceCancelText
                            anchors.centerIn: parent
                            text: "CANCEL"
                            color: Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 9 * root.u
                            font.weight: Font.Medium
                            font.letterSpacing: 0.8
                        }
                        HoverHandler { id: replaceCancelHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: Picker.clearOverwriteConfirmation() }
                    }

                    Rectangle {
                        width: replaceText.implicitWidth + 24 * root.u
                        height: 34 * root.u
                        radius: 6 * root.u
                        color: Ryoku.bone
                        border.width: 1
                        border.color: Ryoku.bone

                        Text {
                            id: replaceText
                            anchors.centerIn: parent
                            text: "REPLACE"
                            color: Ryoku.inkOnBone
                            font.family: Ryoku.uiFont
                            font.pixelSize: 9 * root.u
                            font.weight: Font.Medium
                            font.letterSpacing: 0.8
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                if (root.session)
                                    Picker.confirmOverwrite(root.session.path, saveName.text)
                            }
                        }
                    }
                }
            }
        }
    }
}
