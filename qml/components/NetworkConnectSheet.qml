// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ryofiles.Core

Window {
    id: root

    property real uiScale: 1
    property string errorText: ""
    signal connected(string uri)

    width: 560 * uiScale
    height: Math.max(340 * uiScale, contentColumn.implicitHeight + 40 * uiScale)
    minimumWidth: 420 * uiScale
    minimumHeight: 300 * uiScale
    visible: false
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    title: "Ryofiles Network"
    color: Ryoku.paperLift

    function clearSecrets() {
        passwordField.text = ""
    }

    function resetFields() {
        clearSecrets()
        locationField.text = ""
        userField.text = ""
        domainField.text = ""
        anonymousToggle.checked = false
        errorText = ""
    }

    function open() {
        resetFields()
        visible = true
        raise()
        requestActivate()
        Qt.callLater(function() {
            locationField.forceActiveFocus()
        })
    }

    function closeDialog() {
        clearSecrets()
        errorText = ""
        if (NetworkConnection.busy)
            NetworkConnection.cancel()
        else
            visible = false
    }

    function submitLocation() {
        var location = locationField.text.trim()
        if (location === "")
            return
        clearSecrets()
        errorText = ""
        if (!NetworkConnection.connectTo(location))
            errorText = NetworkConnection.lastError
    }

    function submitCredentials() {
        errorText = ""
        NetworkConnection.submitCredentials(
            userField.text,
            passwordField.text,
            domainField.text,
            anonymousToggle.checked)
        clearSecrets()
    }

    onClosing: function(close) {
        clearSecrets()
        errorText = ""
        if (NetworkConnection.busy)
            NetworkConnection.cancel()
    }

    Rectangle {
        anchors.fill: parent
        color: Ryoku.paperLift
        border.width: 1
        border.color: Ryoku.lineStrong
    }

    Column {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20 * root.uiScale
        spacing: 12 * root.uiScale

        Text {
            text: "// CONNECT NETWORK LOCATION"
            color: Ryoku.ink
            font.family: Ryoku.monoFont
            font.pixelSize: 11 * root.uiScale
            font.letterSpacing: 1.2
        }

        Text {
            width: parent.width
            text: NetworkConnection.awaitingCredentials || NetworkConnection.awaitingChoice
                ? NetworkConnection.promptMessage
                : "SFTP, SMB, WebDAV or FTP URI"
            color: Ryoku.inkMuted
            font.family: Ryoku.uiFont
            font.pixelSize: 10 * root.uiScale
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: parent.width
            height: 38 * root.uiScale
            visible: !NetworkConnection.awaitingCredentials && !NetworkConnection.awaitingChoice
            radius: 6 * root.uiScale
            color: "transparent"
            border.width: locationField.activeFocus ? 2 : 1
            border.color: locationField.activeFocus ? Ryoku.ink : Ryoku.line

            TextInput {
                id: locationField
                anchors.fill: parent
                anchors.leftMargin: 10 * root.uiScale
                anchors.rightMargin: 10 * root.uiScale
                verticalAlignment: Text.AlignVCenter
                color: Ryoku.ink
                selectionColor: Ryoku.bone
                selectedTextColor: Ryoku.inkOnBone
                font.family: Ryoku.monoFont
                font.pixelSize: 11 * root.uiScale
                selectByMouse: true
                enabled: !NetworkConnection.busy
                onTextEdited: root.errorText = ""
                onAccepted: root.submitLocation()
            }
        }

        Column {
            width: parent.width
            spacing: 8 * root.uiScale
            visible: NetworkConnection.awaitingCredentials

            Rectangle {
                width: parent.width
                height: NetworkConnection.needsUserName ? 38 * root.uiScale : 0
                visible: NetworkConnection.needsUserName
                radius: 6 * root.uiScale
                color: "transparent"
                border.width: userField.activeFocus ? 2 : 1
                border.color: userField.activeFocus ? Ryoku.ink : Ryoku.line
                TextInput {
                    id: userField
                    anchors.fill: parent
                    anchors.leftMargin: 10 * root.uiScale
                    anchors.rightMargin: 10 * root.uiScale
                    verticalAlignment: Text.AlignVCenter
                    color: Ryoku.ink
                    selectionColor: Ryoku.bone
                    selectedTextColor: Ryoku.inkOnBone
                    font.family: Ryoku.uiFont
                    font.pixelSize: 11 * root.uiScale
                    selectByMouse: true
                }
            }

            Rectangle {
                width: parent.width
                height: NetworkConnection.needsDomain ? 38 * root.uiScale : 0
                visible: NetworkConnection.needsDomain
                radius: 6 * root.uiScale
                color: "transparent"
                border.width: domainField.activeFocus ? 2 : 1
                border.color: domainField.activeFocus ? Ryoku.ink : Ryoku.line
                TextInput {
                    id: domainField
                    anchors.fill: parent
                    anchors.leftMargin: 10 * root.uiScale
                    anchors.rightMargin: 10 * root.uiScale
                    verticalAlignment: Text.AlignVCenter
                    color: Ryoku.ink
                    selectionColor: Ryoku.bone
                    selectedTextColor: Ryoku.inkOnBone
                    font.family: Ryoku.uiFont
                    font.pixelSize: 11 * root.uiScale
                    selectByMouse: true
                }
            }

            Rectangle {
                width: parent.width
                height: NetworkConnection.needsPassword && !anonymousToggle.checked
                    ? 38 * root.uiScale : 0
                visible: NetworkConnection.needsPassword && !anonymousToggle.checked
                radius: 6 * root.uiScale
                color: "transparent"
                border.width: passwordField.activeFocus ? 2 : 1
                border.color: passwordField.activeFocus ? Ryoku.ink : Ryoku.line
                TextInput {
                    id: passwordField
                    anchors.fill: parent
                    anchors.leftMargin: 10 * root.uiScale
                    anchors.rightMargin: 10 * root.uiScale
                    verticalAlignment: Text.AlignVCenter
                    color: Ryoku.ink
                    selectionColor: Ryoku.bone
                    selectedTextColor: Ryoku.inkOnBone
                    font.family: Ryoku.uiFont
                    font.pixelSize: 11 * root.uiScale
                    echoMode: TextInput.Password
                    selectByMouse: true
                    onAccepted: root.submitCredentials()
                }
            }

            Rectangle {
                id: anonymousToggle
                property bool checked: false
                width: parent.width
                height: NetworkConnection.anonymousSupported ? 34 * root.uiScale : 0
                visible: NetworkConnection.anonymousSupported
                radius: 6 * root.uiScale
                color: checked ? Ryoku.bone : "transparent"
                border.width: 1
                border.color: checked ? Ryoku.bone : Ryoku.line
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10 * root.uiScale
                    anchors.verticalCenter: parent.verticalCenter
                    text: (anonymousToggle.checked ? "//  " : "    ") + "Connect anonymously"
                    color: anonymousToggle.checked ? Ryoku.inkOnBone : Ryoku.inkDim
                    font.family: Ryoku.uiFont
                    font.pixelSize: 10 * root.uiScale
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        anonymousToggle.checked = !anonymousToggle.checked
                        root.clearSecrets()
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: 6 * root.uiScale
            visible: NetworkConnection.awaitingChoice

            Repeater {
                model: NetworkConnection.choices
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: parent.width
                    height: 34 * root.uiScale
                    radius: 6 * root.uiScale
                    color: choiceHover.hovered ? Ryoku.tint10 : "transparent"
                    border.width: 1
                    border.color: Ryoku.line
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10 * root.uiScale
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        color: Ryoku.inkDim
                        font.family: Ryoku.uiFont
                        font.pixelSize: 10 * root.uiScale
                    }
                    HoverHandler { id: choiceHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            root.errorText = ""
                            NetworkConnection.submitChoice(index)
                        }
                    }
                }
            }
        }

        Text {
            width: parent.width
            visible: root.errorText !== ""
            text: "// NETWORK_  " + root.errorText
            color: Ryoku.sun
            font.family: Ryoku.monoFont
            font.pixelSize: 9 * root.uiScale
            wrapMode: Text.WordWrap
        }

        Row {
            anchors.right: parent.right
            spacing: 8 * root.uiScale

            Rectangle {
                width: 86 * root.uiScale
                height: 30 * root.uiScale
                radius: 6 * root.uiScale
                color: cancelHover.hovered ? Ryoku.tint10 : "transparent"
                border.width: 1
                border.color: Ryoku.line
                Text {
                    anchors.centerIn: parent
                    text: NetworkConnection.busy ? "CANCEL" : "CLOSE"
                    color: Ryoku.inkDim
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * root.uiScale
                    font.weight: Font.Medium
                }
                HoverHandler { id: cancelHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.closeDialog() }
            }

            Rectangle {
                width: 98 * root.uiScale
                height: 30 * root.uiScale
                visible: !NetworkConnection.awaitingChoice
                radius: 6 * root.uiScale
                color: Ryoku.bone
                border.width: 1
                border.color: Ryoku.bone
                opacity: NetworkConnection.busy && !NetworkConnection.awaitingCredentials ? 0.6 : 1.0
                Text {
                    anchors.centerIn: parent
                    text: NetworkConnection.awaitingCredentials
                        ? "CONTINUE"
                        : (NetworkConnection.busy ? "CONNECTING…" : "CONNECT")
                    color: Ryoku.inkOnBone
                    font.family: Ryoku.uiFont
                    font.pixelSize: 9 * root.uiScale
                    font.weight: Font.Medium
                }
                HoverHandler {
                    enabled: !NetworkConnection.busy || NetworkConnection.awaitingCredentials
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    enabled: !NetworkConnection.busy || NetworkConnection.awaitingCredentials
                    onTapped: {
                        if (NetworkConnection.awaitingCredentials)
                            root.submitCredentials()
                        else
                            root.submitLocation()
                    }
                }
            }
        }
    }

    Connections {
        target: NetworkConnection

        function onPromptChanged() {
            if (!root.visible)
                return
            root.clearSecrets()
            root.errorText = ""
            if (NetworkConnection.awaitingCredentials) {
                userField.text = NetworkConnection.suggestedUserName
                domainField.text = NetworkConnection.suggestedDomain
                anonymousToggle.checked = false
                Qt.callLater(function() {
                    if (NetworkConnection.needsUserName)
                        userField.forceActiveFocus()
                    else if (NetworkConnection.needsPassword)
                        passwordField.forceActiveFocus()
                })
            }
        }

        function onConnected(uri) {
            root.clearSecrets()
            root.errorText = ""
            root.visible = false
            root.connected(uri)
        }

        function onConnectionFailed(uri, error) {
            root.clearSecrets()
            root.errorText = error
            root.visible = true
            root.raise()
            root.requestActivate()
        }

        function onConnectionCancelled(uri) {
            root.resetFields()
            root.visible = false
        }
    }

    Component.onDestruction: root.clearSecrets()
}
