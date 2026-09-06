// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    required property var files
    property real uiScale: 1
    property bool expanded: false
    property bool paneActive: true

    readonly property bool active: expanded || (files && files.filterQuery !== "")
    readonly property bool remote: session && session.remote

    signal deepSearchRequested(string query)
    signal focusReturnRequested()

    height: active ? 42 * uiScale : 0
    visible: height > 0.5
    clip: true

    Behavior on height {
        enabled: !Ryoku.reduceMotion
        NumberAnimation { duration: Ryoku.duration(140); easing.type: Easing.OutCubic }
    }

    function returnFocus() {
        Qt.callLater(function() {
            if (root.parent && root.parent.focusView)
                root.parent.focusView()
            else
                root.focusReturnRequested()
        })
    }

    function open() {
        if (!files) return
        expanded = true
        field.text = files.filterQuery
        Qt.callLater(function() {
            field.forceActiveFocus()
            field.selectAll()
        })
    }

    function clearAndClose() {
        if (files && files.filterQuery !== "") files.filterQuery = ""
        if (session) session.clearSelection()
        field.text = ""
        field.focus = false
        expanded = false
        root.returnFocus()
    }

    onDeepSearchRequested: function(query) {
        if (!root.remote) deepPanel.open(query, query && query.trim() !== "")
    }

    Shortcut { sequence: "Ctrl+F"; enabled: root.paneActive; onActivated: root.open() }
    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: root.paneActive && !root.remote
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
            color: Ryoku.lineSoft
        }

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 4 * root.uiScale
            anchors.right: deepButton.left
            anchors.rightMargin: 8 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            height: 30 * root.uiScale
            radius: 7 * root.uiScale
            color: field.activeFocus ? Ryoku.paperLift : Ryoku.tint5
            border.width: field.activeFocus ? 1 : 0
            border.color: Ryoku.lineStrong

            Behavior on color {
                enabled: !Ryoku.reduceMotion
                ColorAnimation { duration: Ryoku.duration(110) }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 9 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                text: "⌕"
                color: Ryoku.inkMuted
                font.family: Ryoku.uiFont
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
                    if (!root.files) return
                    root.files.filterQuery = text
                    if (root.session) root.session.clearSelection()
                }
                Keys.onEscapePressed: function(event) {
                    root.clearAndClose()
                    event.accepted = true
                }
                Keys.onReturnPressed: function(event) {
                    focus = false
                    root.returnFocus()
                    event.accepted = true
                }
            }

            Text {
                id: countLabel
                anchors.right: parent.right
                anchors.rightMargin: 9 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                text: root.files ? root.files.count + " match" + (root.files.count === 1 ? "" : "es") : ""
                color: Ryoku.inkFaint
                font.family: Ryoku.uiFont
                font.pixelSize: 8 * root.uiScale
            }
        }

        Rectangle {
            id: deepButton
            anchors.right: closeButton.left
            anchors.rightMargin: 7 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            width: 48 * root.uiScale
            height: 28 * root.uiScale
            radius: 7 * root.uiScale
            opacity: root.remote ? 0.38 : 1.0
            color: deepTap.pressed
                ? Ryoku.tint10
                : (deepHover.hovered && !root.remote ? Ryoku.tint5 : "transparent")

            Behavior on color {
                enabled: !Ryoku.reduceMotion
                ColorAnimation { duration: Ryoku.duration(90) }
            }

            Text {
                anchors.centerIn: parent
                text: "Deep"
                color: Ryoku.inkDim
                font.family: Ryoku.uiFont
                font.pixelSize: 8.5 * root.uiScale
                font.weight: Font.Medium
            }
            HoverHandler { id: deepHover; enabled: !root.remote; cursorShape: Qt.PointingHandCursor }
            TapHandler { id: deepTap; enabled: !root.remote; onTapped: root.deepSearchRequested(field.text) }
        }

        Rectangle {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: 4 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            width: 28 * root.uiScale
            height: 28 * root.uiScale
            radius: 7 * root.uiScale
            color: closeTap.pressed ? Ryoku.tint10 : (closeHover.hovered ? Ryoku.tint5 : "transparent")
            Text {
                anchors.centerIn: parent
                text: "×"
                color: Ryoku.inkMuted
                font.family: Ryoku.uiFont
                font.pixelSize: 15 * root.uiScale
            }
            HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { id: closeTap; onTapped: root.clearAndClose() }
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

    Connections {
        target: root.files
        function onFilterQueryChanged() {
            if (!root.files) return
            if (field.text !== root.files.filterQuery) field.text = root.files.filterQuery
            if (root.files.filterQuery === "" && !field.activeFocus) root.expanded = false
        }
    }

    Connections {
        target: root.session
        function onLocationKindChanged() {
            if (root.session && root.session.remote) deepPanel.close()
        }
    }
}
