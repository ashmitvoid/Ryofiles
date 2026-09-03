// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

GridView {
    id: view

    required property var session
    required property var files
    property real uiScale: 1

    property bool restoring: false
    property bool pointerSelection: false
    signal contextRequested(real sceneX, real sceneY, string path, bool isDirectory)

    clip: true
    model: files
    cellWidth: 158 * uiScale
    cellHeight: 126 * uiScale
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
        id: tile

        required property int index
        required property string name
        required property string filePath
        required property bool isDir
        required property string sizeText

        readonly property bool selected: {
            var revision = view.session ? view.session.selectionRevision : 0
            return revision >= 0 && view.session && view.session.isSelectedPath(filePath)
        }

        width: view.cellWidth - 8 * view.uiScale
        height: view.cellHeight - 8 * view.uiScale
        radius: 6 * view.uiScale
        color: selected
            ? Ryoku.bone
            : (mouse.containsMouse ? Ryoku.tint5 : "transparent")
        border.width: selected ? 1 : 0
        border.color: selected ? Ryoku.bone : "transparent"

        Column {
            anchors.fill: parent
            anchors.margins: 10 * view.uiScale
            spacing: 6 * view.uiScale

            Item {
                width: parent.width
                height: 56 * view.uiScale

                Text {
                    anchors.centerIn: parent
                    text: tile.isDir ? "▰" : "□"
                    color: tile.selected ? Ryoku.inkOnBoneDim : Ryoku.inkDim
                    font.family: Ryoku.monoFont
                    font.pixelSize: 30 * view.uiScale
                }
            }

            Text {
                width: parent.width
                text: tile.name
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
                color: tile.selected ? Ryoku.inkOnBone : Ryoku.ink
                font.family: Ryoku.uiFont
                font.pixelSize: 12 * view.uiScale
                font.weight: Font.Medium
            }

            Text {
                width: parent.width
                text: tile.isDir ? "DIR" : tile.sizeText
                horizontalAlignment: Text.AlignHCenter
                color: tile.selected ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * view.uiScale
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onClicked: function(event) {
                view.pointerSelection = true
                view.currentIndex = tile.index

                if (event.button === Qt.RightButton) {
                    if (!tile.selected)
                        view.session.selectSingle(tile.index)

                    var point = tile.mapToItem(null, event.x, event.y)
                    view.contextRequested(point.x, point.y, tile.filePath, tile.isDir)
                    view.pointerSelection = false
                    view.forceActiveFocus()
                    return
                }

                if (event.modifiers & Qt.ShiftModifier)
                    view.session.selectRange(tile.index)
                else if (event.modifiers & Qt.ControlModifier)
                    view.session.toggleSelection(tile.index)
                else
                    view.session.selectSingle(tile.index)

                view.pointerSelection = false
                view.forceActiveFocus()
            }

            onDoubleClicked: function(event) {
                view.currentIndex = tile.index
                view.session.activate(tile.index)
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
