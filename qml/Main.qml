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
    property bool restoringView: false

    TabManager {
        id: tabs
    }

    function restoreViewState() {
        if (!root.session || !root.files)
            return

        root.restoringView = true
        Qt.callLater(function() {
            if (!root.session || !root.files) {
                root.restoringView = false
                return
            }

            var idx = root.files.indexOfPath(root.session.selectedPath)
            if (idx < 0 && root.files.count > 0)
                idx = 0

            list.currentIndex = idx

            var maxY = Math.max(0, list.contentHeight - list.height)
            list.contentY = Math.max(0, Math.min(root.session.scrollPosition, maxY))

            Qt.callLater(function() {
                root.restoringView = false
            })
        })
    }

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
            root.restoreViewState()
        }

        function onSelectedPathChanged() {
            if (!root.restoringView)
                root.restoreViewState()
        }
    }

    Connections {
        target: root.files
        function onCountChanged() { root.restoreViewState() }
    }

    Connections {
        target: tabs
        function onCurrentSessionChanged() {
            location.text = root.session ? root.session.path : ""
            root.restoreViewState()
        }
    }

    Shortcut {
        sequence: "Ctrl+T"
        onActivated: tabs.newTab("")
    }
    Shortcut {
        sequence: "Ctrl+W"
        onActivated: tabs.closeCurrentTab()
    }
    Shortcut {
        sequence: "Ctrl+Shift+T"
        onActivated: tabs.reopenClosedTab()
    }
    Shortcut {
        sequence: "Ctrl+Tab"
        onActivated: tabs.nextTab()
    }
    Shortcut {
        sequence: "Ctrl+Shift+Tab"
        onActivated: tabs.previousTab()
    }
    Shortcut {
        sequence: "Alt+Left"
        onActivated: if (root.session) root.session.goBack()
    }
    Shortcut {
        sequence: "Alt+Right"
        onActivated: if (root.session) root.session.goForward()
    }
    Shortcut {
        sequence: "Alt+Up"
        onActivated: if (root.session) root.session.goUp()
    }
    Shortcut {
        sequence: "Ctrl+H"
        onActivated: if (root.files) root.files.showHidden = !root.files.showHidden
    }
    Shortcut {
        sequence: "Ctrl+R"
        onActivated: if (root.session) root.session.refresh()
    }
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: location.forceActiveFocus()
    }

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
                    anchors.right: hiddenBadge.left
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

                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true
                    model: root.files
                    spacing: 1 * root.u
                    currentIndex: -1
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: true

                    onCurrentIndexChanged: {
                        if (root.restoringView || !root.session || !root.files || currentIndex < 0)
                            return
                        var selected = root.files.pathAt(currentIndex)
                        if (selected !== "")
                            root.session.selectedPath = selected
                    }

                    onContentYChanged: {
                        if (!root.restoringView && root.session)
                            root.session.scrollPosition = Math.max(0, contentY)
                    }

                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string name
                        required property string filePath
                        required property bool isDir
                        required property string sizeText
                        required property string modifiedText

                        width: list.width
                        height: 44 * root.u
                        radius: 6 * root.u
                        color: list.currentIndex === index
                            ? Ryoku.bone
                            : (rowHover.hovered ? Ryoku.tint5 : "transparent")

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12 * root.u
                            anchors.rightMargin: 12 * root.u
                            spacing: 12 * root.u

                            Text {
                                width: 22 * root.u
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.isDir ? "▰" : "·"
                                color: list.currentIndex === row.index
                                    ? Ryoku.inkOnBoneDim
                                    : Ryoku.inkFaint
                                font.family: Ryoku.monoFont
                                font.pixelSize: 13 * root.u
                            }

                            Text {
                                width: Math.max(120, parent.width - (22 + 12 + 118 + 12 + 154) * root.u)
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.name
                                elide: Text.ElideMiddle
                                color: list.currentIndex === row.index
                                    ? Ryoku.inkOnBone
                                    : Ryoku.ink
                                font.family: Ryoku.uiFont
                                font.pixelSize: 13 * root.u
                            }

                            Text {
                                width: 118 * root.u
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignRight
                                text: row.isDir ? "DIR" : row.sizeText
                                color: list.currentIndex === row.index
                                    ? Ryoku.inkOnBoneDim
                                    : Ryoku.inkMuted
                                font.family: Ryoku.monoFont
                                font.pixelSize: 10 * root.u
                            }

                            Text {
                                width: 154 * root.u
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignRight
                                text: row.modifiedText
                                color: list.currentIndex === row.index
                                    ? Ryoku.inkOnBoneDim
                                    : Ryoku.inkMuted
                                font.family: Ryoku.monoFont
                                font.pixelSize: 10 * root.u
                            }
                        }

                        HoverHandler {
                            id: rowHover
                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: list.currentIndex = row.index
                            onDoubleTapped: {
                                list.currentIndex = row.index
                                if (root.session)
                                    root.session.activate(row.index)
                            }
                        }
                    }

                    Keys.onReturnPressed: {
                        if (root.session)
                            root.session.activate(currentIndex)
                    }
                    Keys.onEnterPressed: {
                        if (root.session)
                            root.session.activate(currentIndex)
                    }

                    Component.onCompleted: {
                        forceActiveFocus()
                        root.restoreViewState()
                    }
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
                        : ((root.files ? root.files.count : 0) + " items"
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
                    text: "CTRL+T NEW   CTRL+W CLOSE   ALT+←/→ HISTORY"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.u
                }
            }
        }
    }
}
