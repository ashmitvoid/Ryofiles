// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    property real uiScale: 1
    property var actions: GitActions
    property string requestedPath: ""
    property bool requestedStaged: false
    property string operationId: ""

    visible: false
    anchors.fill: parent
    z: 980

    function openFor(path, staged) {
        if (!root.actions || !GitStatus.repository || GitStatus.rootPath === "" || path === "")
            return false

        root.requestedPath = path
        root.requestedStaged = staged
        root.visible = true
        root.operationId = root.actions.requestDiff(GitStatus.rootPath, path, staged)
        if (root.operationId === "") {
            root.visible = false
            return false
        }
        return true
    }

    function close() {
        if (root.actions && root.actions.busy && root.operationId !== "")
            root.actions.cancel()
        root.operationId = ""
        root.visible = false
    }

    Rectangle {
        anchors.fill: parent
        color: Ryoku.paper

        Column {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                width: parent.width
                height: 54 * root.uiScale
                color: Ryoku.paperLift

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Ryoku.line
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 16 * root.uiScale
                    anchors.right: actionsRow.left
                    anchors.rightMargin: 12 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2 * root.uiScale

                    Text {
                        text: root.requestedStaged ? "// GIT · STAGED DIFF" : "// GIT · WORKTREE DIFF"
                        color: Ryoku.ink
                        font.family: Ryoku.monoFont
                        font.pixelSize: 10 * root.uiScale
                        font.weight: Font.Medium
                        font.letterSpacing: 0.8
                    }

                    Text {
                        width: parent.width
                        text: root.requestedPath
                        elide: Text.ElideMiddle
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.uiScale
                    }
                }

                Row {
                    id: actionsRow
                    anchors.right: closeButton.left
                    anchors.rightMargin: 8 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6 * root.uiScale

                    Rectangle {
                        width: copyLabel.implicitWidth + 16 * root.uiScale
                        height: 30 * root.uiScale
                        radius: 6 * root.uiScale
                        enabled: root.actions && root.actions.diffText !== ""
                        opacity: enabled ? 1.0 : 0.4
                        color: copyHover.hovered && enabled ? Ryoku.tint10 : "transparent"
                        border.width: 1
                        border.color: enabled ? Ryoku.line : Ryoku.lineSoft

                        Text {
                            id: copyLabel
                            anchors.centerIn: parent
                            text: "COPY DIFF"
                            color: Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 9 * root.uiScale
                            font.weight: Font.Medium
                            font.letterSpacing: 0.7
                        }

                        HoverHandler {
                            id: copyHover
                            enabled: parent.enabled
                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler {
                            enabled: parent.enabled
                            onTapped: FileClipboard.copyText(root.actions.diffText)
                        }
                    }

                    Rectangle {
                        width: cancelLabel.implicitWidth + 16 * root.uiScale
                        height: 30 * root.uiScale
                        radius: 6 * root.uiScale
                        visible: root.actions && root.actions.busy
                        color: cancelHover.hovered ? Ryoku.tint10 : "transparent"
                        border.width: 1
                        border.color: Ryoku.line

                        Text {
                            id: cancelLabel
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
                        TapHandler { onTapped: if (root.actions) root.actions.cancel() }
                    }
                }

                Text {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.rightMargin: 16 * root.uiScale
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
                    anchors.leftMargin: 16 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        if (!root.actions)
                            return "// GIT ACTIONS UNAVAILABLE"
                        if (root.actions.busy)
                            return "// READING DIFF…"
                        if (root.actions.error !== "")
                            return "// DIFF FAILED"
                        if (root.actions.diffText === "")
                            return "// NO CHANGES ON THIS SIDE"
                        return "// " + root.actions.diffText.length + " CHARACTERS"
                    }
                    color: root.actions && root.actions.error !== "" ? Ryoku.sun : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.uiScale
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 16 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.actions && root.actions.diffTruncated
                    text: "512 KIB LIMIT · OUTPUT TRUNCATED"
                    color: Ryoku.sun
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.uiScale
                }
            }

            Item {
                width: parent.width
                height: parent.height - 88 * root.uiScale

                Text {
                    anchors.centerIn: parent
                    visible: root.actions && root.actions.busy
                    text: "// READING GIT DIFF"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 11 * root.uiScale
                }

                Text {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48 * root.uiScale, 720 * root.uiScale)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: root.actions && !root.actions.busy && root.actions.error !== ""
                    text: root.actions ? root.actions.error : ""
                    color: Ryoku.sun
                    font.family: Ryoku.uiFont
                    font.pixelSize: 11 * root.uiScale
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.actions && !root.actions.busy
                        && root.actions.error === "" && root.actions.diffText === ""
                    text: "// NO DIFF"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 11 * root.uiScale
                }

                Flickable {
                    id: diffScroll
                    anchors.fill: parent
                    anchors.margins: 12 * root.uiScale
                    visible: root.actions && !root.actions.busy
                        && root.actions.error === "" && root.actions.diffText !== ""
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: width
                    contentHeight: Math.max(height, diffText.implicitHeight + 24 * root.uiScale)

                    TextEdit {
                        id: diffText
                        x: 12 * root.uiScale
                        y: 12 * root.uiScale
                        width: diffScroll.width - 24 * root.uiScale
                        height: Math.max(diffScroll.height - 24 * root.uiScale, implicitHeight)
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        text: root.actions ? root.actions.diffText : ""
                        textFormat: TextEdit.PlainText
                        color: Ryoku.inkDim
                        selectionColor: Ryoku.bone
                        selectedTextColor: Ryoku.inkOnBone
                        font.family: Ryoku.monoFont
                        font.pixelSize: 10 * root.uiScale
                        lineHeight: 1.25
                    }
                }
            }
        }
    }
}
