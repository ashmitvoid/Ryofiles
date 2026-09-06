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
    readonly property string richPath: {
        if (!root.visible || !root.details || root.details.path === undefined)
            return ""
        if (root.details.isDirectory === true || root.details.isSymlink === true)
            return ""
        return root.details.path || ""
    }

    visible: false
    anchors.fill: parent
    z: 1000

    function addRow(rows, label, value) {
        var text = value === undefined || value === null ? "" : String(value)
        if (text !== "")
            rows.push({ label: label, value: text })
    }

    function addSection(rows, label) {
        rows.push({ section: true, label: label, value: "" })
    }

    function formatDuration(milliseconds) {
        var total = Math.max(0, Math.round(Number(milliseconds) / 1000))
        if (!isFinite(total) || total <= 0)
            return ""
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var seconds = total % 60
        function pad(value) { return value < 10 ? "0" + value : String(value) }
        return hours > 0
            ? hours + ":" + pad(minutes) + ":" + pad(seconds)
            : minutes + ":" + pad(seconds)
    }

    function formatBitRate(value) {
        var bits = Number(value)
        if (!isFinite(bits) || bits <= 0)
            return ""
        if (bits >= 1000000)
            return (bits / 1000000).toFixed(bits < 10000000 ? 1 : 0) + " Mbps"
        return Math.round(bits / 1000) + " kbps"
    }

    function imageCameraText() {
        var parts = []
        if (imageMetadata.cameraMake !== "")
            parts.push(imageMetadata.cameraMake)
        if (imageMetadata.cameraModel !== ""
                && imageMetadata.cameraModel !== imageMetadata.cameraMake)
            parts.push(imageMetadata.cameraModel)
        return parts.join(" · ")
    }

    function propertyRows() {
        var rows = []
        root.addSection(rows, "FILE")
        root.addRow(rows, "TYPE", root.details.type || "")
        root.addRow(rows, "MIME", root.details.mime || "")
        root.addRow(rows, "LOCATION", root.details.parent || "")
        root.addRow(rows, "PATH", root.details.path || "")
        root.addRow(rows, "SIZE", root.displayedSize)
        root.addRow(rows, "CONTENTS", root.folderContents)
        root.addRow(rows, "MODIFIED", root.details.modified || "")
        root.addRow(rows, "CREATED", root.details.created || "")
        root.addRow(rows, "OWNER", root.details.owner || "")
        root.addRow(rows, "GROUP", root.details.group || "")
        root.addRow(rows, "PERMISSIONS", root.details.permissions || "")
        root.addRow(rows, "LINK TARGET", root.details.symlinkTarget || "")

        if (imageMetadata.active) {
            root.addSection(rows, "IMAGE")
            if (imageMetadata.loading) {
                root.addRow(rows, "STATUS", "Reading metadata…")
            } else if (imageMetadata.supported) {
                if (imageMetadata.pixelWidth > 0 && imageMetadata.pixelHeight > 0)
                    root.addRow(rows, "DIMENSIONS", imageMetadata.pixelWidth + " × " + imageMetadata.pixelHeight + " px")
                root.addRow(rows, "FORMAT", imageMetadata.formatName.toUpperCase())
                if (imageMetadata.animated)
                    root.addRow(rows, "ANIMATION", imageMetadata.frameCount > 0
                        ? imageMetadata.frameCount + " frames" : "Animated")
                root.addRow(rows, "CAMERA", root.imageCameraText())
                root.addRow(rows, "LENS", imageMetadata.lensModel)
                root.addRow(rows, "DATE TAKEN", imageMetadata.dateTaken)
                root.addRow(rows, "EXPOSURE", imageMetadata.exposureTime)
                root.addRow(rows, "APERTURE", imageMetadata.aperture)
                root.addRow(rows, "ISO", imageMetadata.iso)
                root.addRow(rows, "FOCAL LENGTH", imageMetadata.focalLength)
                root.addRow(rows, "ORIENTATION", imageMetadata.orientation)
                root.addRow(rows, "EXPOSURE BIAS", imageMetadata.exposureBias)
                root.addRow(rows, "WHITE BALANCE", imageMetadata.whiteBalance)
                root.addRow(rows, "COLOR SPACE", imageMetadata.colorSpace)
                root.addRow(rows, "SOFTWARE", imageMetadata.software)
                root.addRow(rows, "ARTIST", imageMetadata.artist)
                root.addRow(rows, "COPYRIGHT", imageMetadata.copyright)
                if (imageMetadata.metadataLimited)
                    root.addRow(rows, "METADATA", "Deep metadata skipped for large image")
            }
        }

        if (mediaMetadata.active) {
            root.addSection(rows, "MEDIA")
            if (mediaMetadata.loading) {
                root.addRow(rows, "STATUS", "Reading metadata…")
            } else if (mediaMetadata.supported) {
                root.addRow(rows, "MEDIA KIND", mediaMetadata.mediaKind.toUpperCase())
                root.addRow(rows, "CONTAINER", mediaMetadata.formatName)
                root.addRow(rows, "DURATION", root.formatDuration(mediaMetadata.durationMs))
                root.addRow(rows, "BIT RATE", root.formatBitRate(mediaMetadata.bitRate))
                root.addRow(rows, "TITLE", mediaMetadata.title)
                root.addRow(rows, "ARTIST", mediaMetadata.artist)
                root.addRow(rows, "ALBUM", mediaMetadata.album)
                if (mediaMetadata.hasAudio) {
                    root.addRow(rows, "AUDIO CODEC", mediaMetadata.audioCodec)
                    root.addRow(rows, "SAMPLE RATE", mediaMetadata.sampleRate > 0
                        ? mediaMetadata.sampleRate + " Hz" : "")
                    root.addRow(rows, "CHANNELS", mediaMetadata.channels > 0
                        ? mediaMetadata.channels + (mediaMetadata.channelLayout !== ""
                            ? " · " + mediaMetadata.channelLayout : "") : mediaMetadata.channelLayout)
                }
                if (mediaMetadata.hasVideo) {
                    root.addRow(rows, "VIDEO CODEC", mediaMetadata.videoCodec)
                    if (mediaMetadata.videoWidth > 0 && mediaMetadata.videoHeight > 0)
                        root.addRow(rows, "VIDEO SIZE", mediaMetadata.videoWidth + " × " + mediaMetadata.videoHeight + " px")
                    root.addRow(rows, "FRAME RATE", mediaMetadata.frameRate > 0
                        ? mediaMetadata.frameRate.toFixed(2).replace(/\.00$/, "") + " fps" : "")
                }
            }
        }

        if (fontMetadata.active) {
            root.addSection(rows, "FONT")
            if (fontMetadata.loading) {
                root.addRow(rows, "STATUS", "Reading metadata…")
            } else if (fontMetadata.supported) {
                root.addRow(rows, "FAMILY", fontMetadata.familyName)
                root.addRow(rows, "STYLE NAME", fontMetadata.styleName)
                root.addRow(rows, "STYLE", fontMetadata.styleLabel)
                root.addRow(rows, "WEIGHT", fontMetadata.weight > 0 ? fontMetadata.weight : "")
                root.addRow(rows, "UNITS / EM", fontMetadata.unitsPerEm > 0
                    ? Math.round(fontMetadata.unitsPerEm) : "")
                root.addRow(rows, "WRITING SYSTEMS", fontMetadata.writingSystems)
            }
        }

        if (pdfMetadata.active) {
            root.addSection(rows, "PDF")
            if (pdfMetadata.loading) {
                root.addRow(rows, "STATUS", "Reading metadata…")
            } else if (pdfMetadata.supported) {
                root.addRow(rows, "PAGES", pdfMetadata.pageCount > 0 ? pdfMetadata.pageCount : "")
                root.addRow(rows, "DOCUMENT TITLE", pdfMetadata.title)
                root.addRow(rows, "AUTHOR", pdfMetadata.author)
                root.addRow(rows, "SUBJECT", pdfMetadata.subject)
                root.addRow(rows, "KEYWORDS", pdfMetadata.keywords)
            }
        }
        return rows
    }

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

    ImagePreviewLoader {
        id: imageMetadata
        active: root.richPath !== "" && imageMetadata.isCandidate(root.richPath)
        path: active ? root.richPath : ""
    }

    MediaPreviewLoader {
        id: mediaMetadata
        metadataOnly: true
        active: root.richPath !== "" && mediaMetadata.isCandidate(root.richPath)
        path: active ? root.richPath : ""
    }

    FontPreviewLoader {
        id: fontMetadata
        metadataOnly: true
        active: root.richPath !== "" && fontMetadata.isCandidate(root.richPath)
        path: active ? root.richPath : ""
    }

    PdfPreviewLoader {
        id: pdfMetadata
        metadataOnly: true
        active: root.richPath !== "" && pdfMetadata.isCandidate(root.richPath)
        path: active ? root.richPath : ""
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
                model: root.propertyRows()

                delegate: Item {
                    id: propertyRow
                    required property var modelData

                    readonly property bool section: modelData.section === true
                    width: ListView.view.width
                    height: section ? 31 * root.uiScale
                        : (modelData.value !== "" ? 52 * root.uiScale : 0)
                    visible: section || modelData.value !== ""

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: propertyRow.section ? 9 * root.uiScale : 7 * root.uiScale
                        visible: propertyRow.section
                        text: "// " + propertyRow.modelData.label
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * root.uiScale
                        font.letterSpacing: 1.1
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: 7 * root.uiScale
                        width: 110 * root.uiScale
                        visible: !propertyRow.section
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
                        visible: !propertyRow.section
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
                        visible: !propertyRow.section
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
