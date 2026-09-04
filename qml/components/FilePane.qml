// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    property real uiScale: 1
    property bool paneActive: true
    property int paneIndex: 0

    readonly property var files: root.session ? root.session.model : null

    signal activated(int paneIndex)
    signal contextRequested(real sceneX, real sceneY, string path, bool isDirectory, int paneIndex)

    clip: true

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: 6 * root.uiScale
        border.width: 1
        border.color: root.paneActive ? Ryoku.lineStrong : Ryoku.lineSoft
    }

    Rectangle {
        z: 90
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 2 * root.uiScale
        color: root.paneActive ? Ryoku.sun : "transparent"
    }

    Rectangle {
        id: paneMark
        z: 90
        anchors.top: parent.top
        anchors.topMargin: 6 * root.uiScale
        anchors.right: parent.right
        anchors.rightMargin: 8 * root.uiScale
        width: paneLabel.implicitWidth + 14 * root.uiScale
        height: 24 * root.uiScale
        radius: 6 * root.uiScale
        color: root.paneActive ? Ryoku.bone : Ryoku.paperLift
        border.width: 1
        border.color: root.paneActive ? Ryoku.bone : Ryoku.line

        Text {
            id: paneLabel
            anchors.centerIn: parent
            text: root.paneActive ? "ACTIVE" : "PANE " + (root.paneIndex + 1)
            color: root.paneActive ? Ryoku.inkOnBone : Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 8 * root.uiScale
            font.letterSpacing: 0.7
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.activated(root.paneIndex) }
    }

    Component {
        id: compactView
        FileListView {
            session: root.session
            files: root.files
            uiScale: root.uiScale
            compact: true
            paneActive: root.paneActive
            onPaneActivated: root.activated(root.paneIndex)
            onContextRequested: function(sceneX, sceneY, path, isDirectory) {
                root.contextRequested(sceneX, sceneY, path, isDirectory, root.paneIndex)
            }
        }
    }

    Component {
        id: gridView
        FileGridView {
            session: root.session
            files: root.files
            uiScale: root.uiScale
            paneActive: root.paneActive
            onPaneActivated: root.activated(root.paneIndex)
            onContextRequested: function(sceneX, sceneY, path, isDirectory) {
                root.contextRequested(sceneX, sceneY, path, isDirectory, root.paneIndex)
            }
        }
    }

    Component {
        id: detailsView
        FileListView {
            session: root.session
            files: root.files
            uiScale: root.uiScale
            compact: false
            paneActive: root.paneActive
            onPaneActivated: root.activated(root.paneIndex)
            onContextRequested: function(sceneX, sceneY, path, isDirectory) {
                root.contextRequested(sceneX, sceneY, path, isDirectory, root.paneIndex)
            }
        }
    }

    Loader {
        anchors.fill: parent
        anchors.margins: 2 * root.uiScale
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
        font.pixelSize: 11 * root.uiScale
        lineHeight: 1.7
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: 8 * root.uiScale
        anchors.topMargin: 8 * root.uiScale
        width: loadingLabel.implicitWidth + 18 * root.uiScale
        height: 26 * root.uiScale
        visible: root.files && root.files.loading
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.line

        Text {
            id: loadingLabel
            anchors.centerIn: parent
            text: "READING…"
            color: Ryoku.inkMuted
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * root.uiScale
            font.letterSpacing: 1.1
        }
    }
}
