// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    required property var files
    property real uiScale: 1
    property bool expanded: false
    property bool gitAwarenessActive: true

    readonly property bool active: expanded || (files && files.filterQuery !== "")

    signal deepSearchRequested(string query)

    height: active ? 42 * uiScale : 0
    visible: height > 0
    clip: true

    function open() {
        if (!files)
            return
        expanded = true
        field.text = files.filterQuery
        Qt.callLater(function() {
            field.forceActiveFocus()
            field.selectAll()
        })
    }

    function clearAndClose() {
        if (files && files.filterQuery !== "")
            files.filterQuery = ""
        if (session)
            session.clearSelection()
        field.text = ""
        field.focus = false
        expanded = false
    }

    onDeepSearchRequested: function(query) {
        deepPanel.open(query, query && query.trim() !== "")
    }

    Shortcut {
        sequence: "Ctrl+F"
        enabled: root.gitAwarenessActive
        onActivated: root.open()
    }

    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: root.gitAwarenessActive
        onActivated: root.deepSearchRequested(field.text)
    }

    Rectangle {
        anchors.fill: parent
        color: Ryoku.paper

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Ryoku.line
        }

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 4 * root.uiScale
            anchors.right: deepButton.left
            anchors.rightMargin: 8 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            height: 30 * root.uiScale
            radius: 6 * root.uiScale
            color: "transparent"
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? Ryoku.ink : Ryoku.line

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 9 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                text: "⌕"
                color: Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 13 * root.uiScale
            }

            TextInput {
                id: field
                anchors.left: parent.left
                anchors.leftMargin: 30 * root.uiScale
                anchors.right: countLabel.left
                anchors.rightMargin: 8 * root.uiScale
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                verticalAlignment: Text.AlignVCenter
                color: Ryoku.ink
                selectionColor: Ryoku.bone
                selectedTextColor: Ryoku.inkOnBone
                font.family: Ryoku.uiFont
                font.pixelSize: 11 * root.uiScale
                selectByMouse: true
                clip: true

                onTextEdited: {
                    if (!root.files)
                        return
                    root.files.filterQuery = text
                    if (root.session)
                        root.session.clearSelection()
                }

                Keys.onEscapePressed: function(event) {
                    root.clearAndClose()
                    event.accepted = true
                }

                Keys.onReturnPressed: function(event) {
                    focus = false
                    event.accepted = true
                }
            }

            Text {
                id: countLabel
                anchors.right: parent.right
                anchors.rightMargin: 9 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                text: root.files ? root.files.count + " MATCH" + (root.files.count === 1 ? "" : "ES") : ""
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.uiScale
            }
        }

        Rectangle {
            id: deepButton
            anchors.right: closeButton.left
            anchors.rightMargin: 8 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            width: 52 * root.uiScale
            height: 28 * root.uiScale
            radius: 6 * root.uiScale
            color: deepHover.hovered ? Ryoku.tint10 : "transparent"
            border.width: 1
            border.color: Ryoku.line

            Text {
                anchors.centerIn: parent
                text: "DEEP"
                color: Ryoku.inkDim
                font.family: Ryoku.uiFont
                font.pixelSize: 8 * root.uiScale
                font.weight: Font.Medium
                font.letterSpacing: 0.8
            }

            HoverHandler {
                id: deepHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler { onTapped: root.deepSearchRequested(field.text) }
        }

        Text {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: 5 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            text: "×"
            color: closeHover.hovered ? Ryoku.ink : Ryoku.inkMuted
            font.family: Ryoku.uiFont
            font.pixelSize: 16 * root.uiScale

            HoverHandler {
                id: closeHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler { onTapped: root.clearAndClose() }
        }
    }

    DeepSearchPanel {
        id: deepPanel
        parent: root.parent
        anchors.fill: parent
        session: root.session
        files: root.files
        desktop: Desktop
        uiScale: root.uiScale
    }

    GitAwarenessOverlay {
        parent: root.parent
        anchors.fill: parent
        session: root.session
        uiScale: root.uiScale
        active: root.gitAwarenessActive
    }

    Connections {
        target: root.files

        function onFilterQueryChanged() {
            if (!root.files)
                return

            if (field.text !== root.files.filterQuery)
                field.text = root.files.filterQuery

            if (root.files.filterQuery === "" && !field.activeFocus)
                root.expanded = false
        }
    }
}
