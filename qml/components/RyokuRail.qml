// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Item {
    id: rail

    required property var fs
    property real uiScale: 1
    property int trashCount: 0
    property bool trashActive: false
    signal navigate(string path)
    signal openTrash()

    width: 268 * uiScale

    function pathInside(path, rootPath) {
        if (!path || !rootPath)
            return false
        if (path === rootPath)
            return true
        var prefix = rootPath.endsWith("/") ? rootPath : rootPath + "/"
        return path.indexOf(prefix) === 0
    }

    Connections {
        target: Drives

        function onMounted(objectPath, mountPath) {
            if (mountPath && mountPath.length > 0)
                rail.navigate(mountPath)
        }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Ryoku.line
    }

    Column {
        anchors.fill: parent
        anchors.margins: 24 * rail.uiScale
        spacing: 18 * rail.uiScale

        Rectangle {
            id: brand
            width: parent.width
            height: 64 * rail.uiScale
            color: "transparent"
            radius: 6 * rail.uiScale
            border.width: 1
            border.color: Ryoku.line

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16 * rail.uiScale
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10 * rail.uiScale

                Text {
                    text: "力"
                    color: Ryoku.ink
                    font.family: "Noto Sans CJK JP"
                    font.pixelSize: 22 * rail.uiScale
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        text: "RYOFILES"
                        color: Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 14 * rail.uiScale
                        font.weight: Font.Medium
                        font.letterSpacing: 2.2
                    }
                    Text {
                        text: "//FILES_"
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 10 * rail.uiScale
                        font.letterSpacing: 1.3
                    }
                }
            }
        }

        Flickable {
            id: sections
            width: parent.width
            height: Math.max(0, parent.height - brand.height - parent.spacing)
            contentWidth: width
            contentHeight: sectionColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: sectionColumn
                width: sections.width
                spacing: 18 * rail.uiScale

                Column {
                    width: parent.width
                    spacing: 2 * rail.uiScale

                    Text {
                        text: "01  PLACES"
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * rail.uiScale
                        font.letterSpacing: 1.5
                        bottomPadding: 5 * rail.uiScale
                    }

                    Repeater {
                        model: [
                            { label: "Home", path: rail.fs.home },
                            { label: "Desktop", path: rail.fs.desktop },
                            { label: "Documents", path: rail.fs.documents },
                            { label: "Downloads", path: rail.fs.downloads },
                            { label: "Pictures", path: rail.fs.pictures },
                            { label: "Music", path: rail.fs.music },
                            { label: "Videos", path: rail.fs.videos }
                        ]

                        delegate: Rectangle {
                            id: place
                            required property var modelData
                            width: parent.width
                            height: 34 * rail.uiScale
                            radius: 6 * rail.uiScale
                            color: rail.fs.path === modelData.path
                                ? Ryoku.bone
                                : (hover.hovered ? Ryoku.tint10 : "transparent")

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 12 * rail.uiScale
                                anchors.verticalCenter: parent.verticalCenter
                                text: (rail.fs.path === place.modelData.path ? "//  " : "    ")
                                    + place.modelData.label
                                color: rail.fs.path === place.modelData.path
                                    ? Ryoku.inkOnBone
                                    : Ryoku.inkDim
                                font.family: Ryoku.uiFont
                                font.pixelSize: 13 * rail.uiScale
                            }

                            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: rail.navigate(place.modelData.path) }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4 * rail.uiScale
                    visible: Drives.count > 0 || Drives.loading || Drives.lastError !== ""

                    Text {
                        text: "02  DEVICES"
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * rail.uiScale
                        font.letterSpacing: 1.5
                        bottomPadding: 3 * rail.uiScale
                    }

                    Repeater {
                        model: Drives

                        delegate: Rectangle {
                            id: drive
                            required property string objectPath
                            required property string name
                            required property string devicePath
                            required property string mountPoint
                            required property string fsType
                            required property string sizeText
                            required property bool mounted
                            required property bool removable
                            required property bool canPowerOffNow
                            required property bool busy

                            readonly property bool active:
                                drive.mounted && rail.pathInside(rail.fs.path, drive.mountPoint)

                            function openOrMount() {
                                if (drive.busy)
                                    return
                                if (drive.mounted) {
                                    if (drive.mountPoint !== "")
                                        rail.navigate(drive.mountPoint)
                                } else {
                                    Drives.mount(drive.objectPath)
                                }
                            }

                            width: parent.width
                            height: ((drive.mounted || drive.canPowerOffNow) ? 56 : 48) * rail.uiScale
                            radius: 6 * rail.uiScale
                            color: drive.active
                                ? Ryoku.bone
                                : (driveHover.hovered && !drive.busy ? Ryoku.tint10 : "transparent")
                            border.width: 1
                            border.color: drive.active ? Ryoku.bone : Ryoku.lineSoft
                            opacity: drive.busy ? 0.62 : 1.0

                            Item {
                                id: driveBody
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.right: actions.left
                                anchors.rightMargin: 6 * rail.uiScale

                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12 * rail.uiScale
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1 * rail.uiScale

                                    Text {
                                        width: parent.width
                                        text: (drive.active ? "//  " : "") + drive.name
                                        elide: Text.ElideMiddle
                                        color: drive.active ? Ryoku.inkOnBone : Ryoku.inkDim
                                        font.family: Ryoku.uiFont
                                        font.pixelSize: 12 * rail.uiScale
                                        font.weight: Font.Medium
                                    }

                                    Text {
                                        width: parent.width
                                        text: drive.mounted
                                            ? drive.mountPoint
                                            : ((drive.fsType !== "" ? drive.fsType.toUpperCase() + "  ·  " : "") + drive.sizeText)
                                        elide: Text.ElideMiddle
                                        color: drive.active ? Ryoku.inkOnBoneDim : Ryoku.inkFaint
                                        font.family: Ryoku.monoFont
                                        font.pixelSize: 8 * rail.uiScale
                                    }
                                }

                                HoverHandler {
                                    id: driveHover
                                    enabled: !drive.busy
                                    cursorShape: Qt.PointingHandCursor
                                }
                                TapHandler {
                                    enabled: !drive.busy
                                    onTapped: drive.openOrMount()
                                }
                            }

                            Column {
                                id: actions
                                anchors.right: parent.right
                                anchors.rightMargin: 8 * rail.uiScale
                                anchors.verticalCenter: parent.verticalCenter
                                width: 58 * rail.uiScale
                                spacing: 2 * rail.uiScale

                                Rectangle {
                                    width: parent.width
                                    height: 20 * rail.uiScale
                                    radius: 4 * rail.uiScale
                                    color: openHover.hovered && !drive.busy ? Ryoku.tint10 : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: drive.busy ? "…" : (drive.mounted ? "OPEN" : "MOUNT")
                                        color: drive.active ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                                        font.family: Ryoku.monoFont
                                        font.pixelSize: 8 * rail.uiScale
                                        font.letterSpacing: 0.7
                                    }

                                    HoverHandler {
                                        id: openHover
                                        enabled: !drive.busy
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    TapHandler {
                                        enabled: !drive.busy
                                        onTapped: drive.openOrMount()
                                    }
                                }

                                Rectangle {
                                    width: parent.width
                                    height: visible ? 20 * rail.uiScale : 0
                                    visible: drive.mounted || drive.canPowerOffNow
                                    radius: 4 * rail.uiScale
                                    color: secondaryHover.hovered && !drive.busy ? Ryoku.tint10 : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: drive.mounted ? "UNMOUNT" : "POWER OFF"
                                        color: drive.active ? Ryoku.inkOnBoneDim : Ryoku.sun
                                        font.family: Ryoku.monoFont
                                        font.pixelSize: drive.mounted ? 7 * rail.uiScale : 6.5 * rail.uiScale
                                        font.letterSpacing: drive.mounted ? 0.45 : 0.25
                                    }

                                    HoverHandler {
                                        id: secondaryHover
                                        enabled: !drive.busy
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    TapHandler {
                                        enabled: !drive.busy
                                        onTapped: {
                                            if (drive.mounted)
                                                Drives.unmount(drive.objectPath)
                                            else
                                                Drives.powerOff(drive.objectPath)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: Drives.loading && Drives.count === 0
                        text: "    SCANNING STORAGE…"
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * rail.uiScale
                        font.letterSpacing: 0.6
                    }

                    Text {
                        width: parent.width
                        visible: Drives.lastError !== ""
                        text: "// STORAGE_  " + Drives.lastError
                        color: Ryoku.sun
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * rail.uiScale
                        wrapMode: Text.WordWrap
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4 * rail.uiScale

                    Text {
                        text: "03  NETWORK"
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * rail.uiScale
                        font.letterSpacing: 1.5
                        bottomPadding: 3 * rail.uiScale
                    }

                    Repeater {
                        model: NetworkLocations

                        delegate: Rectangle {
                            id: network
                            required property string name
                            required property string uri
                            required property string rootUri
                            required property string scheme
                            required property string host
                            required property bool canUnmount

                            readonly property bool active: rail.pathInside(rail.fs.path, network.rootUri)
                            readonly property bool disconnecting:
                                NetworkDisconnect.busy && NetworkDisconnect.targetRootUri === network.rootUri
                            readonly property bool disconnectAvailable:
                                network.canUnmount && !NetworkDisconnect.busy

                            width: parent.width
                            height: 54 * rail.uiScale
                            radius: 6 * rail.uiScale
                            color: network.active
                                ? Ryoku.bone
                                : (networkHover.hovered && !network.disconnecting ? Ryoku.tint10 : "transparent")
                            border.width: 1
                            border.color: network.active ? Ryoku.bone : Ryoku.lineSoft
                            opacity: network.disconnecting ? 0.62 : 1.0

                            Item {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.right: networkActions.left
                                anchors.rightMargin: 6 * rail.uiScale

                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12 * rail.uiScale
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1 * rail.uiScale

                                    Text {
                                        width: parent.width
                                        text: (network.active ? "//  " : "") + network.name
                                        elide: Text.ElideMiddle
                                        color: network.active ? Ryoku.inkOnBone : Ryoku.inkDim
                                        font.family: Ryoku.uiFont
                                        font.pixelSize: 11 * rail.uiScale
                                        font.weight: Font.Medium
                                    }

                                    Text {
                                        width: parent.width
                                        text: network.scheme.toUpperCase() + "  ·  " + network.host
                                        elide: Text.ElideMiddle
                                        color: network.active ? Ryoku.inkOnBoneDim : Ryoku.inkFaint
                                        font.family: Ryoku.monoFont
                                        font.pixelSize: 8 * rail.uiScale
                                    }
                                }

                                HoverHandler {
                                    id: networkHover
                                    enabled: !network.disconnecting
                                    cursorShape: Qt.PointingHandCursor
                                }
                                TapHandler {
                                    enabled: !network.disconnecting
                                    onTapped: rail.navigate(network.uri)
                                }
                            }

                            Column {
                                id: networkActions
                                anchors.right: parent.right
                                anchors.rightMargin: 8 * rail.uiScale
                                anchors.verticalCenter: parent.verticalCenter
                                width: 64 * rail.uiScale
                                spacing: 2 * rail.uiScale

                                Rectangle {
                                    width: parent.width
                                    height: 20 * rail.uiScale
                                    radius: 4 * rail.uiScale
                                    color: networkOpenHover.hovered && !network.disconnecting
                                        ? Ryoku.tint10 : "transparent"
                                    Text {
                                        anchors.centerIn: parent
                                        text: network.disconnecting ? "…" : "OPEN"
                                        color: network.active ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                                        font.family: Ryoku.monoFont
                                        font.pixelSize: 8 * rail.uiScale
                                        font.letterSpacing: 0.7
                                    }
                                    HoverHandler {
                                        id: networkOpenHover
                                        enabled: !network.disconnecting
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    TapHandler {
                                        enabled: !network.disconnecting
                                        onTapped: rail.navigate(network.uri)
                                    }
                                }

                                Rectangle {
                                    width: parent.width
                                    height: network.canUnmount ? 20 * rail.uiScale : 0
                                    visible: network.canUnmount
                                    radius: 4 * rail.uiScale
                                    color: networkDisconnectHover.hovered && network.disconnectAvailable
                                        ? Ryoku.tint10 : "transparent"
                                    opacity: network.disconnectAvailable || network.disconnecting ? 1.0 : 0.45
                                    Text {
                                        anchors.centerIn: parent
                                        text: network.disconnecting ? "CANCEL" : "DISCONNECT"
                                        color: network.active ? Ryoku.inkOnBoneDim : Ryoku.sun
                                        font.family: Ryoku.monoFont
                                        font.pixelSize: 6.4 * rail.uiScale
                                        font.letterSpacing: 0.15
                                    }
                                    HoverHandler {
                                        id: networkDisconnectHover
                                        enabled: network.disconnectAvailable || network.disconnecting
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    TapHandler {
                                        enabled: network.disconnectAvailable || network.disconnecting
                                        onTapped: {
                                            if (network.disconnecting)
                                                NetworkDisconnect.cancel()
                                            else
                                                NetworkDisconnect.disconnectFrom(network.rootUri)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: NetworkLocations.count === 0
                        text: "    NO NETWORK MOUNTS"
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * rail.uiScale
                        font.letterSpacing: 0.5
                    }

                    Text {
                        width: parent.width
                        visible: NetworkDisconnect.lastError !== ""
                        text: "// NETWORK_  " + NetworkDisconnect.lastError
                        color: Ryoku.sun
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * rail.uiScale
                        wrapMode: Text.WordWrap
                    }
                }

                Column {
                    width: parent.width
                    spacing: 2 * rail.uiScale

                    Text {
                        text: "04  SYSTEM"
                        color: Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 9 * rail.uiScale
                        font.letterSpacing: 1.5
                        bottomPadding: 5 * rail.uiScale
                    }

                    Rectangle {
                        id: trashButton
                        width: parent.width
                        height: 34 * rail.uiScale
                        radius: 6 * rail.uiScale
                        color: rail.trashActive
                            ? Ryoku.bone
                            : (trashHover.hovered ? Ryoku.tint10 : "transparent")

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 12 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            text: (rail.trashActive ? "//  " : "    ") + "Trash"
                            color: rail.trashActive ? Ryoku.inkOnBone : Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 13 * rail.uiScale
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 12 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            text: rail.trashCount > 0 ? String(rail.trashCount) : ""
                            color: rail.trashActive ? Ryoku.inkOnBoneDim : Ryoku.inkMuted
                            font.family: Ryoku.monoFont
                            font.pixelSize: 9 * rail.uiScale
                        }

                        HoverHandler { id: trashHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: rail.openTrash() }
                    }
                }

                Text {
                    width: parent.width
                    text: "Ryoku-native file manager\nPhase 4 // network"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * rail.uiScale
                    lineHeight: 1.4
                    wrapMode: Text.WordWrap
                    bottomPadding: 8 * rail.uiScale
                }
            }
        }
    }
}
