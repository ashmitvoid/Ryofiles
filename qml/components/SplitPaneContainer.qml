// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var tabs
    property real uiScale: 1
    property real splitRatio: 0.5

    signal contextRequested(real sceneX, real sceneY, string path, bool isDirectory)

    readonly property real dividerWidth: 10 * root.uiScale
    readonly property real minimumPaneWidth: 240 * root.uiScale

    function activatePane(index) {
        if (!root.tabs)
            return
        root.tabs.activePane = index
    }

    function clampRatio(value) {
        if (!root.tabs || !root.tabs.split || root.width <= 0)
            return 1.0
        var available = Math.max(1, root.width - root.dividerWidth)
        var minimumRatio = Math.min(0.45, root.minimumPaneWidth / available)
        var maximumRatio = 1.0 - minimumRatio
        return Math.max(minimumRatio, Math.min(maximumRatio, value))
    }

    FilePane {
        id: primaryPane
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.tabs && root.tabs.split
            ? Math.max(0, (parent.width - root.dividerWidth) * root.clampRatio(root.splitRatio))
            : parent.width
        session: root.tabs ? root.tabs.primarySession : null
        uiScale: root.uiScale
        paneIndex: 0
        paneActive: !root.tabs || root.tabs.activePane === 0

        onActivated: index => root.activatePane(index)
        onContextRequested: function(sceneX, sceneY, path, isDirectory, paneIndex) {
            root.activatePane(paneIndex)
            root.contextRequested(sceneX, sceneY, path, isDirectory)
        }
    }

    Item {
        id: divider
        visible: root.tabs && root.tabs.split
        anchors.left: primaryPane.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.dividerWidth
        z: 100

        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: parent.height
            color: dividerMouse.containsMouse || dividerMouse.drag.active
                ? Ryoku.lineStrong
                : Ryoku.line
        }

        MouseArea {
            id: dividerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor

            property real pressSceneX: 0
            property real pressRatio: 0.5

            onPressed: function(mouse) {
                pressSceneX = mapToItem(root, mouse.x, mouse.y).x
                pressRatio = root.splitRatio
            }

            onPositionChanged: function(mouse) {
                if (!pressed || root.width <= root.dividerWidth)
                    return
                var sceneX = mapToItem(root, mouse.x, mouse.y).x
                var delta = sceneX - pressSceneX
                var available = root.width - root.dividerWidth
                root.splitRatio = root.clampRatio(pressRatio + delta / available)
            }
        }
    }

    FilePane {
        id: secondaryPane
        visible: root.tabs && root.tabs.split
        anchors.left: divider.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        session: root.tabs ? root.tabs.secondarySession : null
        uiScale: root.uiScale
        paneIndex: 1
        paneActive: root.tabs && root.tabs.activePane === 1

        onActivated: index => root.activatePane(index)
        onContextRequested: function(sceneX, sceneY, path, isDirectory, paneIndex) {
            root.activatePane(paneIndex)
            root.contextRequested(sceneX, sceneY, path, isDirectory)
        }
    }

    GitAwarenessOverlay {
        anchors.fill: parent
        session: root.tabs ? root.tabs.currentSession : null
        uiScale: root.uiScale
        active: root.tabs && root.tabs.currentSession !== null
    }

    Shortcut {
        sequence: "F6"
        enabled: root.tabs && root.tabs.split
        onActivated: root.tabs.activePane = root.tabs.activePane === 0 ? 1 : 0
    }

    Connections {
        target: root.tabs
        function onSplitChanged() {
            if (root.tabs && root.tabs.split)
                root.splitRatio = root.clampRatio(root.splitRatio)
            else
                root.splitRatio = 0.5
        }
    }

    onWidthChanged: {
        if (root.tabs && root.tabs.split)
            root.splitRatio = root.clampRatio(root.splitRatio)
    }
}
