// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    required property var files
    required property var desktop
    property real uiScale: 1
    property bool expanded: false
    property var attachedSearch: null

    readonly property var search: root.session ? root.session.deepSearch : null
    readonly property bool active: root.expanded || (root.search && root.search.active)

    visible: active
    z: 200

    function attachCurrentSearch() {
        if (root.attachedSearch && root.attachedSearch !== root.search && root.attachedSearch.running)
            root.attachedSearch.cancel()
        root.attachedSearch = root.search
    }

    function open(queryText, runImmediately) {
        root.attachCurrentSearch()
        root.expanded = true
        field.text = queryText || (root.search ? root.search.query : "")
        Qt.callLater(function() {
            field.forceActiveFocus()
            field.selectAll()
            if (runImmediately && field.text.trim() !== "")
                root.runSearch()
        })
    }

    function runSearch() {
        if (!root.search || !root.session || !root.files)
            return

        var clean = field.text.trim()
        if (clean === "")
            return

        root.session.clearSelection()
        root.files.filterQuery = ""
        root.search.start(root.session.path, clean, root.files.showHidden)
        root.expanded = true
        field.focus = false
    }

    function close() {
        if (root.search)
            root.search.clear()
        field.text = ""
        field.focus = false
        root.expanded = false
    }

    onSessionChanged: {
        root.attachCurrentSearch()
        root.expanded = root.search && root.search.active
        field.text = root.search ? root.search.query : ""
    }

    Rectangle {
        anchors.fill: parent
        color: Ryoku.paper

        Column {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                width: parent.width
                height: 52 * root.uiScale
                color: Ryoku.paperLift

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Ryoku.line
                }

                Text {
                    id: searchMark
                    anchors.left: parent.left
                    anchors.leftMargin: 14 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: "⌕"
                    color: Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 16 * root.uiScale
                }

                Rectangle {
                    anchors.left: searchMark.right
                    anchors.leftMargin: 10 * root.uiScale
                    anchors.right: searchButton.left
                    anchors.rightMargin: 8 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    height: 34 * root.uiScale
                    radius: 6 * root.uiScale
                    color: "transparent"
                    border.width: field.activeFocus ? 2 : 1
                    border.color: field.activeFocus ? Ryoku.ink : Ryoku.line

                    TextInput {
                        id: field
                        anchors.fill: parent
                        anchors.leftMargin: 10 * root.uiScale
                        anchors.rightMargin: 10 * root.uiScale
                        verticalAlignment: Text.AlignVCenter
                        color: Ryoku.ink
                        selectionColor: Ryoku.bone
                        selectedTextColor: Ryoku.inkOnBone
                        font.family: Ryoku.uiFont
                        font.pixelSize: 11 * root.uiScale
                        selectByMouse: true
                        clip: true

                        Keys.onReturnPressed: function(event) {
                            root.runSearch()
                            event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            root.runSearch()
                            event.accepted = true
                        }
                        Keys.onEscapePressed: function(event) {
                            root.close()
                            event.accepted = true
                        }
                    }
                }

                Rectangle {
                    id: searchButton
                    anchors.right: cancelButton.left
                    anchors.rightMargin: 6 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    width: 70 * root.uiScale
                    height: 30 * root.uiScale
                    radius: 6 * root.uiScale
                    color: searchHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        anchors.centerIn: parent
                        text: root.search && root.search.running ? "RUNNING" : "SEARCH"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.Medium
                        font.letterSpacing: 0.8
                    }

                    HoverHandler {
                        id: searchHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler { onTapped: root.runSearch() }
                }

                Rectangle {
                    id: cancelButton
                    anchors.right: closeButton.left
                    anchors.rightMargin: 6 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    width: 62 * root.uiScale
                    height: 30 * root.uiScale
                    radius: 6 * root.uiScale
                    visible: root.search && root.search.running
                    color: cancelHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        anchors.centerIn: parent
                        text: "CANCEL"
                        color: Ryoku.inkMuted
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.Medium
                    }

                    HoverHandler {
                        id: cancelHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler { onTapped: if (root.search) root.search.cancel() }
                }

                Text {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.rightMargin: 14 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    color: closeHover.hovered ? Ryoku.ink : Ryoku.inkMuted
                    font.family: Ryoku.uiFont
                    font.pixelSize: 18 * root.uiScale

                    HoverHandler {
                        id: closeHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler { onTapped: root.close() }
                }
            }

            Rectangle {
                width: parent.width
                height: 34 * root.uiScale
                color: "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        if (!root.search)
                            return "// DEEP SEARCH"
                        if (root.search.running)
                            return "// " + root.search.count + " MATCHES · " + root.search.visitedCount + " VISITED"
                        if (root.search.cancelled)
                            return "// CANCELLED · " + root.search.count + " PARTIAL MATCHES"
                        if (root.search.active)
                            return "// " + root.search.count + " MATCHES · " + root.search.visitedCount + " VISITED"
                        return "// SEARCH BELOW CURRENT FOLDER"
                    }
                    color: Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.uiScale
                    font.letterSpacing: 0.7
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 14 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.search && root.search.truncated
                    text: "LIMIT REACHED · REFINE QUERY"
                    color: Ryoku.sun
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.uiScale
                }
            }

            Item {
                width: parent.width
                height: parent.height - 86 * root.uiScale

                Text {
                    anchors.centerIn: parent
                    visible: root.search && !root.search.running && root.search.active
                        && root.search.count === 0 && root.search.error === ""
                    text: "// NO MATCHES"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 11 * root.uiScale
                }

                Text {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40 * root.uiScale, 620 * root.uiScale)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: root.search && root.search.error !== ""
                    text: root.search ? root.search.error : ""
                    color: Ryoku.sun
                    font.family: Ryoku.uiFont
                    font.pixelSize: 11 * root.uiScale
                }

                ListView {
                    id: results
                    anchors.fill: parent
                    anchors.leftMargin: 10 * root.uiScale
                    anchors.rightMargin: 10 * root.uiScale
                    anchors.bottomMargin: 10 * root.uiScale
                    model: root.search
                    clip: true
                    spacing: 2 * root.uiScale
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: true

                    delegate: Rectangle {
                        id: resultRow
                        required property string name
                        required property string filePath
                        required property string parentPath
                        required property string relativePath
                        required property bool isDir

                        width: ListView.view.width
                        height: 58 * root.uiScale
                        radius: 6 * root.uiScale
                        color: resultHover.hovered ? Ryoku.tint5 : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10 * root.uiScale
                            anchors.top: parent.top
                            anchors.topMargin: 9 * root.uiScale
                            width: 22 * root.uiScale
                            text: resultRow.isDir ? "▰" : "·"
                            color: Ryoku.inkFaint
                            font.family: Ryoku.monoFont
                            font.pixelSize: 12 * root.uiScale
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 40 * root.uiScale
                            anchors.right: parent.right
                            anchors.rightMargin: 152 * root.uiScale
                            anchors.top: parent.top
                            anchors.topMargin: 7 * root.uiScale
                            text: resultRow.name
                            elide: Text.ElideMiddle
                            color: Ryoku.ink
                            font.family: Ryoku.uiFont
                            font.pixelSize: 12 * root.uiScale
                            font.weight: Font.Medium
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 40 * root.uiScale
                            anchors.right: parent.right
                            anchors.rightMargin: 152 * root.uiScale
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 7 * root.uiScale
                            text: resultRow.relativePath
                            elide: Text.ElideMiddle
                            color: Ryoku.inkFaint
                            font.family: Ryoku.monoFont
                            font.pixelSize: 9 * root.uiScale
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 8 * root.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 5 * root.uiScale

                            Repeater {
                                model: ["PARENT", "OPEN"]

                                delegate: Rectangle {
                                    id: action
                                    required property string modelData
                                    width: actionText.implicitWidth + 14 * root.uiScale
                                    height: 28 * root.uiScale
                                    radius: 6 * root.uiScale
                                    color: actionHover.hovered ? Ryoku.tint10 : "transparent"
                                    border.width: 1
                                    border.color: Ryoku.line

                                    Text {
                                        id: actionText
                                        anchors.centerIn: parent
                                        text: action.modelData
                                        color: Ryoku.inkDim
                                        font.family: Ryoku.uiFont
                                        font.pixelSize: 8 * root.uiScale
                                        font.weight: Font.Medium
                                    }

                                    HoverHandler {
                                        id: actionHover
                                        cursorShape: Qt.PointingHandCursor
                                    }

                                    TapHandler {
                                        onTapped: {
                                            if (action.modelData === "PARENT") {
                                                if (root.session.navigate(resultRow.parentPath))
                                                    root.close()
                                                return
                                            }

                                            if (resultRow.isDir) {
                                                if (root.session.navigate(resultRow.filePath))
                                                    root.close()
                                            } else if (root.desktop.openDefault(resultRow.filePath)) {
                                                root.close()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        HoverHandler { id: resultHover }
                    }
                }
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.visible && !field.activeFocus
        onActivated: root.close()
    }

    Connections {
        target: root.search
        function onSearchChanged() {
            if (root.search && field.text !== root.search.query)
                field.text = root.search.query
        }
    }

    Connections {
        target: root.session
        function onPathChanged() {
            if (root.visible)
                root.close()
        }
    }

    Component.onCompleted: root.attachCurrentSearch()
}
