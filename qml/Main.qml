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

    property string lastError: ""

    TabManager { id: tabs }

    Timer {
        id: errorClear
        interval: 4000
        onTriggered: root.lastError = ""
    }

    Connections {
        target: root.session

        function onErrorOccurred(message) {
            root.lastError = message
            errorClear.restart()
        }

        function onPathChanged() {
            location.text = root.session ? root.session.path : ""
        }
    }

    Connections {
        target: tabs
        function onCurrentSessionChanged() {
            location.text = root.session ? root.session.path : ""
        }
    }

    Shortcut { sequence: "Ctrl+T"; onActivated: tabs.newTab("") }
    Shortcut { sequence: "Ctrl+W"; onActivated: tabs.closeCurrentTab() }
    Shortcut { sequence: "Ctrl+Shift+T"; onActivated: tabs.reopenClosedTab() }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: tabs.nextTab() }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: tabs.previousTab() }

    Shortcut { sequence: "Alt+Left"; onActivated: if (root.session) root.session.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: if (root.session) root.session.goForward() }
    Shortcut { sequence: "Alt+Up"; onActivated: if (root.session) root.session.goUp() }

    Shortcut {
        sequence: "Ctrl+H"
        onActivated: if (root.files) root.files.showHidden = !root.files.showHidden
    }
    Shortcut { sequence: "Ctrl+R"; onActivated: if (root.session) root.session.refresh() }
    Shortcut { sequence: "Ctrl+L"; onActivated: location.forceActiveFocus() }

    Shortcut { sequence: "Ctrl+A"; onActivated: if (root.session) root.session.selectAll() }
    Shortcut { sequence: "Escape"; onActivated: if (root.session) root.session.clearSelection() }

    Shortcut { sequence: "Ctrl+1"; onActivated: if (root.session) root.session.viewMode = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: if (root.session) root.session.viewMode = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: if (root.session) root.session.viewMode = 2 }

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

            onNavigate: path => {
                if (root.session)
                    root.session.navigate(path)
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
                                if (!root.session)
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
                    anchors.right: viewModes.left
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
                        text: root.session ? root.session.path : ""
                        selectByMouse: true

                        onAccepted: {
                            if (root.session && !root.session.navigate(text))
                                text = root.session.path
                            focus = false
                        }
                    }
                }

                Row {
                    id: viewModes
                    anchors.right: hiddenBadge.left
                    anchors.rightMargin: 8 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4 * root.u

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
                    font.pixelSize: 11 * root.u
                    lineHeight: 1.7
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8 * root.u
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
                        : ((root.session && root.session.selectionCount > 0
                            ? root.session.selectionCount + " selected  // "
                            : "")
                           + (root.files ? root.files.count : 0) + " items"
                           + (root.files && root.files.showHidden ? "  // hidden visible" : "")
                           + "  // " + tabs.count + (tabs.count === 1 ? " tab" : " tabs"))
                    color: root.lastError !== "" ? Ryoku.sun : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 10 * root.u
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 20 * root.u
                    anchors.verticalCenter: parent.verticalCenter
                    text: "CTRL+A SELECT ALL   CTRL+1/2/3 VIEW   ESC CLEAR"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.u
                }
            }
        }
    }
}
