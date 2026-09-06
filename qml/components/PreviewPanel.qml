// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    required property var session
    required property var desktop
    required property var thumbnails
    property real uiScale: 1
    property var details: ({})

    function refreshDetails() {
        if (!root.visible || !session || session.selectionCount !== 1 || session.selectedPath === "") {
            details = ({})
            return
        }
        details = desktop.propertiesForPath(session.selectedPath)
    }

    function formatBytes(value) {
        var bytes = Number(value)
        if (!isFinite(bytes) || bytes < 0)
            return ""
        if (bytes < 1024)
            return Math.round(bytes) + " B"
        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(bytes < 10 * 1024 ? 1 : 0) + " KiB"
        if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(bytes < 10 * 1024 * 1024 ? 1 : 0) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function archiveKindMark(kind) {
        if (kind === "directory")
            return "▰"
        if (kind === "symlink")
            return "↗"
        if (kind === "hardlink")
            return "↔"
        if (kind === "file")
            return "□"
        return "·"
    }

    onSessionChanged: refreshDetails()
    onVisibleChanged: refreshDetails()

    Connections {
        target: root.session
        function onSelectionChanged() { root.refreshDetails() }
        function onPathChanged() { root.refreshDetails() }
    }

    Component.onCompleted: refreshDetails()

    ArchivePreviewLoader {
        id: archivePreview
        active: root.visible
            && root.session
            && root.session.selectionCount === 1
            && archivePreview.isCandidate(root.session.selectedPath)
        path: active ? root.session.selectedPath : ""
    }

    TextPreviewLoader {
        id: textPreview
        active: root.visible
            && root.session
            && root.session.selectionCount === 1
            && !root.thumbnails.isCandidate(root.session.selectedPath)
            && !archivePreview.isCandidate(root.session.selectedPath)
        path: active ? root.session.selectedPath : ""
    }

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Ryoku.line
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: 20 * root.uiScale
        anchors.rightMargin: 20 * root.uiScale
        anchors.topMargin: 18 * root.uiScale
        anchors.bottomMargin: 18 * root.uiScale
        spacing: 14 * root.uiScale

        Row {
            width: parent.width

            Text {
                width: parent.width - closeButton.width
                text: "// PREVIEW"
                color: Ryoku.ink
                font.family: Ryoku.monoFont
                font.pixelSize: 10 * root.uiScale
                font.letterSpacing: 1.2
            }

            Text {
                id: closeButton
                text: "×"
                color: closeHover.hovered ? Ryoku.ink : Ryoku.inkMuted
                font.family: Ryoku.uiFont
                font.pixelSize: 17 * root.uiScale

                HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: if (root.session) root.session.previewVisible = false }
            }
        }

        Rectangle {
            id: previewArea
            width: parent.width
            height: Math.min(parent.width, 270 * root.uiScale)
            radius: 6 * root.uiScale
            color: Ryoku.tint5
            border.width: 1
            border.color: Ryoku.line
            clip: true

            Image {
                id: previewImage
                anchors.fill: parent
                anchors.margins: 8 * root.uiScale
                visible: root.visible
                    && root.session
                    && root.session.selectionCount === 1
                    && root.thumbnails.isCandidate(root.session.selectedPath)
                source: visible
                    ? root.thumbnails.urlForPath(
                        root.session.selectedPath,
                        Math.round(720 * root.uiScale),
                        10)
                    : ""
                sourceSize.width: Math.round(720 * root.uiScale)
                sourceSize.height: Math.round(720 * root.uiScale)
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: false
                smooth: true
            }

            Flickable {
                id: textFlick
                anchors.fill: parent
                anchors.margins: 10 * root.uiScale
                visible: textPreview.supported
                clip: true
                contentWidth: width
                contentHeight: previewText.paintedHeight
                boundsBehavior: Flickable.StopAtBounds

                Text {
                    id: previewText
                    width: textFlick.width
                    text: textPreview.text
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    color: Ryoku.inkDim
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.uiScale
                    lineHeight: 1.25
                }
            }

            Flickable {
                id: archiveFlick
                anchors.fill: parent
                anchors.margins: 10 * root.uiScale
                visible: archivePreview.supported
                clip: true
                contentWidth: width
                contentHeight: archiveColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: archiveColumn
                    width: archiveFlick.width
                    spacing: 5 * root.uiScale

                    Text {
                        width: parent.width
                        text: archivePreview.formatName !== ""
                            ? "// " + archivePreview.formatName.toUpperCase()
                            : "// ARCHIVE"
                        elide: Text.ElideRight
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * root.uiScale
                        font.letterSpacing: 0.6
                    }

                    Repeater {
                        model: archivePreview.entries

                        delegate: Row {
                            id: archiveEntryRow
                            required property var modelData
                            width: archiveColumn.width
                            spacing: 7 * root.uiScale

                            Text {
                                width: 14 * root.uiScale
                                text: root.archiveKindMark(archiveEntryRow.modelData.kind)
                                color: Ryoku.inkFaint
                                font.family: Ryoku.monoFont
                                font.pixelSize: 9 * root.uiScale
                            }

                            Text {
                                width: Math.max(0, archiveEntryRow.width - 82 * root.uiScale)
                                text: archiveEntryRow.modelData.path
                                elide: Text.ElideMiddle
                                color: Ryoku.inkDim
                                font.family: Ryoku.monoFont
                                font.pixelSize: 8 * root.uiScale
                            }

                            Text {
                                width: 54 * root.uiScale
                                text: archiveEntryRow.modelData.sizeKnown
                                    ? root.formatBytes(archiveEntryRow.modelData.size)
                                    : ""
                                horizontalAlignment: Text.AlignRight
                                color: Ryoku.inkFaint
                                font.family: Ryoku.monoFont
                                font.pixelSize: 8 * root.uiScale
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 28 * root.uiScale
                visible: !textFlick.visible
                    && !archiveFlick.visible
                    && (!previewImage.visible || previewImage.status !== Image.Ready)
                text: {
                    if (!root.session || root.session.selectionCount === 0)
                        return "// NO SELECTION"
                    if (root.session.selectionCount > 1)
                        return root.session.selectionCount + " ITEMS SELECTED"
                    if (previewImage.visible && previewImage.status === Image.Loading)
                        return "// LOADING IMAGE…"
                    if (archivePreview.loading)
                        return "// READING ARCHIVE…"
                    if (archivePreview.isCandidate(root.session.selectedPath)
                            && archivePreview.error !== "")
                        return "// COULD NOT READ ARCHIVE"
                    if (textPreview.loading)
                        return "// READING TEXT…"
                    if (textPreview.error !== "")
                        return "// COULD NOT READ PREVIEW"
                    return root.details.isDirectory === true ? "▰" : "□"
                }
                horizontalAlignment: Text.AlignHCenter
                color: Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: root.details.isDirectory === true
                    ? 38 * root.uiScale
                    : 10 * root.uiScale
            }
        }

        Text {
            width: parent.width
            text: root.details.name || ""
            visible: text !== ""
            elide: Text.ElideMiddle
            color: Ryoku.ink
            font.family: Ryoku.uiFont
            font.pixelSize: 15 * root.uiScale
            font.weight: Font.Medium
        }

        Text {
            width: parent.width
            text: root.details.mime || root.details.type || ""
            visible: text !== ""
            elide: Text.ElideRight
            color: Ryoku.inkMuted
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * root.uiScale
        }

        Text {
            width: parent.width
            visible: archivePreview.supported
            text: {
                var listed = archivePreview.entries.length
                var suffix = archivePreview.truncated ? "+ ENTRIES — BOUNDED PREVIEW" : " ENTRIES"
                return "// " + listed + suffix
            }
            wrapMode: Text.WordWrap
            color: Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 8 * root.uiScale
        }

        Text {
            width: parent.width
            visible: archivePreview.isCandidate(root.session ? root.session.selectedPath : "")
                && archivePreview.error !== ""
            text: "// " + archivePreview.error
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            color: Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 8 * root.uiScale
        }

        Text {
            width: parent.width
            visible: textPreview.supported && textPreview.truncated
            text: "// TEXT PREVIEW TRUNCATED — 192 KiB / 2,500-line limit"
            wrapMode: Text.WordWrap
            color: Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 8 * root.uiScale
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Ryoku.line
            visible: root.session && root.session.selectionCount === 1
        }

        Column {
            width: parent.width
            spacing: 8 * root.uiScale
            visible: root.session && root.session.selectionCount === 1

            Repeater {
                model: [
                    { label: "SIZE", value: root.details.sizeText || "" },
                    { label: "MODIFIED", value: root.details.modified || "" },
                    { label: "OWNER", value: root.details.owner || "" },
                    { label: "PERMS", value: root.details.permissions || "" }
                ]

                delegate: Row {
                    id: metadataRow
                    required property var modelData
                    width: parent.width
                    visible: modelData.value !== ""

                    Text {
                        width: 78 * root.uiScale
                        text: metadataRow.modelData.label
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * root.uiScale
                        font.letterSpacing: 0.8
                    }

                    Text {
                        width: parent.width - 78 * root.uiScale
                        text: metadataRow.modelData.value
                        elide: Text.ElideMiddle
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10 * root.uiScale
                    }
                }
            }
        }

        Text {
            width: parent.width
            visible: root.details.isDirectory === true
            text: "// Folder size remains on-demand; no recursive scan."
            wrapMode: Text.WordWrap
            color: Ryoku.inkFaint
            font.family: Ryoku.monoFont
            font.pixelSize: 8 * root.uiScale
        }
    }
}
