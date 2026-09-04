// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    property real uiScale: 1
    property var commands: []
    property string query: ""
    property int selectedIndex: -1
    readonly property var matches: root.filteredCommands()

    signal commandTriggered(string commandId)

    visible: false
    anchors.fill: parent
    z: 1100

    function normalized(value) {
        return (value || "").toString().toLowerCase()
    }

    function filteredCommands() {
        var source = root.commands || []
        var clean = root.normalized(root.query).trim()
        var tokens = clean === "" ? [] : clean.split(/\s+/)
        var ranked = []

        for (var i = 0; i < source.length; ++i) {
            var command = source[i]
            var label = root.normalized(command.label)
            var detail = root.normalized(command.detail)
            var keywords = root.normalized(command.keywords)
            var shortcut = root.normalized(command.shortcut)
            var category = root.normalized(command.category)
            var haystack = label + " " + detail + " " + keywords + " " + shortcut + " " + category
            var matched = true

            for (var t = 0; t < tokens.length; ++t) {
                if (haystack.indexOf(tokens[t]) < 0) {
                    matched = false
                    break
                }
            }
            if (!matched)
                continue

            var score = 0
            if (clean !== "") {
                if (label === clean)
                    score += 300
                else if (label.indexOf(clean) === 0)
                    score += 180
                else if (label.indexOf(clean) >= 0)
                    score += 100
                if (keywords.indexOf(clean) >= 0)
                    score += 40
                if (shortcut.indexOf(clean) >= 0)
                    score += 20
            }
            if (command.enabled)
                score += 5

            ranked.push({ command: command, score: score, order: i })
        }

        ranked.sort(function(a, b) {
            if (a.score !== b.score)
                return b.score - a.score
            return a.order - b.order
        })

        var result = []
        var limit = Math.min(12, ranked.length)
        for (var r = 0; r < limit; ++r)
            result.push(ranked[r].command)
        return result
    }

    function firstEnabledIndex() {
        for (var i = 0; i < root.matches.length; ++i) {
            if (root.matches[i].enabled)
                return i
        }
        return root.matches.length > 0 ? 0 : -1
    }

    function normalizeSelection() {
        if (root.matches.length === 0) {
            root.selectedIndex = -1
            return
        }
        if (root.selectedIndex < 0 || root.selectedIndex >= root.matches.length
                || !root.matches[root.selectedIndex].enabled)
            root.selectedIndex = root.firstEnabledIndex()
        Qt.callLater(function() {
            if (root.selectedIndex >= 0)
                results.positionViewAtIndex(root.selectedIndex, ListView.Contain)
        })
    }

    function stepSelection(delta) {
        var count = root.matches.length
        if (count === 0)
            return

        var index = root.selectedIndex
        if (index < 0)
            index = delta > 0 ? -1 : 0

        for (var i = 0; i < count; ++i) {
            index = (index + delta + count) % count
            if (root.matches[index].enabled) {
                root.selectedIndex = index
                results.positionViewAtIndex(index, ListView.Contain)
                return
            }
        }
    }

    function activateSelected() {
        if (root.selectedIndex < 0 || root.selectedIndex >= root.matches.length)
            return
        var command = root.matches[root.selectedIndex]
        if (!command.enabled)
            return
        var id = command.id
        root.close()
        root.commandTriggered(id)
    }

    function open() {
        root.query = ""
        root.visible = true
        field.text = ""
        root.selectedIndex = root.firstEnabledIndex()
        Qt.callLater(function() {
            field.forceActiveFocus()
            root.normalizeSelection()
        })
    }

    function close() {
        field.focus = false
        root.visible = false
        root.query = ""
        root.selectedIndex = -1
    }

    onMatchesChanged: root.normalizeSelection()

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.close()
    }

    Rectangle {
        id: plate
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 86 * root.uiScale
        width: Math.min(720 * root.uiScale, root.width - 48 * root.uiScale)
        height: 76 * root.uiScale
            + Math.max(42 * root.uiScale, root.matches.length * 42 * root.uiScale)
            + 34 * root.uiScale
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong
        clip: true

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Rectangle {
            id: searchBand
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 76 * root.uiScale
            color: Ryoku.paperLift

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Ryoku.line
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 18 * root.uiScale
                anchors.top: parent.top
                anchors.topMargin: 12 * root.uiScale
                text: "// COMMAND"
                color: Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
                font.letterSpacing: 1.0
            }

            Text {
                id: searchMark
                anchors.left: parent.left
                anchors.leftMargin: 18 * root.uiScale
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12 * root.uiScale
                text: ">"
                color: Ryoku.ink
                font.family: Ryoku.monoFont
                font.pixelSize: 14 * root.uiScale
                font.weight: Font.Medium
            }

            TextInput {
                id: field
                anchors.left: searchMark.right
                anchors.leftMargin: 10 * root.uiScale
                anchors.right: hint.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8 * root.uiScale
                height: 30 * root.uiScale
                verticalAlignment: Text.AlignVCenter
                color: Ryoku.ink
                selectionColor: Ryoku.bone
                selectedTextColor: Ryoku.inkOnBone
                font.family: Ryoku.uiFont
                font.pixelSize: 13 * root.uiScale
                selectByMouse: true
                clip: true

                onTextChanged: root.query = text

                Keys.onUpPressed: function(event) {
                    root.stepSelection(-1)
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) {
                    root.stepSelection(1)
                    event.accepted = true
                }
                Keys.onReturnPressed: function(event) {
                    root.activateSelected()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    root.activateSelected()
                    event.accepted = true
                }
                Keys.onEscapePressed: function(event) {
                    root.close()
                    event.accepted = true
                }
            }

            Text {
                id: hint
                anchors.right: parent.right
                anchors.rightMargin: 18 * root.uiScale
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16 * root.uiScale
                text: root.matches.length + " / 12"
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.uiScale
            }
        }

        ListView {
            id: results
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: searchBand.bottom
            anchors.bottom: footer.top
            anchors.leftMargin: 8 * root.uiScale
            anchors.rightMargin: 8 * root.uiScale
            model: root.matches
            currentIndex: root.selectedIndex
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            spacing: 0

            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index

                width: ListView.view.width
                height: 42 * root.uiScale
                radius: 5 * root.uiScale
                color: root.selectedIndex === row.index
                    ? (row.modelData.enabled ? Ryoku.bone : Ryoku.tint5)
                    : (rowHover.hovered && row.modelData.enabled ? Ryoku.tint5 : "transparent")
                opacity: row.modelData.enabled ? 1.0 : 0.42

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    width: 62 * root.uiScale
                    text: row.modelData.category
                    color: root.selectedIndex === row.index && row.modelData.enabled
                        ? Ryoku.inkOnBoneDim
                        : Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.uiScale
                    font.letterSpacing: 0.8
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 80 * root.uiScale
                    anchors.right: shortcutLabel.left
                    anchors.rightMargin: 10 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1 * root.uiScale

                    Text {
                        width: parent.width
                        text: row.modelData.label
                        elide: Text.ElideRight
                        color: root.selectedIndex === row.index && row.modelData.enabled
                            ? Ryoku.inkOnBone
                            : Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 11 * root.uiScale
                        font.weight: Font.Medium
                    }

                    Text {
                        width: parent.width
                        text: row.modelData.detail || ""
                        visible: text !== ""
                        elide: Text.ElideRight
                        color: root.selectedIndex === row.index && row.modelData.enabled
                            ? Ryoku.inkOnBoneDim
                            : Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * root.uiScale
                    }
                }

                Text {
                    id: shortcutLabel
                    anchors.right: parent.right
                    anchors.rightMargin: 10 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.shortcut || ""
                    color: root.selectedIndex === row.index && row.modelData.enabled
                        ? Ryoku.inkOnBoneDim
                        : Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.uiScale
                }

                HoverHandler {
                    id: rowHover
                    enabled: row.modelData.enabled
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (hovered && row.modelData.enabled)
                            root.selectedIndex = row.index
                    }
                }

                TapHandler {
                    enabled: row.modelData.enabled
                    onTapped: {
                        root.selectedIndex = row.index
                        root.activateSelected()
                    }
                }
            }
        }

        Text {
            anchors.centerIn: results
            visible: root.matches.length === 0
            text: "// NO COMMANDS"
            color: Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 10 * root.uiScale
        }

        Rectangle {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 34 * root.uiScale
            color: Ryoku.paperLift

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Ryoku.line
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 18 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                text: "↑ ↓ NAVIGATE   ENTER RUN"
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.uiScale
                font.letterSpacing: 0.6
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 18 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                text: "ESC CLOSE"
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.uiScale
            }
        }
    }
}
