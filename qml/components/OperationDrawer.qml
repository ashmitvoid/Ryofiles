// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Rectangle {
    id: root

    required property var operations
    property real uiScale: 1

    function formatBytes(bytes) {
        var value = Number(bytes)
        if (!isFinite(value) || value <= 0)
            return "0 B"
        if (value < 1024)
            return Math.floor(value) + " B"
        if (value < 1024 * 1024)
            return (value / 1024).toFixed(1) + " KiB"
        if (value < 1024 * 1024 * 1024)
            return (value / (1024 * 1024)).toFixed(1) + " MiB"
        return (value / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    width: 360 * uiScale
    height: Math.min(360 * uiScale, header.height + list.contentHeight + 18 * uiScale)
    radius: 6 * uiScale
    color: Ryoku.paperLift
    border.width: 1
    border.color: Ryoku.lineStrong
    visible: operations && operations.count > 0

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 42 * root.uiScale
        color: "transparent"

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            text: root.operations && root.operations.activeCount > 0
                ? "// OPERATIONS  " + root.operations.activeCount + " ACTIVE"
                : "// OPERATIONS"
            color: Ryoku.ink
            font.family: Ryoku.monoFont
            font.pixelSize: 10 * root.uiScale
            font.letterSpacing: 1.1
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 14 * root.uiScale
            anchors.verticalCenter: parent.verticalCenter
            text: "CLEAR"
            color: clearHover.hovered ? Ryoku.ink : Ryoku.inkMuted
            font.family: Ryoku.uiFont
            font.pixelSize: 9 * root.uiScale
            font.weight: Font.Medium

            HoverHandler { id: clearHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.operations.clearFinished() }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Ryoku.line
        }
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        clip: true
        model: root.operations
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds

        delegate: Item {
            id: job

            required property string jobId
            required property string kind
            required property string state
            required property string currentSource
            required property string destination
            required property real progress
            required property string errorText
            required property string source
            required property bool progressIndeterminate
            required property var entriesProcessed
            required property var bytesProcessed

            width: list.width
            height: (job.kind === "extract" ? 78 : 62) * root.uiScale

            readonly property bool conflictState:
                state === "conflict" || state === "waiting"
            readonly property bool active:
                state === "queued" || state === "running" || conflictState
            readonly property bool extraction: kind === "extract"

            Text {
                id: title
                anchors.left: parent.left
                anchors.leftMargin: 14 * root.uiScale
                anchors.top: parent.top
                anchors.topMargin: 10 * root.uiScale
                width: parent.width - 88 * root.uiScale
                text: job.kind.toUpperCase() + "  //  " + job.state.toUpperCase()
                elide: Text.ElideRight
                color: job.state === "failed" ? Ryoku.sun : Ryoku.ink
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
                font.letterSpacing: 0.8
            }

            Text {
                id: detail
                anchors.left: title.left
                anchors.top: title.bottom
                anchors.topMargin: 5 * root.uiScale
                width: title.width
                text: job.errorText !== ""
                    ? job.errorText
                    : (job.extraction
                        ? (job.source !== "" ? job.source : job.destination)
                        : (job.currentSource !== "" ? job.currentSource : job.destination))
                elide: Text.ElideMiddle
                color: job.errorText !== "" ? Ryoku.sun : Ryoku.inkMuted
                font.family: Ryoku.uiFont
                font.pixelSize: 10 * root.uiScale
            }

            Text {
                anchors.left: title.left
                anchors.top: detail.bottom
                anchors.topMargin: 4 * root.uiScale
                width: title.width
                visible: job.extraction && job.errorText === ""
                text: {
                    var telemetry = Number(job.entriesProcessed) + " entries  //  "
                        + root.formatBytes(job.bytesProcessed)
                    if (job.currentSource !== "" && job.currentSource !== job.source)
                        return job.currentSource + "  //  " + telemetry
                    return job.destination + "  //  " + telemetry
                }
                elide: Text.ElideMiddle
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.uiScale
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 14 * root.uiScale
                anchors.verticalCenter: parent.verticalCenter
                text: job.active ? "CANCEL" : "×"
                color: actionHover.hovered ? Ryoku.ink : Ryoku.inkMuted
                font.family: Ryoku.uiFont
                font.pixelSize: job.active ? 9 * root.uiScale : 15 * root.uiScale
                font.weight: Font.Medium

                HoverHandler { id: actionHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        if (job.active)
                            root.operations.cancel(job.jobId)
                        else
                            root.operations.dismiss(job.jobId)
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Ryoku.lineSoft
            }

            Rectangle {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                height: 2 * root.uiScale
                width: parent.width * Math.max(0, Math.min(1, job.progress))
                visible: job.active && !job.conflictState && !job.progressIndeterminate
                color: Ryoku.inkDim
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 2 * root.uiScale
                visible: job.active && !job.conflictState && job.progressIndeterminate
                color: Ryoku.inkDim
                opacity: 0.45
            }
        }
    }
}
