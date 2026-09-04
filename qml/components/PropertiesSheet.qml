// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var desktop
    property real uiScale: 1
    property var details: ({})
    property string folderSizeError: ""

    readonly property bool folderSizeMatches:
        desktop.folderSizeResult
        && desktop.folderSizeResult.path !== undefined
        && desktop.folderSizeResult.path === (root.details.path || "")
    readonly property bool folderSizeBusyHere:
        desktop.folderSizeBusy && root.folderSizeMatches
    readonly property string displayedSize: {
        if (root.details.isDirectory !== true)
            return root.details.sizeText || ""
        if (root.folderSizeBusyHere)
            return "Calculating…"
        if (root.folderSizeMatches && desktop.folderSizeResult.sizeText !== undefined)
            return desktop.folderSizeResult.sizeText
        return root.details.sizeText || "Not calculated"
    }
    readonly property string folderContents: {
        if (!root.folderSizeMatches || root.folderSizeBusyHere
                || desktop.folderSizeResult.files === undefined)
            return ""
        return desktop.folderSizeResult.files + " files · "
            + desktop.folderSizeResult.folders + " folders · "
            + desktop.folderSizeResult.links + " links"
    }

    visible: false
    anchors.fill: parent
    z: 1000

    function close() {
        desktop.cancelFolderSize()
        root.folderSizeError = ""
        root.visible = false
    }

    function openFor(path) {
        desktop.cancelFolderSize()
        root.folderSizeError = ""
        details = desktop.propertiesForPath(path)
        visible = Object.keys(details).length > 0
    }

    onVisibleChanged: {
        if (!visible)
            desktop.cancelFolderSize()
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.48)
        TapHandler { onTapped: root.close() }
    }

    Rectangle {
        width: Math.min(580 * root.uiScale, parent.width - 48 * root.uiScale)
        height: Math.min(640 * root.uiScale, parent.height - 64 * root.uiScale)
        anchors.centerIn: parent
        radius: 6 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong

        Column {
            anchors.fill: parent
            anchors.margins: 22 * root.uiScale
            spacing: 12 * root.uiScale

            Row {
                width: parent.width

                Text {
                    width: parent.width - close.width
                    text: "// PROPERTIES"
                    color: Ryoku.ink
                    font.family: Ryoku.monoFont
                    font.pixelSize: 11 * root.uiScale
                    font.letterSpacing: 1.2
                }

                Text {
                    id: close
                    text: "×"
                    color: closeHover.hovered ? Ryoku.ink : Ryoku.inkMuted
                    font.family: Ryoku.uiFont
                    font.pixelSize: 18 * root.uiScale
                    HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.close() }
                }
            }

            Text {
                width: parent.width
                text: root.details.name || ""
                elide: Text.ElideMiddle
                color: Ryoku.ink
                font.family: Ryoku.uiFont
                font.pixelSize: 19 * root.uiScale
                font.weight: Font.Medium
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Ryoku.line
            }

            ListView {
                width: parent.width
                height: parent.height - 154 * root.uiScale
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: [
                    { label: "TYPE", value: root.details.type || "" },
                    { label: "MIME", value: root.details.mime || "" },
                    { label: "LOCATION", value: root.details.parent || "" },
                    { label: "PATH", value: root.details.path || "" },
                    { label: "SIZE", value: root.displayedSize },
                    { label: "CONTENTS", value: root.folderContents },
                    { label: "MODIFIED", value: root.details.modified || "" },
                    { label: "CREATED", value: root.details.created || "" },
                    { label: "OWNER", value: root.details.owner || "" },
                    { label: "GROUP", value: root.details.group || "" },
                    { label: "PERMISSIONS", value: root.details.permissions || "" },
                    { label: "LINK TARGET", value: root.details.symlinkTarget || "" }
                ]

                delegate: Item {
                    id: propertyRow
                    required property var modelData

                    width: ListView.view.width
                    height: modelData.value !== "" ? 52 * root.uiScale : 0
                    visible: modelData.value !== ""

                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: 7 * root.uiScale
                        width: 110 * root.uiScale
                        text: propertyRow.modelData.label
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.uiScale
                        font.letterSpacing: 0.9
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 122 * root.uiScale
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 7 * root.uiScale
                        text: propertyRow.modelData.value
                        elide: Text.ElideMiddle
                        color: Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 11 * root.uiScale
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Ryoku.lineSoft
                    }
                }
            }

            Row {
                width: parent.width
                height: root.details.isDirectory === true ? 34 * root.uiScale : 0
                visible: root.details.isDirectory === true
                spacing: 10 * root.uiScale

                Rectangle {
                    id: calculateButton
                    width: calculateLabel.implicitWidth + 20 * root.uiScale
                    height: 30 * root.uiScale
                    radius: 6 * root.uiScale
                    color: calculateHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: root.folderSizeBusyHere ? Ryoku.sun : Ryoku.lineStrong

                    Text {
                        id: calculateLabel
                        anchors.centerIn: parent
                        text: root.folderSizeBusyHere ? "CANCEL" : "CALCULATE SIZE"
                        color: root.folderSizeBusyHere ? Ryoku.sun : Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.Medium
                        font.letterSpacing: 0.8
                    }

                    HoverHandler {
                        id: calculateHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: {
                            root.folderSizeError = ""
                            if (root.folderSizeBusyHere) {
                                desktop.cancelFolderSize()
                                return
                            }
                            if (!desktop.calculateFolderSize(root.details.path || ""))
                                root.folderSizeError = "Could not calculate this folder"
                        }
                    }
                }

                Text {
                    width: parent.width - calculateButton.width - 10 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.folderSizeError !== ""
                        ? root.folderSizeError
                        : (root.folderSizeBusyHere
                            ? "Scanning on demand — browsing remains unaffected."
                            : "Recursive size is only scanned when requested.")
                    wrapMode: Text.WordWrap
                    color: root.folderSizeError !== "" ? Ryoku.sun : Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.uiScale
                }
            }
        }
    }
}
