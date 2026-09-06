// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtMultimedia
import Ryofiles.Core

Item {
    id: root

    required property var session
    property real uiScale: 1

    readonly property bool mediaCandidate: root.session
        && root.session.selectionCount === 1
        && mediaPreview.isCandidate(root.session.selectedPath)
    readonly property bool fontCandidate: root.session
        && root.session.selectionCount === 1
        && fontPreview.isCandidate(root.session.selectedPath)
    readonly property bool candidate: mediaCandidate || fontCandidate
    readonly property bool active: root.visible && candidate
    readonly property string kind: mediaCandidate ? "media" : (fontCandidate ? "font" : "")
    readonly property bool loading: mediaCandidate ? mediaPreview.loading : (fontCandidate ? fontPreview.loading : false)
    readonly property bool supported: mediaCandidate ? mediaPreview.supported : (fontCandidate ? fontPreview.supported : false)
    readonly property string error: mediaCandidate ? mediaPreview.error : (fontCandidate ? fontPreview.error : "")

    function formatTime(ms) {
        var total = Math.max(0, Math.floor(Number(ms) / 1000))
        var minutes = Math.floor(total / 60)
        var seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function codecSummary() {
        var parts = []
        if (mediaPreview.videoCodec !== "") parts.push(mediaPreview.videoCodec.toUpperCase())
        if (mediaPreview.audioCodec !== "") parts.push(mediaPreview.audioCodec.toUpperCase())
        return parts.join(" · ")
    }

    MediaPreviewLoader {
        id: mediaPreview
        active: root.active && root.mediaCandidate
        path: active ? root.session.selectedPath : ""
    }

    MediaPlaybackController {
        id: mediaPlayback
        active: root.active && root.mediaCandidate
        path: active ? root.session.selectedPath : ""
    }

    FontPreviewLoader {
        id: fontPreview
        active: root.active && root.fontCandidate
        path: active ? root.session.selectedPath : ""
    }

    Image {
        anchors.fill: parent
        anchors.margins: 10 * root.uiScale
        visible: root.active
            && root.mediaCandidate
            && mediaPreview.posterSource !== ""
            && !mediaPlayback.prepared
        source: visible ? mediaPreview.posterSource : ""
        fillMode: Image.PreserveAspectFit
        cache: true
        asynchronous: true
        smooth: true
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        anchors.margins: 10 * root.uiScale
        visible: root.active
            && root.mediaCandidate
            && mediaPreview.hasVideo
            && mediaPlayback.prepared
        fillMode: VideoOutput.PreserveAspectFit
        Component.onCompleted: mediaPlayback.attachVideoSink(videoSink)
    }

    Image {
        anchors.fill: parent
        anchors.leftMargin: 10 * root.uiScale
        anchors.rightMargin: 10 * root.uiScale
        anchors.topMargin: 10 * root.uiScale
        anchors.bottomMargin: fontMetadata.visible ? 68 * root.uiScale : 10 * root.uiScale
        visible: root.active
            && root.fontCandidate
            && fontPreview.supported
            && fontPreview.sampleSource !== ""
        source: visible ? fontPreview.sampleSource : ""
        fillMode: Image.PreserveAspectFit
        cache: true
        asynchronous: true
        smooth: true
    }

    Item {
        anchors.centerIn: parent
        width: Math.min(parent.width - 28 * root.uiScale, 230 * root.uiScale)
        height: 86 * root.uiScale
        visible: root.active && root.loading

        Column {
            anchors.centerIn: parent
            width: parent.width
            spacing: 8 * root.uiScale
            Text {
                width: parent.width
                text: root.fontCandidate ? "Preparing font preview" : "Reading media"
                horizontalAlignment: Text.AlignHCenter
                color: Ryoku.inkDim
                font.family: Ryoku.uiFont
                font.pixelSize: 11 * root.uiScale
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: "Preview work is bounded and cancellable"
                horizontalAlignment: Text.AlignHCenter
                color: Ryoku.inkFaint
                font.family: Ryoku.uiFont
                font.pixelSize: 8.5 * root.uiScale
            }
        }
    }

    Item {
        anchors.centerIn: parent
        width: Math.min(parent.width - 28 * root.uiScale, 240 * root.uiScale)
        height: 110 * root.uiScale
        visible: root.active && !root.loading && root.error !== ""

        Column {
            anchors.centerIn: parent
            width: parent.width
            spacing: 7 * root.uiScale
            Text {
                width: parent.width
                text: "Preview unavailable"
                horizontalAlignment: Text.AlignHCenter
                color: Ryoku.ink
                font.family: Ryoku.uiFont
                font.pixelSize: 11 * root.uiScale
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: root.error
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

    Text {
        anchors.centerIn: parent
        visible: root.active
            && root.mediaCandidate
            && !root.loading
            && root.error === ""
            && mediaPreview.supported
            && mediaPreview.posterSource === ""
            && !mediaPlayback.prepared
        text: mediaPreview.hasAudio && !mediaPreview.hasVideo ? "Audio" : "Media"
        color: Ryoku.inkMuted
        font.family: Ryoku.uiFont
        font.pixelSize: 22 * root.uiScale
        font.weight: Font.Medium
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 9 * root.uiScale
        height: 66 * root.uiScale
        visible: root.active && root.mediaCandidate && mediaPreview.supported
        radius: 8 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineSoft

        Column {
            anchors.fill: parent
            anchors.margins: 8 * root.uiScale
            spacing: 5 * root.uiScale

            Row {
                width: parent.width
                height: 26 * root.uiScale
                spacing: 6 * root.uiScale

                Rectangle {
                    width: 30 * root.uiScale
                    height: parent.height
                    radius: 6 * root.uiScale
                    color: playHover.hovered ? Ryoku.tint10 : Ryoku.tint5
                    Text {
                        anchors.centerIn: parent
                        text: mediaPlayback.playing ? "Ⅱ" : "▶"
                        color: Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10 * root.uiScale
                    }
                    HoverHandler { id: playHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (mediaPlayback.playing) mediaPlayback.pause()
                            else mediaPlayback.play()
                        }
                    }
                }

                Rectangle {
                    width: 30 * root.uiScale
                    height: parent.height
                    radius: 6 * root.uiScale
                    color: stopHover.hovered && mediaPlayback.prepared ? Ryoku.tint10 : Ryoku.tint5
                    opacity: mediaPlayback.prepared ? 1.0 : 0.38
                    Text {
                        anchors.centerIn: parent
                        text: "■"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 8 * root.uiScale
                    }
                    HoverHandler { id: stopHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { enabled: mediaPlayback.prepared; onTapped: mediaPlayback.stop() }
                }

                Text {
                    width: Math.max(0, parent.width - 72 * root.uiScale)
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    text: root.formatTime(mediaPlayback.position)
                        + " / " + root.formatTime(mediaPlayback.duration > 0
                            ? mediaPlayback.duration : mediaPreview.durationMs)
                    color: Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.uiScale
                }
            }

            Rectangle {
                id: progressTrack
                width: parent.width
                height: 4 * root.uiScale
                radius: 2 * root.uiScale
                color: Ryoku.tint10
                Rectangle {
                    height: parent.height
                    radius: parent.radius
                    width: {
                        var duration = mediaPlayback.duration > 0 ? mediaPlayback.duration : mediaPreview.durationMs
                        if (duration <= 0) return 0
                        return parent.width * Math.max(0, Math.min(1, mediaPlayback.position / duration))
                    }
                    color: Ryoku.inkDim
                }
                TapHandler {
                    enabled: mediaPlayback.prepared && mediaPlayback.seekable && mediaPlayback.duration > 0
                    onTapped: (eventPoint, button) => {
                        var ratio = Math.max(0, Math.min(1, eventPoint.position.x / progressTrack.width))
                        mediaPlayback.seek(Math.round(ratio * mediaPlayback.duration))
                    }
                }
            }

            Text {
                width: parent.width
                text: {
                    var parts = []
                    if (mediaPreview.title !== "") parts.push(mediaPreview.title)
                    else if (root.session) parts.push(root.session.selectedPath.split("/").pop())
                    if (mediaPreview.artist !== "") parts.push(mediaPreview.artist)
                    var codecs = root.codecSummary()
                    if (codecs !== "") parts.push(codecs)
                    return parts.join(" · ")
                }
                elide: Text.ElideRight
                color: Ryoku.inkFaint
                font.family: Ryoku.uiFont
                font.pixelSize: 7.5 * root.uiScale
            }
        }
    }

    Rectangle {
        id: fontMetadata
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 9 * root.uiScale
        height: 54 * root.uiScale
        visible: root.active && root.fontCandidate && fontPreview.supported
        radius: 8 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineSoft

        Column {
            anchors.fill: parent
            anchors.margins: 8 * root.uiScale
            spacing: 3 * root.uiScale
            Text {
                width: parent.width
                text: {
                    var family = fontPreview.familyName !== "" ? fontPreview.familyName : "Font"
                    var style = fontPreview.styleName !== "" ? fontPreview.styleName : fontPreview.styleLabel
                    return style !== "" ? family + " · " + style : family
                }
                elide: Text.ElideRight
                color: Ryoku.ink
                font.family: Ryoku.uiFont
                font.pixelSize: 10 * root.uiScale
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: {
                    var parts = []
                    if (fontPreview.weight > 0) parts.push("Weight " + fontPreview.weight)
                    if (fontPreview.unitsPerEm > 0) parts.push("UPM " + Math.round(fontPreview.unitsPerEm))
                    if (fontPreview.writingSystems !== "") parts.push(fontPreview.writingSystems)
                    return parts.join(" · ")
                }
                elide: Text.ElideRight
                color: Ryoku.inkFaint
                font.family: Ryoku.uiFont
                font.pixelSize: 7.5 * root.uiScale
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10 * root.uiScale
        visible: root.active
            && root.mediaCandidate
            && mediaPreview.supported
            && mediaPlayback.error !== ""
        text: mediaPlayback.error
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        color: Ryoku.sun
        font.family: Ryoku.uiFont
        font.pixelSize: 8 * root.uiScale
    }
}
