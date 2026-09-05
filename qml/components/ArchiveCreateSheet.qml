// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: root

    property real uiScale: 1
    property var sourcePaths: []
    property string parentPath: ""
    property string errorText: ""
    property int formatIndex: 0
    readonly property var formats: [
        { label: "TAR.GZ", suffix: ".tar.gz" },
        { label: "ZIP", suffix: ".zip" },
        { label: "7Z", suffix: ".7z" },
        { label: "TAR.ZST", suffix: ".tar.zst" },
        { label: "TAR.XZ", suffix: ".tar.xz" },
        { label: "TAR", suffix: ".tar" },
        { label: "TGZ", suffix: ".tgz" }
    ]

    signal accepted(var paths, string archivePath)
    signal cancelled()

    visible: false
    anchors.fill: parent
    z: 1000

    function leafName(path) {
        if (!path || path === "")
            return ""
        var slash = path.lastIndexOf("/")
        return slash >= 0 ? path.substring(slash + 1) : path
    }

    function parentDirectory(path) {
        if (!path || path === "")
            return ""
        var slash = path.lastIndexOf("/")
        if (slash <= 0)
            return "/"
        return path.substring(0, slash)
    }

    function stripSupportedSuffix(name) {
        var lower = name.toLowerCase()
        for (var i = 0; i < root.formats.length; ++i) {
            var suffix = root.formats[i].suffix
            if (lower.endsWith(suffix))
                return name.substring(0, name.length - suffix.length)
        }
        return name
    }

    function openFor(paths) {
        var snapshot = []
        if (paths) {
            for (var i = 0; i < paths.length; ++i) {
                if (paths[i] && paths[i] !== "")
                    snapshot.push(paths[i])
            }
        }
        if (snapshot.length === 0)
            return

        root.sourcePaths = snapshot
        root.parentPath = root.parentDirectory(snapshot[0])
        root.formatIndex = 0
        root.errorText = ""
        var initialName = snapshot.length === 1
            ? root.leafName(snapshot[0])
            : "Archive"
        nameInput.text = root.stripSupportedSuffix(initialName)
        root.visible = true
        Qt.callLater(function() {
            nameInput.forceActiveFocus()
            nameInput.selectAll()
        })
    }

    function requestedPath() {
        var base = nameInput.text.trim()
        if (base === "" || base === "." || base === ".." || base.indexOf("/") >= 0)
            return ""
        if (root.parentPath === "")
            return ""

        var suffix = root.formats[root.formatIndex].suffix
        var fileName = root.stripSupportedSuffix(base) + suffix
        return root.parentPath === "/"
            ? "/" + fileName
            : root.parentPath + "/" + fileName
    }

    function submit() {
        var path = root.requestedPath()
        if (path === "") {
            root.errorText = "Enter a valid archive name"
            return
        }
        root.errorText = ""
        root.accepted(root.sourcePaths, path)
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.48)
        TapHandler { onTapped: root.cancelled() }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(520 * root.uiScale, root.width - 40 * root.uiScale)
        height: form.implicitHeight + 34 * root.uiScale
        radius: 8 * root.uiScale
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong

        TapHandler { }

        Column {
            id: form
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 17 * root.uiScale
            spacing: 12 * root.uiScale

            Text {
                width: parent.width
                text: "// CREATE ARCHIVE"
                color: Ryoku.ink
                font.family: Ryoku.monoFont
                font.pixelSize: 11 * root.uiScale
                font.letterSpacing: 1.2
            }

            Text {
                width: parent.width
                text: root.sourcePaths.length
                    + (root.sourcePaths.length === 1 ? " ITEM" : " ITEMS")
                    + "  //  NO OVERWRITE"
                color: Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
                font.letterSpacing: 0.7
            }

            Rectangle {
                width: parent.width
                height: 38 * root.uiScale
                radius: 6 * root.uiScale
                color: "transparent"
                border.width: nameInput.activeFocus ? 2 : 1
                border.color: nameInput.activeFocus ? Ryoku.ink : Ryoku.line

                TextInput {
                    id: nameInput
                    anchors.fill: parent
                    anchors.leftMargin: 11 * root.uiScale
                    anchors.rightMargin: 11 * root.uiScale
                    verticalAlignment: Text.AlignVCenter
                    color: Ryoku.ink
                    selectionColor: Ryoku.bone
                    selectedTextColor: Ryoku.inkOnBone
                    font.family: Ryoku.uiFont
                    font.pixelSize: 12 * root.uiScale
                    selectByMouse: true
                    onAccepted: root.submit()
                    onTextChanged: root.errorText = ""
                }
            }

            Text {
                width: parent.width
                text: "// FORMAT"
                color: Ryoku.inkMuted
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.uiScale
                font.letterSpacing: 1.0
            }

            Flow {
                width: parent.width
                spacing: 6 * root.uiScale

                Repeater {
                    model: root.formats

                    delegate: Rectangle {
                        id: formatChip
                        required property var modelData
                        required property int index

                        readonly property bool selected: root.formatIndex === index
                        width: chipText.implicitWidth + 18 * root.uiScale
                        height: 30 * root.uiScale
                        radius: 6 * root.uiScale
                        color: selected
                            ? Ryoku.bone
                            : (chipHover.hovered ? Ryoku.tint10 : "transparent")
                        border.width: 1
                        border.color: selected ? Ryoku.bone : Ryoku.line

                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: formatChip.modelData.label
                            color: formatChip.selected ? Ryoku.inkOnBone : Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 9 * root.uiScale
                            font.weight: Font.Medium
                            font.letterSpacing: 0.8
                        }

                        HoverHandler {
                            id: chipHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: {
                                root.formatIndex = formatChip.index
                                root.errorText = ""
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: root.errorText !== ""
                text: root.errorText
                color: Ryoku.sun
                wrapMode: Text.Wrap
                font.family: Ryoku.monoFont
                font.pixelSize: 9 * root.uiScale
            }

            Row {
                anchors.right: parent.right
                spacing: 8 * root.uiScale

                Rectangle {
                    width: 82 * root.uiScale
                    height: 30 * root.uiScale
                    radius: 6 * root.uiScale
                    color: cancelHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        anchors.centerIn: parent
                        text: "CANCEL"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.Medium
                    }

                    HoverHandler {
                        id: cancelHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler { onTapped: root.cancelled() }
                }

                Rectangle {
                    width: 82 * root.uiScale
                    height: 30 * root.uiScale
                    radius: 6 * root.uiScale
                    color: Ryoku.bone
                    border.width: 1
                    border.color: Ryoku.bone

                    Text {
                        anchors.centerIn: parent
                        text: "CREATE"
                        color: Ryoku.inkOnBone
                        font.family: Ryoku.uiFont
                        font.pixelSize: 9 * root.uiScale
                        font.weight: Font.Medium
                    }

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.submit() }
                }
            }
        }
    }
}
