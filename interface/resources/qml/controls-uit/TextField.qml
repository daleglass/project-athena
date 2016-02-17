//
//  TextField.qml
//
//  Created by David Rowe on 17 Feb 2016
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Controls.Styles 1.4

import "../styles-uit"

TextField {
    id: textField

    property int colorScheme: hifi.colorSchemes.light
    property string label: ""

    FontLoader { id: firaSansSemiBold; source: "../../fonts/FiraSans-SemiBold.ttf"; }
    font.pointSize: 16

    style:  TextFieldStyle {
        textColor: textField.colorScheme == hifi.colorSchemes.light
                   ? (textField.focus ? hifi.colors.black : hifi.colors.lightGray)
                   : (textField.focus ? hifi.colors.white : hifi.colors.lightGrayText)
        background: Rectangle {
            color: textField.colorScheme == hifi.colorSchemes.light
                   ? (textField.focus ? hifi.colors.white : hifi.colors.lightGray)
                   : (textField.focus ? hifi.colors.black : hifi.colors.baseGrayShadow)
            border.color: hifi.colors.primaryHighlight
            border.width: textField.focus ? 1 : 0
        }
        placeholderTextColor: hifi.colors.lightGray
        selectedTextColor: hifi.colors.black
        selectionColor: hifi.colors.primaryHighlight
        padding.left: hifi.dimensions.textPadding
        padding.right: hifi.dimensions.textPadding
    }

    RalewaySemibold {
        text: textField.label
        size: 13.5
        color: hifi.colors.lightGray
        anchors.left: parent.left
        anchors.bottom: parent.top
        anchors.bottomMargin: 4
        visible: label != ""
    }
}
