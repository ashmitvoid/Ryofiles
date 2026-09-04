// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    property real uiScale: 1

    z: 45
    enabled: false

    function syncPath() {
        if (root.session)
            GitStatus.path = root.session.path
        else
            GitStatus.path = ""
    }

    GitBranchChip {
        anchors.left: parent.left
        anchors.leftMargin: 8 * root.uiScale
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8 * root.uiScale
        uiScale: root.uiScale
    }

    Connections {
        target: root.session
        function onPathChanged() { root.syncPath() }
    }

    onSessionChanged: root.syncPath()

    Component.onCompleted: root.syncPath()
    Component.onDestruction: {
        if (root.session && GitStatus.path === root.session.path)
            GitStatus.path = ""
    }
}
