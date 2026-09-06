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

    readonly property int selectionCount: root.session ? root.session.selectionCount : 0
    readonly property string selectedPath:
        root.selectionCount === 1 && root.session ? root.session.selectedPath : ""
    readonly property string selectedName: root.baseName(root.selectedPath)
    readonly property bool singleSelection: root.selectionCount === 1 && root.selectedPath !== ""
    readonly property bool directorySelection: root.singleSelection && root.details.isDirectory === true
    readonly property string previewKind: {
        if (!root.singleSelection) return ""
        if (root.directorySelection) return "folder"
        if (imagePreview.isCandidate(root.selectedPath)) return "image"
        if (pdfPreview.isCandidate(root.selectedPath)) return "pdf"
        if (mediaPreviewBlock.mediaCandidate) return "media"
        if (mediaPreviewBlock.fontCandidate) return "font"
        if (archivePreview.isCandidate(root.selectedPath)) return "archive"
        return "text"
    }
    readonly property bool busy: {
        if (!root.singleSelection || root.directorySelection) return false
        if (root.previewKind === "image")
            return imagePreview.loading || previewImage.status === Image.Loading
        if (root.previewKind === "pdf")
            return pdfPreview.loading || pdfImage.status === Image.Loading
        if (root.previewKind === "media" || root.previewKind === "font")
            return mediaPreviewBlock.loading
        if (root.previewKind === "archive") return archivePreview.loading
        return textPreview.loading
    }
    readonly property string previewError: {
        if (!root.singleSelection || root.directorySelection) return ""
        if (root.previewKind === "image") {
            if (imagePreview.error !== "") return imagePreview.error
            if (!imagePreview.loading && previewImage.status === Image.Error)
                return "The image could not be decoded."
            return ""
        }
        if (root.previewKind === "pdf") return pdfPreview.error
        if (root.previewKind === "media" || root.previewKind === "font") return mediaPreviewBlock.error
        if (root.previewKind === "archive") return archivePreview.error
        return textPreview.error
    }
    readonly property bool ready: {
        if (!root.singleSelection || root.directorySelection) return false
        if (root.previewKind === "image")
            return previewImage.status === Image.Ready || animationImage.source !== ""
        if (root.previewKind === "pdf") return pdfPreview.supported && pdfImage.status === Image.Ready
        if (root.previewKind === "media" || root.previewKind === "font") return mediaPreviewBlock.supported
        if (root.previewKind === "archive") return archivePreview.supported
        return textPreview.supported
    }
    readonly property bool limitError: {
        var message = root.previewError.toLowerCase()
        return message.indexOf("limit") >= 0
            || message.indexOf("exceed") >= 0
            || message.indexOf("too large") >= 0
            || message.indexOf("maximum") >= 0
    }
    readonly property bool showStateCard:
        !root.singleSelection
        || root.directorySelection
        || (!root.ready && (root.busy || root.previewError !== "" || !root.busy))

    function baseName(path) {
        if (!path || path === "") return ""
        var clean = path.endsWith("/") && path.length > 1 ? path.slice(0, -1) : path
        var slash = clean.lastIndexOf("/")
        return slash >= 0 ? clean.substring(slash + 1) : clean
    }

    function refreshDetails() {
        if (!root.visible || !root.singleSelection) {
            details = ({})
            return
        }
        details = desktop.propertiesForPath(root.selectedPath)
    }

    function resetImageView() {
        imageFlick.zoom = 1.0
        imageFlick.contentX = 0
        imageFlick.contentY = 0
        imageFlick.returnToBounds()
    }

    function setImageZoom(value) {
        var bounded = Math.max(1.0, Math.min(4.0, value))
        if (Math.abs(imageFlick.zoom - bounded) < 0.001) return
        var oldWidth = Math.max(1, imageFlick.contentWidth)
        var oldHeight = Math.max(1, imageFlick.contentHeight)
        var centerX = (imageFlick.contentX + imageFlick.width / 2) / oldWidth
        var centerY = (imageFlick.contentY + imageFlick.height / 2) / oldHeight
        imageFlick.zoom = bounded
        imageFlick.contentX = centerX * imageFlick.contentWidth - imageFlick.width / 2
        imageFlick.contentY = centerY * imageFlick.contentHeight - imageFlick.height / 2
        imageFlick.returnToBounds()
    }

    function formatBytes(value) {
        var bytes = Number(value)
        if (!isFinite(bytes) || bytes < 0) return ""
        if (bytes < 1024) return Math.round(bytes) + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(bytes < 10 * 1024 ? 1 : 0) + " KiB"
        if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(bytes < 10 * 1024 * 1024 ? 1 : 0) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function stateTitle() {
        if (root.selectionCount === 0) return "Select a file"
        if (root.selectionCount > 1) return root.selectionCount + " items selected"
        if (root.directorySelection) return "Folder selected"
        if (root.busy) return "Loading preview"
        if (root.previewError !== "") return root.limitError ? "Preview limit reached" : "Preview unavailable"
        return "No preview available"
    }

    function stateDetail() {
        if (root.selectionCount === 0) return "Choose an item to inspect it here."
        if (root.selectionCount > 1) return "Preview is available for one selected item at a time."
        if (root.directorySelection) return "Folder contents are never recursively scanned just to produce a preview."
        if (root.busy) return "Work is bounded and cancelled automatically when the selection changes."
        if (root.previewError !== "") return root.previewError
        return "This file type does not have a visual preview. Properties remain available below."
    }

    onSessionChanged: {
        refreshDetails()
        resetImageView()
    }
    onSelectedPathChanged: {
        refreshDetails()
        resetImageView()
        imageAnimation.stop()
        panelScroll.contentY = 0
    }
    onVisibleChanged: {
        refreshDetails()
        if (!visible) imageAnimation.stop()
    }

    Connections {
        target: root.session
        function onSelectionChanged() { root.refreshDetails() }
        function onPathChanged() {
            root.refreshDetails()
            root.resetImageView()
            imageAnimation.stop()
        }
    }

    Component.onCompleted: refreshDetails()

    ArchivePreviewLoader {
        id: archivePreview
        active: root.visible && root.singleSelection && archivePreview.isCandidate(root.selectedPath)
        path: active ? root.selectedPath : ""
    }

    PdfPreviewLoader {
        id: pdfPreview
        active: root.visible && root.singleSelection && pdfPreview.isCandidate(root.selectedPath)
        path: active ? root.selectedPath : ""
    }

    ImagePreviewLoader {
        id: imagePreview
        active: root.visible && root.singleSelection && imagePreview.isCandidate(root.selectedPath)
        path: active ? root.selectedPath : ""
    }

    ImageAnimationController {
        id: imageAnimation
        active: root.visible
            && root.singleSelection
            && imagePreview.animationSupported
            && !Ryoku.reduceMotion
        path: active ? root.selectedPath : ""
    }

    TextPreviewLoader {
        id: textPreview
        active: root.visible
            && root.singleSelection
            && !root.directorySelection
            && !root.thumbnails.isCandidate(root.selectedPath)
            && !archivePreview.isCandidate(root.selectedPath)
            && !pdfPreview.isCandidate(root.selectedPath)
            && !mediaPreviewBlock.candidate
        path: active ? root.selectedPath : ""
    }

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Ryoku.lineSoft
    }

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 52 * root.uiScale

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 16 * root.uiScale
            anchors.right: closeButton.left
            anchors.rightMargin: 10 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1 * root.uiScale

            Text {
                width: parent.width
                text: root.selectedName !== "" ? root.selectedName : "Preview"
                elide: Text.ElideMiddle
                color: Ryoku.ink
                font.family: Ryoku.uiFont
                font.pixelSize: 11.5 * root.uiScale
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: root.singleSelection
                    ? (root.details.mime || root.details.type || root.previewKind)
                    : "Quick inspection"
                elide: Text.ElideRight
                color: Ryoku.inkFaint
                font.family: Ryoku.uiFont
                font.pixelSize: 8 * root.uiScale
            }
        }

        Rectangle {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: 12 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            width: 28 * root.uiScale
            height: 28 * root.uiScale
            radius: 7 * root.uiScale
            color: closeHover.hovered ? Ryoku.tint10 : "transparent"
            Text {
                anchors.centerIn: parent
                text: "×"
                color: Ryoku.inkMuted
                font.family: Ryoku.uiFont
                font.pixelSize: 15 * root.uiScale
            }
            HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: if (root.session) root.session.previewVisible = false }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Ryoku.lineSoft
        }
    }

    Flickable {
        id: panelScroll
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: panelColumn.implicitHeight + 28 * root.uiScale
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: panelColumn
            x: 16 * root.uiScale
            y: 14 * root.uiScale
            width: panelScroll.width - 32 * root.uiScale
            spacing: 12 * root.uiScale

            Rectangle {
                id: previewArea
                width: parent.width
                height: Math.min(parent.width * 0.82, 286 * root.uiScale)
                radius: 10 * root.uiScale
                color: Ryoku.tint5
                border.width: 1
                border.color: Ryoku.lineSoft
                clip: true

                Flickable {
                    id: imageFlick
                    property real zoom: 1.0
                    anchors.fill: parent
                    anchors.margins: 8 * root.uiScale
                    visible: root.previewKind === "image"
                        && root.singleSelection
                        && imageAnimation.frameSource === ""
                    clip: true
                    interactive: zoom > 1.001
                    contentWidth: width * zoom
                    contentHeight: height * zoom
                    boundsBehavior: Flickable.StopAtBounds

                    Image {
                        id: previewImage
                        width: imageFlick.contentWidth
                        height: imageFlick.contentHeight
                        source: imageFlick.visible
                            ? root.thumbnails.urlForPath(
                                root.selectedPath,
                                Math.round(1024 * root.uiScale),
                                10)
                            : ""
                        sourceSize.width: Math.round(1024 * root.uiScale)
                        sourceSize.height: Math.round(1024 * root.uiScale)
                        fillMode: Image.PreserveAspectFit
                        cache: true
                        asynchronous: true
                        smooth: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        onWheel: function(wheel) {
                            if ((wheel.modifiers & Qt.ControlModifier) === 0) {
                                wheel.accepted = false
                                return
                            }
                            root.setImageZoom(imageFlick.zoom * (wheel.angleDelta.y > 0 ? 1.2 : 1 / 1.2))
                            wheel.accepted = true
                        }
                    }
                }

                Image {
                    id: animationImage
                    anchors.fill: parent
                    anchors.margins: 8 * root.uiScale
                    visible: imageAnimation.frameSource !== ""
                    source: visible ? imageAnimation.frameSource : ""
                    fillMode: Image.PreserveAspectFit
                    cache: true
                    asynchronous: true
                    smooth: true
                    z: 3
                }

                Image {
                    id: pdfImage
                    anchors.fill: parent
                    anchors.margins: 8 * root.uiScale
                    visible: root.previewKind === "pdf" && pdfPreview.supported
                    source: visible ? pdfPreview.imageSource : ""
                    fillMode: Image.PreserveAspectFit
                    cache: true
                    asynchronous: true
                    smooth: true
                }

                MediaPreviewBlock {
                    id: mediaPreviewBlock
                    anchors.fill: parent
                    session: root.session
                    uiScale: root.uiScale
                    visible: candidate
                    z: 2
                }

                Flickable {
                    id: textFlick
                    anchors.fill: parent
                    anchors.margins: 12 * root.uiScale
                    visible: root.previewKind === "text" && textPreview.supported
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
                    anchors.margins: 12 * root.uiScale
                    visible: root.previewKind === "archive" && archivePreview.supported
                    clip: true
                    contentWidth: width
                    contentHeight: archiveColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: archiveColumn
                        width: archiveFlick.width
                        spacing: 6 * root.uiScale

                        Text {
                            width: parent.width
                            text: archivePreview.formatName !== ""
                                ? archivePreview.formatName + " archive"
                                : "Archive contents"
                            elide: Text.ElideRight
                            color: Ryoku.inkMuted
                            font.family: Ryoku.uiFont
                            font.pixelSize: 9 * root.uiScale
                            font.weight: Font.Medium
                        }

                        Repeater {
                            model: archivePreview.entries
                            delegate: Row {
                                id: archiveEntryRow
                                required property var modelData
                                width: archiveColumn.width
                                spacing: 7 * root.uiScale
                                Text {
                                    width: Math.max(0, archiveEntryRow.width - 70 * root.uiScale)
                                    text: archiveEntryRow.modelData.path
                                    elide: Text.ElideMiddle
                                    color: Ryoku.inkDim
                                    font.family: Ryoku.uiFont
                                    font.pixelSize: 8 * root.uiScale
                                }
                                Text {
                                    width: 62 * root.uiScale
                                    text: archiveEntryRow.modelData.sizeKnown
                                        ? root.formatBytes(archiveEntryRow.modelData.size) : ""
                                    horizontalAlignment: Text.AlignRight
                                    color: Ryoku.inkFaint
                                    font.family: Ryoku.monoFont
                                    font.pixelSize: 7.5 * root.uiScale
                                }
                            }
                        }
                    }
                }

                Item {
                    anchors.centerIn: parent
                    width: Math.max(80, parent.width - 34 * root.uiScale)
                    height: 132 * root.uiScale
                    visible: root.showStateCard && !root.ready
                    z: 10

                    Column {
                        anchors.centerIn: parent
                        width: parent.width
                        spacing: 8 * root.uiScale

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 44 * root.uiScale
                            height: 44 * root.uiScale
                            radius: 11 * root.uiScale
                            color: Ryoku.paperLift
                            border.width: 1
                            border.color: root.previewError !== "" ? Ryoku.sun : Ryoku.lineSoft
                            Text {
                                anchors.centerIn: parent
                                text: root.busy ? "…" : (root.directorySelection ? "DIR" : "i")
                                color: root.previewError !== "" ? Ryoku.sun : Ryoku.inkMuted
                                font.family: root.directorySelection ? Ryoku.monoFont : Ryoku.uiFont
                                font.pixelSize: root.directorySelection ? 8 * root.uiScale : 16 * root.uiScale
                                font.weight: Font.Medium
                            }
                        }

                        Text {
                            width: parent.width
                            text: root.stateTitle()
                            horizontalAlignment: Text.AlignHCenter
                            color: Ryoku.ink
                            font.family: Ryoku.uiFont
                            font.pixelSize: 11 * root.uiScale
                            font.weight: Font.Medium
                        }
                        Text {
                            width: parent.width
                            text: root.stateDetail()
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                            color: Ryoku.inkMuted
                            font.family: Ryoku.uiFont
                            font.pixelSize: 8.5 * root.uiScale
                        }
                    }
                }
            }

            Row {
                width: parent.width
                height: 28 * root.uiScale
                visible: root.previewKind === "image" && root.singleSelection
                spacing: 6 * root.uiScale

                Repeater {
                    model: [
                        { label: "−", action: "out" },
                        { label: Math.round(imageFlick.zoom * 100) + "%", action: "fit" },
                        { label: "+", action: "in" }
                    ]
                    delegate: Rectangle {
                        id: zoomButton
                        required property var modelData
                        width: modelData.action === "fit" ? 62 * root.uiScale : 30 * root.uiScale
                        height: parent.height
                        radius: 6 * root.uiScale
                        color: zoomHover.hovered ? Ryoku.tint10 : Ryoku.tint5
                        Text {
                            anchors.centerIn: parent
                            text: zoomButton.modelData.label
                            color: Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 9 * root.uiScale
                        }
                        HoverHandler { id: zoomHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                if (zoomButton.modelData.action === "out") root.setImageZoom(imageFlick.zoom / 1.25)
                                else if (zoomButton.modelData.action === "in") root.setImageZoom(imageFlick.zoom * 1.25)
                                else root.resetImageView()
                            }
                        }
                    }
                }

                Rectangle {
                    width: Math.max(0, parent.width - 140 * root.uiScale)
                    height: parent.height
                    visible: imagePreview.animated
                    radius: 6 * root.uiScale
                    color: animationHover.hovered && imagePreview.animationSupported && !Ryoku.reduceMotion
                        ? Ryoku.tint10 : Ryoku.tint5
                    opacity: imagePreview.animationSupported && !Ryoku.reduceMotion ? 1.0 : 0.42
                    Text {
                        anchors.centerIn: parent
                        text: imageAnimation.playing ? "Stop animation" : "Quick Look animation"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 8.5 * root.uiScale
                    }
                    HoverHandler { id: animationHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        enabled: imagePreview.animationSupported && !Ryoku.reduceMotion
                        onTapped: {
                            if (imageAnimation.playing) imageAnimation.stop()
                            else imageAnimation.play()
                        }
                    }
                }
            }

            Row {
                width: parent.width
                height: 28 * root.uiScale
                visible: root.previewKind === "pdf" && root.singleSelection
                spacing: 6 * root.uiScale

                Rectangle {
                    width: 30 * root.uiScale
                    height: parent.height
                    radius: 6 * root.uiScale
                    color: pdfPrevHover.hovered && pdfPreview.page > 0 && !pdfPreview.loading ? Ryoku.tint10 : Ryoku.tint5
                    opacity: pdfPreview.page > 0 && !pdfPreview.loading ? 1.0 : 0.4
                    Text { anchors.centerIn: parent; text: "‹"; color: Ryoku.inkDim; font.family: Ryoku.uiFont; font.pixelSize: 15 * root.uiScale }
                    HoverHandler { id: pdfPrevHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { enabled: pdfPreview.page > 0 && !pdfPreview.loading; onTapped: pdfPreview.page = pdfPreview.page - 1 }
                }
                Text {
                    width: Math.max(0, parent.width - 72 * root.uiScale)
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: pdfPreview.pageCount > 0
                        ? "Page " + (pdfPreview.page + 1) + " of " + pdfPreview.pageCount : "PDF"
                    color: Ryoku.inkMuted
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * root.uiScale
                }
                Rectangle {
                    width: 30 * root.uiScale
                    height: parent.height
                    radius: 6 * root.uiScale
                    readonly property bool canNext: pdfPreview.pageCount > 0
                        && pdfPreview.page + 1 < pdfPreview.pageCount && !pdfPreview.loading
                    color: pdfNextHover.hovered && canNext ? Ryoku.tint10 : Ryoku.tint5
                    opacity: canNext ? 1.0 : 0.4
                    Text { anchors.centerIn: parent; text: "›"; color: Ryoku.inkDim; font.family: Ryoku.uiFont; font.pixelSize: 15 * root.uiScale }
                    HoverHandler { id: pdfNextHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { enabled: parent.canNext; onTapped: pdfPreview.page = pdfPreview.page + 1 }
                }
            }

            Column {
                width: parent.width
                visible: root.singleSelection
                spacing: 5 * root.uiScale

                Text {
                    width: parent.width
                    text: root.details.name || root.selectedName
                    elide: Text.ElideMiddle
                    color: Ryoku.ink
                    font.family: Ryoku.uiFont
                    font.pixelSize: 13 * root.uiScale
                    font.weight: Font.Medium
                }

                Text {
                    width: parent.width
                    visible: imagePreview.supported
                    text: {
                        var parts = []
                        if (imagePreview.pixelWidth > 0 && imagePreview.pixelHeight > 0)
                            parts.push(imagePreview.pixelWidth + " × " + imagePreview.pixelHeight)
                        if (imagePreview.formatName !== "") parts.push(imagePreview.formatName.toUpperCase())
                        if (imagePreview.animated && imagePreview.frameCount > 1) parts.push(imagePreview.frameCount + " frames")
                        return parts.join(" · ")
                    }
                    elide: Text.ElideRight
                    color: Ryoku.inkMuted
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8.5 * root.uiScale
                }

                Text {
                    width: parent.width
                    visible: imagePreview.supported
                        && (imagePreview.cameraMake !== "" || imagePreview.cameraModel !== "" || imagePreview.lensModel !== "")
                    text: {
                        var parts = []
                        var camera = (imagePreview.cameraMake + " " + imagePreview.cameraModel).trim()
                        if (camera !== "") parts.push(camera)
                        if (imagePreview.lensModel !== "") parts.push(imagePreview.lensModel)
                        return parts.join(" · ")
                    }
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    wrapMode: Text.WordWrap
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * root.uiScale
                }

                Text {
                    width: parent.width
                    visible: imagePreview.supported
                        && (imagePreview.dateTaken !== "" || imagePreview.exposureTime !== ""
                            || imagePreview.aperture !== "" || imagePreview.iso !== "" || imagePreview.focalLength !== "")
                    text: {
                        var parts = []
                        if (imagePreview.dateTaken !== "") parts.push(imagePreview.dateTaken)
                        if (imagePreview.exposureTime !== "") parts.push(imagePreview.exposureTime)
                        if (imagePreview.aperture !== "") parts.push("f/" + imagePreview.aperture)
                        if (imagePreview.iso !== "") parts.push("ISO " + imagePreview.iso)
                        if (imagePreview.focalLength !== "") parts.push(imagePreview.focalLength)
                        return parts.join(" · ")
                    }
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    wrapMode: Text.WordWrap
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * root.uiScale
                }

                Text {
                    width: parent.width
                    visible: imagePreview.metadataLimited
                    text: "Deep image metadata was skipped because it exceeds the 64 MiB safety limit."
                    wrapMode: Text.WordWrap
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * root.uiScale
                }

                Text {
                    width: parent.width
                    visible: imagePreview.animated && !imagePreview.animationSupported
                    text: "Animation Quick Look is disabled because this file exceeds the bounded animation limits."
                    wrapMode: Text.WordWrap
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * root.uiScale
                }

                Text {
                    width: parent.width
                    visible: textPreview.supported && textPreview.truncated
                    text: "Text preview is truncated at the bounded 192 KiB / 2,500-line limit."
                    wrapMode: Text.WordWrap
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * root.uiScale
                }

                Text {
                    width: parent.width
                    visible: archivePreview.supported
                    text: archivePreview.entries.length
                        + (archivePreview.truncated ? "+ entries · bounded preview" : " entries")
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * root.uiScale
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                visible: root.singleSelection
                color: Ryoku.lineSoft
            }

            Column {
                width: parent.width
                spacing: 8 * root.uiScale
                visible: root.singleSelection

                Repeater {
                    model: [
                        { label: "Size", value: root.details.sizeText || "" },
                        { label: "Modified", value: root.details.modified || "" },
                        { label: "Owner", value: root.details.owner || "" },
                        { label: "Permissions", value: root.details.permissions || "" }
                    ]
                    delegate: Row {
                        id: metadataRow
                        required property var modelData
                        width: parent.width
                        visible: modelData.value !== ""
                        Text {
                            width: 84 * root.uiScale
                            text: metadataRow.modelData.label
                            color: Ryoku.inkFaint
                            font.family: Ryoku.uiFont
                            font.pixelSize: 8.5 * root.uiScale
                        }
                        Text {
                            width: parent.width - 84 * root.uiScale
                            text: metadataRow.modelData.value
                            elide: Text.ElideMiddle
                            color: Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 9 * root.uiScale
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: root.directorySelection
                text: "Folder size is intentionally on-demand; opening this panel never starts a recursive scan."
                wrapMode: Text.WordWrap
                color: Ryoku.inkFaint
                font.family: Ryoku.uiFont
                font.pixelSize: 8 * root.uiScale
            }
        }
    }
}
