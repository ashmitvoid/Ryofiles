// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtMultimedia
import Ryofiles.Core

Item {
    id: root

    required property var session
    property real uiScale: 1

    readonly property bool candidate: root.session
        && root.session.selectionCount === 1
        && mediaPreview.isCandidate(root.session.selectedPath)
    readonly property bool active: root.visible && candidate

    function formatTime(ms) {
        var total = Math.max(0, Math.floor(Number(ms) / 1000))
        var minutes = Math.floor(total / 60)
        var seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function codecSummary() {
        var parts = []
        if (mediaPreview.videoCodec !== "")
            parts.push(mediaPreview.videoCodec.toUpperCase())
        if (mediaPreview.audioCodec !== "")
            parts.push(mediaPreview.audioCodec.toUpperCase())
        return parts.join(" · ")
    }

    MediaPreviewLoader {
        id: mediaPreview
        active: root.active
        path: active ? root.session.selectedPath : ""
    }

    MediaPlaybackController {
        id: mediaPlayback
        active: root.active
        path: active ? root.session.selectedPath : ""
    }

    Image {
        anchors.fill: parent
        anchors.margins: 8 * root.uiScale
        visible: root.active
            && mediaPreview.posterSource !== ""
            && !mediaPlayback.prepared
        source: visible ? mediaPreview.posterSource : ""
        fillMode: Image.PreserveAspectFit
        cache: false
        asynchronous: false
        smooth: true
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        anchors.margins: 8 * root.uiScale
        visible: root.active
            && mediaPreview.hasVideo
            && mediaPlayback.prepared
        fillMode: VideoOutput.PreserveAspectFit

        Component.onCompleted: mediaPlayback.attachVideoSink(videoSink)
    }

    Text {
        anchors.centerIn: parent
        visible: root.active
            && !mediaPreview.loading
            && mediaPreview.posterSource === ""
            && !mediaPlayback.prepared
        text: mediaPreview.hasAudio && !mediaPreview.hasVideo ? "♫" : "▶"
        color: Ryoku.inkMuted
        font.family: Ryoku.uiFont
        font.pixelSize: 42 * root.uiScale
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 28 * root.uiScale
        visible: root.active && mediaPreview.loading
        text: "// READING MEDIA…"
        horizontalAlignment: Text.AlignHCenter
        color: Ryoku.inkMuted
        font.family: Ryoku.monoFont
        font.pixelSize: 9 * root.uiScale
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 28 * root.uiScale
        visible: root.active
            && !mediaPreview.loading
            && mediaPreview.error !== ""
        text: "// " + mediaPreview.error
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
        color: Ryoku.inkMuted
        font.family: Ryoku.monoFont
        font.pixelSize: 8 * root.uiScale
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8 * root.uiScale
        height: 64 * root.uiScale
        visible: root.active && mediaPreview.supported
        radius: 5 * root.uiScale
        color: Ryoku.tint5
        opacity: 0.94
        border.width: 1
        border.color: Ryoku.line

        Column {
            anchors.fill: parent
            anchors.margins: 7 * root.uiScale
            spacing: 5 * root.uiScale

            Row {
                width: parent.width
                height: 24 * root.uiScale
                spacing: 7 * root.uiScale

                Rectangle {
                    width: 34 * root.uiScale
                    height: parent.height
                    radius: 4 * root.uiScale
                    color: playHover.hovered ? Ryoku.tint10 : Ryoku.tint5
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        anchors.centerIn: parent
                        text: mediaPlayback.playing ? "Ⅱ" : "▶"
                        color: Ryoku.ink
                        font.family: Ryoku.monoFont
                        font.pixelSize: 10 * root.uiScale
                    }
                    HoverHandler { id: playHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (mediaPlayback.playing)
                                mediaPlayback.pause()
                            else
                                mediaPlayback.play()
                        }
                    }
                }

                Rectangle {
                    width: 34 * root.uiScale
                    height: parent.height
                    radius: 4 * root.uiScale
                    color: stopHover.hovered ? Ryoku.tint10 : Ryoku.tint5
                    border.width: 1
                    border.color: Ryoku.line
                    opacity: mediaPlayback.prepared ? 1.0 : 0.45

                    Text {
                        anchors.centerIn: parent
                        text: "■"
                        color: Ryoku.inkDim
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * root.uiScale
                    }
                    HoverHandler { id: stopHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        enabled: mediaPlayback.prepared
                        onTapped: mediaPlayback.stop()
                    }
                }

                Text {
                    width: Math.max(0, parent.width - 82 * root.uiScale)
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    text: root.formatTime(mediaPlayback.position)
                        + " / "
                        + root.formatTime(mediaPlayback.duration > 0
                            ? mediaPlayback.duration
                            : mediaPreview.durationMs)
                    color: Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.uiScale
                }
            }

            Rectangle {
                id: progressTrack
                width: parent.width
                height: 5 * root.uiScale
                radius: height / 2
                color: Ryoku.tint10

                Rectangle {
                    height: parent.height
                    radius: height / 2
                    width: {
                        var duration = mediaPlayback.duration > 0
                            ? mediaPlayback.duration
                            : mediaPreview.durationMs
                        if (duration <= 0)
                            return 0
                        return parent.width * Math.max(0, Math.min(1,
                            mediaPlayback.position / duration))
                    }
                    color: Ryoku.inkDim
                }

                TapHandler {
                    enabled: mediaPlayback.prepared
                        && mediaPlayback.seekable
                        && mediaPlayback.duration > 0
                    onTapped: (eventPoint, button) => {
                        var ratio = Math.max(0, Math.min(1,
                            eventPoint.position.x / progressTrack.width))
                        mediaPlayback.seek(Math.round(ratio * mediaPlayback.duration))
                    }
                }
            }

            Text {
                width: parent.width
                text: {
                    var parts = []
                    if (mediaPreview.title !== "")
                        parts.push(mediaPreview.title)
                    else if (root.session)
                        parts.push(root.session.selectedPath.split("/").pop())
                    if (mediaPreview.artist !== "")
                        parts.push(mediaPreview.artist)
                    var codecs = root.codecSummary()
                    if (codecs !== "")
                        parts.push(codecs)
                    return parts.join(" · ")
                }
                elide: Text.ElideRight
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 7 * root.uiScale
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10 * root.uiScale
        visible: root.active
            && mediaPreview.supported
            && mediaPlayback.error !== ""
        text: "// " + mediaPlayback.error
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        color: Ryoku.inkFaint
        font.family: Ryoku.monoFont
        font.pixelSize: 8 * root.uiScale
    }
}
