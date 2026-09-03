// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

ListView {
    id: view

    required property var session
    required property var files
    property real uiScale: 1
    property bool compact: false

    property bool restoring: false
    property bool pointerSelection: false
    signal contextRequested(real sceneX, real sceneY, string path, bool isDirectory)

    clip: true
    model: files
    spacing: compact ? 0 : 1 * uiScale
    currentIndex: -1
    boundsBehavior: Flickable.StopAtBounds
    reuseItems: true

    function restoreState() {
        if (!session || !files || session.model !== files)
            return

        restoring = true
        if (files.loading)
            return

        Qt.callLater(function() {
            if (!session || !files || session.model !== files) {
                restoring = false
                return
            }

            var idx = files.indexOfPath(session.selectedPath)
            if (idx < 0 && files.count > 0)
                idx = 0

            currentIndex = idx
            var maxY = Math.max(0, contentHeight - height)
            contentY = Math.max(0, Math.min(session.scrollPosition, maxY))

            Qt.callLater(function() {
                restoring = false
            })
        })
    }

    onSessionChanged: {
        restoring = true
        restoreState()
    }
    onFilesChanged: {
        restoring = true
        restoreState()
    }

    onCurrentIndexChanged: {
        if (restoring || pointerSelection || !activeFocus || !session || !files ||
            files.loading || currentIndex < 0)
            return
        session.selectSingle(currentIndex)
    }

    onContentYChanged: {
        if (!restoring && session && files && !files.loading)
            session.scrollPosition = Math.max(0, contentY)
    }

    Connections {
        target: view.session
        function onPathChanged() { view.restoring = true }
    }

    Connections {
        target: view.files
        function onCountChanged() { view.restoreState() }
    }

    delegate: Rectangle {
        id: row

        required property int index
        required property string name
        required property string filePath
        required property bool isDir
        required property string sizeText
        required property string modifiedText

        readonly property bool selected: {
            var revision = view.session ? view.session.selectionRevision : 0
            return revision >= 0 && view.session && view.session.isSelectedPath(filePath)
        }

        width: view.width
        height: (view.compact ? 34 : 44) * view.uiScale
        radius: 6 * view.uiScale
        color: selected
            ? Ryoku.bone
            : (mouse.containsMouse ? Ryoku.tint5 : "transparent")

        Row {
            anchors.fill: parent
            anchors.leftMargin: 12 * view.uiScale
            anchors.rightMargin: 12 * view.uiScale
            spacing: 12 * view.uiScale

            Text {
                width: 22 * view.uiScale
                anchors.verticalCenter: parent.verticalCenter
                text: row.isDir ? "▰" : "·"
                color: row.selected ? Ryoku.inkOnBoneDim : Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 13 * view.uiScale
            }

            Text {
                width: view.compact
                    ? Math.max(100, parent.width - (22 + 12 + 105) * view.uiScale)
                    : Math.max(120, parent.width - (22 + 12 + 118 + 12 + 154) * view.uiScale)
                anchors.verticalCenter: parent.verticalCenter
                text: row.name
                elide: Text.ElideMiddle
                color: row.selected ? Ryoku.inkOnBone : Ryoku.ink
                font.family: Ryoku.uiFont
                font.pixelSize: (view.compact ? 12 : 13) * view.uiScale
            }

            Text {
                width: (view.compact ? 105 : 118) * view.uiScale
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                text: row.isDir ? "DIR" : row.sizeText
                color: row.selected ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 10 * view.uiScale
            }

            Text {
                visible: !view.compact
                width: 154 * view.uiScale
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                text: row.modifiedText
                color: row.selected ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 10 * view.uiScale
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onClicked: function(event) {
                view.pointerSelection = true
                view.currentIndex = row.index

                if (event.button === Qt.RightButton) {
                    if (!row.selected)
                        view.session.selectSingle(row.index)

                    var point = row.mapToItem(null, event.x, event.y)
                    view.contextRequested(point.x, point.y, row.filePath, row.isDir)
                    view.pointerSelection = false
                    view.forceActiveFocus()
                    return
                }

                if (event.modifiers & Qt.ShiftModifier)
                    view.session.selectRange(row.index)
                else if (event.modifiers & Qt.ControlModifier)
                    view.session.toggleSelection(row.index)
                else
                    view.session.selectSingle(row.index)

                view.pointerSelection = false
                view.forceActiveFocus()
            }

            onDoubleClicked: function(event) {
                view.currentIndex = row.index
                view.session.activate(row.index)
            }
        }
    }

    Keys.onReturnPressed: if (session) session.activate(currentIndex)
    Keys.onEnterPressed: if (session) session.activate(currentIndex)

    Component.onCompleted: {
        forceActiveFocus()
        restoreState()
    }
}
