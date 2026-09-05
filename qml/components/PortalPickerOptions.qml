// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Rectangle {
    id: root

    property real uiScale: 1
    property var files: null
    property var session: null

    readonly property bool hasPortalOptions:
        PortalContext.active
        && (PortalContext.filters.length > 0 || PortalContext.choices.length > 0)

    signal closeRequested()

    implicitWidth: 420 * uiScale
    implicitHeight: Math.min(
        360 * uiScale,
        Math.max(96 * uiScale, optionContent.implicitHeight + 24 * uiScale))
    height: implicitHeight
    radius: 8 * uiScale
    color: Ryoku.paperLift
    border.width: 1
    border.color: Ryoku.line
    clip: true

    function syncPortalNameFilters() {
        if (!root.files)
            return
        root.files.portalNameFilters = PortalContext.active
            ? PortalContext.selectedFilterPatterns
            : []
    }

    function choiceDisplay(choice) {
        if (!choice || !choice.options)
            return ""
        for (var i = 0; i < choice.options.length; ++i) {
            if (choice.options[i].id === choice.selected)
                return choice.options[i].label
        }
        return choice.selected || ""
    }

    onFilesChanged: syncPortalNameFilters()
    Component.onCompleted: syncPortalNameFilters()

    Connections {
        target: PortalContext
        function onSelectedFilterChanged() {
            if (root.session)
                root.session.clearSelection()
            root.syncPortalNameFilters()
        }
    }

    Flickable {
        id: optionFlick
        anchors.fill: parent
        anchors.margins: 12 * root.uiScale
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: optionContent.implicitHeight

        Column {
            id: optionContent
            width: optionFlick.width
            spacing: 7 * root.uiScale

            Item {
                width: parent.width
                height: 24 * root.uiScale

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "PORTAL OPTIONS"
                    color: Ryoku.ink
                    font.family: Ryoku.monoFont
                    font.pixelSize: 9 * root.uiScale
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "CLOSE"
                    color: closeHover.hovered ? Ryoku.ink : Ryoku.inkMuted
                    font.family: Ryoku.monoFont
                    font.pixelSize: 8 * root.uiScale

                    HoverHandler {
                        id: closeHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: root.closeRequested()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Ryoku.line
            }

            Text {
                visible: PortalContext.filters.length > 0
                width: parent.width
                text: PortalContext.filterLocked ? "FILE TYPE · FIXED BY APPLICATION" : "FILE TYPE"
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.uiScale
                font.letterSpacing: 0.7
            }

            Repeater {
                model: PortalContext.filters

                delegate: Rectangle {
                    id: filterRow
                    required property int index
                    required property var modelData

                    readonly property bool selected: PortalContext.selectedFilterIndex === index
                    width: optionContent.width
                    height: 34 * root.uiScale
                    radius: 6 * root.uiScale
                    color: selected ? Ryoku.bone : (filterHover.hovered ? Ryoku.tint10 : "transparent")
                    border.width: 1
                    border.color: selected ? Ryoku.bone : Ryoku.line
                    opacity: PortalContext.filterLocked && !selected ? 0.55 : 1.0

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10 * root.uiScale
                        anchors.right: filterState.left
                        anchors.rightMargin: 8 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: filterRow.modelData.name !== ""
                            ? filterRow.modelData.name
                            : "File type " + (filterRow.index + 1)
                        elide: Text.ElideRight
                        color: filterRow.selected ? Ryoku.inkOnBone : Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10 * root.uiScale
                    }

                    Text {
                        id: filterState
                        anchors.right: parent.right
                        anchors.rightMargin: 10 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: filterRow.selected
                            ? (PortalContext.filterLocked ? "FIXED" : "SELECTED")
                            : ""
                        color: filterRow.selected ? Ryoku.inkOnBoneDim : Ryoku.inkFaint
                        font.family: Ryoku.monoFont
                        font.pixelSize: 7 * root.uiScale
                    }

                    HoverHandler {
                        id: filterHover
                        enabled: !PortalContext.filterLocked
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                    TapHandler {
                        enabled: !PortalContext.filterLocked
                        onTapped: PortalContext.selectedFilterIndex = filterRow.index
                    }
                }
            }

            Rectangle {
                visible: PortalContext.filters.length > 0 && PortalContext.choices.length > 0
                width: parent.width
                height: 1
                color: Ryoku.line
            }

            Text {
                visible: PortalContext.choices.length > 0
                width: parent.width
                text: "APPLICATION OPTIONS"
                color: Ryoku.inkFaint
                font.family: Ryoku.monoFont
                font.pixelSize: 8 * root.uiScale
                font.letterSpacing: 0.7
            }

            Repeater {
                model: PortalContext.choices

                delegate: Rectangle {
                    id: choiceRow
                    required property int index
                    required property var modelData

                    width: optionContent.width
                    height: 38 * root.uiScale
                    radius: 6 * root.uiScale
                    color: choiceHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10 * root.uiScale
                        anchors.right: choiceValue.left
                        anchors.rightMargin: 10 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: choiceRow.modelData.label
                        elide: Text.ElideRight
                        color: Ryoku.ink
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10 * root.uiScale
                    }

                    Text {
                        id: choiceValue
                        anchors.right: parent.right
                        anchors.rightMargin: 10 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.choiceDisplay(choiceRow.modelData) + "  ›"
                        color: Ryoku.inkMuted
                        font.family: Ryoku.monoFont
                        font.pixelSize: 8 * root.uiScale
                    }

                    HoverHandler {
                        id: choiceHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: {
                            const next = PortalContext.nextChoiceSelection(choiceRow.modelData.id)
                            if (next !== "")
                                PortalContext.setChoiceSelection(choiceRow.modelData.id, next)
                        }
                    }
                }
            }
        }
    }
}
