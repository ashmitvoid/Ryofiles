// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var desktop
    property real uiScale: 1
    property string targetPath: ""
    property var applications: []

    visible: false
    anchors.fill: parent
    z: 1000

    signal cancelled()

    function openFor(path) {
        targetPath = path
        applications = desktop.applicationsForPath(path)
        visible = true
    }

    Connections {
        target: root.desktop
        function onApplicationsReadyChanged() {
            if (root.visible)
                root.applications = root.desktop.applicationsForPath(root.targetPath)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.48)
        TapHandler {
            onTapped: {
                root.visible = false
                root.cancelled()
            }
        }
    }

    Rectangle {
        width: Math.min(520 * root.uiScale, parent.width - 48 * root.uiScale)
        height: Math.min(540 * root.uiScale, parent.height - 64 * root.uiScale)
        anchors.centerIn: parent
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong

        Column {
            anchors.fill: parent
            anchors.margins: 20 * root.uiScale
            spacing: 12 * root.uiScale

            Text {
                text: "// OPEN WITH"
                color: Ryoku.ink
                font.family: Ryoku.monoFont
                font.pixelSize: 11 * root.uiScale
                font.letterSpacing: 1.2
            }

            Text {
                width: parent.width
                text: root.targetPath
                elide: Text.ElideMiddle
                color: Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Ryoku.line
            }

            Item {
                width: parent.width
                height: parent.height - 108 * root.uiScale

                Text {
                    anchors.centerIn: parent
                    visible: !root.desktop.applicationsReady
                    text: "// DISCOVERING APPLICATIONS…"
                    color: Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 10 * root.uiScale
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.desktop.applicationsReady
                        && root.applications.length === 0
                    text: "// NO COMPATIBLE APPLICATIONS"
                    color: Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 10 * root.uiScale
                }

                ListView {
                    anchors.fill: parent
                    visible: root.desktop.applicationsReady
                        && root.applications.length > 0
                    model: root.applications
                    clip: true
                    spacing: 2 * root.uiScale
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        id: appRow
                        required property var modelData

                        width: ListView.view.width
                        height: 38 * root.uiScale
                        radius: 6 * root.uiScale
                        color: appHover.hovered ? Ryoku.tint10 : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10 * root.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 20 * root.uiScale
                            text: appRow.modelData.name
                            elide: Text.ElideRight
                            color: Ryoku.ink
                            font.family: Ryoku.uiFont
                            font.pixelSize: 12 * root.uiScale
                        }

                        HoverHandler {
                            id: appHover
                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler {
                            onTapped: {
                                if (!root.desktop.openWith(appRow.modelData.id, root.targetPath))
                                    return
                                root.visible = false
                            }
                        }
                    }
                }
            }
        }
    }
}
