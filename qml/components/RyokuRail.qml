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

    width: 228 * uiScale

    NetworkConnectSheet {
        id: networkConnect
        uiScale: rail.uiScale
        onConnected: uri => rail.navigate(uri)
    }

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
        anchors.fill: parent
        color: Ryoku.paperLift
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Ryoku.lineSoft
    }

    Item {
        id: brand
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 62 * rail.uiScale

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 18 * rail.uiScale
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10 * rail.uiScale

            Rectangle {
                width: 30 * rail.uiScale
                height: 30 * rail.uiScale
                radius: 8 * rail.uiScale
                color: Ryoku.tint10
                Text {
                    anchors.centerIn: parent
                    text: "力"
                    color: Ryoku.ink
                    font.family: "Noto Sans CJK JP"
                    font.pixelSize: 17 * rail.uiScale
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1 * rail.uiScale
                Text {
                    text: "Ryofiles"
                    color: Ryoku.ink
                    font.family: Ryoku.uiFont
                    font.pixelSize: 13 * rail.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "File manager for Ryoku"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8.5 * rail.uiScale
                }
            }
        }
    }

    Flickable {
        id: sections
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: brand.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10 * rail.uiScale
        anchors.rightMargin: 10 * rail.uiScale
        anchors.bottomMargin: 10 * rail.uiScale
        contentWidth: width
        contentHeight: sectionColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: sectionColumn
            width: sections.width
            spacing: 16 * rail.uiScale

            Column {
                width: parent.width
                spacing: 2 * rail.uiScale

                Text {
                    text: "Places"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * rail.uiScale
                    font.weight: Font.Medium
                    leftPadding: 8 * rail.uiScale
                    bottomPadding: 5 * rail.uiScale
                }

                Repeater {
                    model: [
                        { icon: "⌂", label: "Home", path: rail.fs.home },
                        { icon: "▣", label: "Desktop", path: rail.fs.desktop },
                        { icon: "≡", label: "Documents", path: rail.fs.documents },
                        { icon: "↓", label: "Downloads", path: rail.fs.downloads },
                        { icon: "◇", label: "Pictures", path: rail.fs.pictures },
                        { icon: "♪", label: "Music", path: rail.fs.music },
                        { icon: "▶", label: "Videos", path: rail.fs.videos }
                    ]

                    delegate: Rectangle {
                        id: place
                        required property var modelData
                        readonly property bool active: !rail.trashActive && rail.fs.path === modelData.path
                        width: parent.width
                        height: 34 * rail.uiScale
                        radius: 7 * rail.uiScale
                        color: active ? Ryoku.tint10 : (placeHover.hovered ? Ryoku.tint5 : "transparent")

                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3 * rail.uiScale
                            height: active ? 18 * rail.uiScale : 0
                            radius: 2 * rail.uiScale
                            color: Ryoku.sun
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22 * rail.uiScale
                            horizontalAlignment: Text.AlignHCenter
                            text: place.modelData.icon
                            color: active ? Ryoku.ink : Ryoku.inkMuted
                            font.family: Ryoku.uiFont
                            font.pixelSize: 13 * rail.uiScale
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 40 * rail.uiScale
                            anchors.right: parent.right
                            anchors.rightMargin: 10 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            text: place.modelData.label
                            elide: Text.ElideRight
                            color: active ? Ryoku.ink : Ryoku.inkDim
                            font.family: Ryoku.uiFont
                            font.pixelSize: 11 * rail.uiScale
                            font.weight: active ? Font.Medium : Font.Normal
                        }

                        HoverHandler { id: placeHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: rail.navigate(place.modelData.path) }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 3 * rail.uiScale
                visible: Drives.count > 0 || Drives.loading || Drives.lastError !== ""

                Text {
                    text: "Devices"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * rail.uiScale
                    font.weight: Font.Medium
                    leftPadding: 8 * rail.uiScale
                    bottomPadding: 4 * rail.uiScale
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
                                if (drive.mountPoint !== "") rail.navigate(drive.mountPoint)
                            } else {
                                Drives.mount(drive.objectPath)
                            }
                        }

                        width: parent.width
                        height: 48 * rail.uiScale
                        radius: 7 * rail.uiScale
                        color: drive.active ? Ryoku.tint10 : (driveHover.hovered ? Ryoku.tint5 : "transparent")
                        opacity: drive.busy ? 0.55 : 1.0

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            text: drive.removable ? "◫" : "▤"
                            color: drive.active ? Ryoku.ink : Ryoku.inkMuted
                            font.family: Ryoku.uiFont
                            font.pixelSize: 14 * rail.uiScale
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 38 * rail.uiScale
                            anchors.right: driveAction.left
                            anchors.rightMargin: 6 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1 * rail.uiScale
                            Text {
                                width: parent.width
                                text: drive.name
                                elide: Text.ElideMiddle
                                color: Ryoku.inkDim
                                font.family: Ryoku.uiFont
                                font.pixelSize: 10.5 * rail.uiScale
                                font.weight: Font.Medium
                            }
                            Text {
                                width: parent.width
                                text: drive.mounted
                                    ? drive.mountPoint
                                    : ((drive.fsType !== "" ? drive.fsType.toUpperCase() + " · " : "") + drive.sizeText)
                                elide: Text.ElideMiddle
                                color: Ryoku.inkFaint
                                font.family: Ryoku.monoFont
                                font.pixelSize: 7.5 * rail.uiScale
                            }
                        }

                        Text {
                            id: driveAction
                            anchors.right: parent.right
                            anchors.rightMargin: 10 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            text: drive.busy ? "…" : (drive.mounted ? "⏏" : "+")
                            color: driveActionHover.hovered ? Ryoku.ink : Ryoku.inkMuted
                            font.family: Ryoku.uiFont
                            font.pixelSize: 13 * rail.uiScale
                            HoverHandler { id: driveActionHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    if (drive.busy) return
                                    if (drive.mounted) Drives.unmount(drive.objectPath)
                                    else drive.openOrMount()
                                }
                            }
                        }

                        HoverHandler { id: driveHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: drive.openOrMount() }
                    }
                }

                Text {
                    width: parent.width
                    visible: Drives.loading && Drives.count === 0
                    text: "Scanning storage…"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * rail.uiScale
                    leftPadding: 8 * rail.uiScale
                }
                Text {
                    width: parent.width
                    visible: Drives.lastError !== ""
                    text: Drives.lastError
                    color: Ryoku.sun
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * rail.uiScale
                    wrapMode: Text.WordWrap
                    leftPadding: 8 * rail.uiScale
                }
            }

            Column {
                width: parent.width
                spacing: 3 * rail.uiScale

                Text {
                    text: "Network"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * rail.uiScale
                    font.weight: Font.Medium
                    leftPadding: 8 * rail.uiScale
                    bottomPadding: 4 * rail.uiScale
                }

                Rectangle {
                    width: parent.width
                    height: 34 * rail.uiScale
                    radius: 7 * rail.uiScale
                    color: connectHover.hovered ? Ryoku.tint5 : "transparent"
                    opacity: NetworkConnection.busy ? 0.55 : 1.0
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12 * rail.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⌁"
                        color: Ryoku.inkMuted
                        font.family: Ryoku.uiFont
                        font.pixelSize: 13 * rail.uiScale
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 40 * rail.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: NetworkConnection.busy ? "Connecting…" : "Connect location"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10.5 * rail.uiScale
                    }
                    HoverHandler { id: connectHover; enabled: !NetworkConnection.busy; cursorShape: Qt.PointingHandCursor }
                    TapHandler { enabled: !NetworkConnection.busy; onTapped: networkConnect.open() }
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

                        width: parent.width
                        height: 44 * rail.uiScale
                        radius: 7 * rail.uiScale
                        color: network.active ? Ryoku.tint10 : (networkHover.hovered ? Ryoku.tint5 : "transparent")
                        opacity: network.disconnecting ? 0.55 : 1.0

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 12 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            text: "⌁"
                            color: Ryoku.inkMuted
                            font.family: Ryoku.uiFont
                            font.pixelSize: 12 * rail.uiScale
                        }
                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 40 * rail.uiScale
                            anchors.right: disconnectButton.left
                            anchors.rightMargin: 5 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0
                            Text {
                                width: parent.width
                                text: network.name
                                elide: Text.ElideRight
                                color: Ryoku.inkDim
                                font.family: Ryoku.uiFont
                                font.pixelSize: 10 * rail.uiScale
                                font.weight: Font.Medium
                            }
                            Text {
                                width: parent.width
                                text: network.scheme.toUpperCase() + " · " + network.host
                                elide: Text.ElideRight
                                color: Ryoku.inkFaint
                                font.family: Ryoku.monoFont
                                font.pixelSize: 7 * rail.uiScale
                            }
                        }
                        Text {
                            id: disconnectButton
                            anchors.right: parent.right
                            anchors.rightMargin: 10 * rail.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            visible: network.canUnmount
                            text: network.disconnecting ? "×" : "⏏"
                            color: disconnectHover.hovered ? Ryoku.sun : Ryoku.inkMuted
                            font.family: Ryoku.uiFont
                            font.pixelSize: 12 * rail.uiScale
                            HoverHandler { id: disconnectHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    if (network.disconnecting) NetworkDisconnect.cancel()
                                    else if (!NetworkDisconnect.busy) NetworkDisconnect.disconnectFrom(network.rootUri)
                                }
                            }
                        }
                        HoverHandler { id: networkHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: if (!network.disconnecting) rail.navigate(network.uri) }
                    }
                }

                Text {
                    width: parent.width
                    visible: NetworkLocations.count === 0
                    text: "No mounted network locations"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * rail.uiScale
                    leftPadding: 8 * rail.uiScale
                }
                Text {
                    width: parent.width
                    visible: NetworkDisconnect.lastError !== ""
                    text: NetworkDisconnect.lastError
                    color: Ryoku.sun
                    font.family: Ryoku.uiFont
                    font.pixelSize: 8 * rail.uiScale
                    wrapMode: Text.WordWrap
                    leftPadding: 8 * rail.uiScale
                }
            }

            Column {
                width: parent.width
                spacing: 3 * rail.uiScale

                Text {
                    text: "System"
                    color: Ryoku.inkFaint
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * rail.uiScale
                    font.weight: Font.Medium
                    leftPadding: 8 * rail.uiScale
                    bottomPadding: 4 * rail.uiScale
                }

                Rectangle {
                    id: trashButton
                    width: parent.width
                    height: 34 * rail.uiScale
                    radius: 7 * rail.uiScale
                    color: rail.trashActive ? Ryoku.tint10 : (trashHover.hovered ? Ryoku.tint5 : "transparent")

                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3 * rail.uiScale
                        height: rail.trashActive ? 18 * rail.uiScale : 0
                        radius: 2 * rail.uiScale
                        color: Ryoku.sun
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12 * rail.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⌫"
                        color: Ryoku.inkMuted
                        font.family: Ryoku.uiFont
                        font.pixelSize: 13 * rail.uiScale
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 40 * rail.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Trash"
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10.5 * rail.uiScale
                        font.weight: rail.trashActive ? Font.Medium : Font.Normal
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10 * rail.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: rail.trashCount > 0 ? String(rail.trashCount) : ""
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * rail.uiScale
                    }
                    HoverHandler { id: trashHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: rail.openTrash() }
                }
            }

            Item { width: 1; height: 8 * rail.uiScale }
        }
    }
}
