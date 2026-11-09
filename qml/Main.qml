// ─────────────────────────────────────────────────────────────────────────
//  Main.qml — главный экран IAMVPN (white-label AM.SALES VPN).
//
//  Левая стеклянная панель с двумя вкладками: VPN (ключи/серверы) и
//  Устройства (сканер сети). По центру — главная кнопка: включает VPN
//  (туннель через sing-box) + обход DPI (zapret) вместе.
// ─────────────────────────────────────────────────────────────────────────
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Controls.Basic
import Qt.labs.platform as Platform
import IAMVPN

Window {
    id: win
    width: 1040
    height: 700
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: qsTr("IAMVPN")
    color: "#070806"

    // Ширина левой панели (можно тянуть за правую границу).
    property real panelW: 340

    readonly property color accent: "#9FE812"
    readonly property color textHi: "#F4F6F0"
    readonly property color textLo: "#7E8A72"

    // ── Красивый шрифт Manrope (variable). Применяем ко всему. ──────────
    FontLoader {
        id: manrope
        source: "qrc:/qt/qml/IAMVPN/assets/fonts/Manrope-Regular.ttf"
    }
    readonly property string uiFont: manrope.status === FontLoader.Ready
                                     ? manrope.name : "Segoe UI"

    // Главное состояние "включено" = VPN подключён (или подключается).
    readonly property bool isOn: Vpn.connected
    readonly property bool isBusy: Vpn.connecting || Zapret.searching

    // Флаг "реально выходим" (через пункт трея Выход), иначе закрытие = в трей.
    property bool reallyQuit: false

    // ── Иконка в системном трее ─────────────────────────────────────────
    // Меню рисуется НАТИВНЫМ Windows-API (TrackPopupMenu) — это
    // настоящее системное меню как у Steam/Discord/OBS. Открывается ровно
    // у курсора, нормально себя ведёт под elevated-процессом, не зависит
    // от QML-позиционирования. См. src/TrayMenuHelper.h.
    Platform.SystemTrayIcon {
        id: tray
        visible: true
        icon.source: "qrc:/qt/qml/IAMVPN/assets/tray.png"
        tooltip: win.isOn ? qsTr("IAMVPN — подключено")
                          : qsTr("IAMVPN — отключено")

        onActivated: function(reason) {
            Logs.log("TRAY", "DBG", "activated reason=" + reason);
            // Win11 + elevated: Qt не различает ЛКМ/ПКМ (UIPI режет
            // WM_RBUTTONUP), и GetAsyncKeyState уже видит кнопку
            // отпущенной к моменту onActivated. Поэтому поведение как у
            // Discord: одиночный клик ЛЮБОЙ кнопкой = меню,
            // двойной клик = открыть окно.
            if (reason === Platform.SystemTrayIcon.DoubleClick) {
                win.showWindow();
            } else {
                Logs.log("TRAY", "DBG", "calling TrayMenu.show");
                TrayMenu.show(win.isOn);
                Logs.log("TRAY", "DBG", "returned from TrayMenu.show");
            }
        }
    }

    // Слот для пункта "Открыть окно" — Qt-сигнал прилетает из C++.
    Connections {
        target: TrayMenu
        function onOpenWindow() { win.showWindow(); }
    }

    // Показать и поднять окно из трея.
    function showWindow() {
        win.show();
        win.raise();
        win.requestActivate();
    }

    // Системный крестик окна (заголовок Windows) — РЕАЛЬНО закрывает
    // приложение. Если нужно свернуть в трей — есть кнопка "—" в шапке.
    onClosing: function(close) {
        win.reallyQuit = true;
        // close.accepted = true (по умолчанию) — окно закрывается, и так
        // как setQuitOnLastWindowClosed(false), Qt.quit() добиваем сами.
        Qt.quit();
    }

    // ── Фон: глубокий градиент + неоновые световые пятна ────────────────
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#08110A" }
            GradientStop { position: 0.5; color: "#060806" }
            GradientStop { position: 1.0; color: "#0A0E08" }
        }
    }

    // Очень деликатное зелёное пятно (верх-лево), еле заметное.
    Rectangle {
        x: -160; y: -120
        width: 380; height: 380; radius: width/2
        color: win.accent
        opacity: 0.035
        layer.enabled: true
        layer.effect: MultiEffect { blurEnabled: true; blur: 1.0; blurMax: 120 }
    }

    StarField { anchors.fill: parent; particleColor: win.accent }

    // Центральное свечение за кнопкой — слабое, чуть ярче при подключении.
    Rectangle {
        anchors.centerIn: contentArea
        width: 460; height: 460; radius: width / 2
        color: win.accent
        opacity: win.isOn ? 0.08 : 0.025
        Behavior on opacity { NumberAnimation { duration: 800 } }
        layer.enabled: true
        layer.effect: MultiEffect { blurEnabled: true; blur: 1.0; blurMax: 100 }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ═══════════ ЛЕВАЯ ПАНЕЛЬ (стекло) ═══════════
        Rectangle {
            id: leftPanel
            Layout.preferredWidth: win.panelW
            Layout.minimumWidth: 280
            Layout.maximumWidth: 540
            Layout.fillHeight: true
            // Полупрозрачное стекло поверх неонового фона.
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1,1,1,0.06) }
                GradientStop { position: 1.0; color: Qt.rgba(1,1,1,0.025) }
            }

            // Тонкая разделительная грань справа (почти незаметная).
            Rectangle {
                anchors.right: parent.right
                width: 1; height: parent.height
                color: Qt.rgba(1,1,1,0.07)
            }

            // «Хваталка» для перетаскивания границы.
            MouseArea {
                anchors.right: parent.right
                anchors.top: parent.top; anchors.bottom: parent.bottom
                width: 6
                cursorShape: Qt.SplitHCursor
                hoverEnabled: true
                preventStealing: true
                property real startX: 0
                property real startW: 0
                onPressed: (mouse) => {
                    startX = mouse.scenePosition !== undefined ? mouse.scenePosition.x : mouse.x;
                    startW = win.panelW;
                }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    var x = mouse.scenePosition !== undefined ? mouse.scenePosition.x : (mapToItem(null, mouse.x, mouse.y).x);
                    var nw = startW + (x - startX);
                    if (nw < 280) nw = 280;
                    if (nw > 540) nw = 540;
                    win.panelW = nw;
                }
                // визуальный акцент при наведении
                Rectangle {
                    anchors.right: parent.right
                    width: 1; height: parent.height
                    color: parent.containsMouse || parent.pressed
                           ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.6)
                           : "transparent"
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 16

                // ── Шапка: кнопка выхода (надёжный способ закрыть) ──
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "IAMVPN"
                        color: win.textLo
                        font.pixelSize: 11
                        font.letterSpacing: 1
                    }
                    Item { Layout.fillWidth: true }
                    // Свернуть в трей
                    Rectangle {
                        width: 26; height: 22; radius: 0
                        color: hideMouse.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.04)
                        Text { anchors.centerIn: parent; text: "—"; color: win.textLo; font.pixelSize: 13 }
                        MouseArea { id: hideMouse; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: win.hide() }
                    }
                    // Полный выход
                    Rectangle {
                        width: 26; height: 22; radius: 0
                        color: exitMouse.containsMouse ? Qt.rgba(0.9,0.3,0.2,0.25) : Qt.rgba(1,1,1,0.04)
                        Text { anchors.centerIn: parent; text: "×"; color: exitMouse.containsMouse ? "#E8643C" : win.textLo; font.pixelSize: 15 }
                        MouseArea { id: exitMouse; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { win.reallyQuit = true; Qt.quit(); } }
                    }
                }

                // ── Вкладки: подчёркнутые табы со скользящим индикатором ──
                Item {
                    id: tabBar
                    property int tab: 0   // 0=VPN 1=Сеть 2=Опции
                    property var labels: [qsTr("VPN"), qsTr("Сеть"), qsTr("Опции"), qsTr("Диагностика")]
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40

                    Row {
                        id: tabRow
                        anchors.fill: parent
                        Repeater {
                            model: tabBar.labels
                            Item {
                                width: tabBar.width / tabBar.labels.length
                                height: tabBar.height
                                property bool sel: tabBar.tab === index
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: parent.sel ? win.textHi : win.textLo
                                    font.pixelSize: 14
                                    font.weight: parent.sel ? Font.DemiBold : Font.Normal
                                    Behavior on color { ColorAnimation { duration: 150 } }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: tabBar.tab = index
                                }
                            }
                        }
                    }

                    // Тонкая линия-основание под всеми вкладками.
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 1
                        color: Qt.rgba(1,1,1,0.08)
                    }
                    // Скользящий зелёный индикатор активной вкладки.
                    Rectangle {
                        height: 2
                        width: tabBar.width / tabBar.labels.length
                        x: tabBar.tab * width
                        anchors.bottom: parent.bottom
                        color: win.accent
                        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    }
                }

                // ── СОДЕРЖИМОЕ ВКЛАДОК ──
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabBar.tab

                    // ═══ Вкладка VPN ═══
                    ColumnLayout {
                        spacing: 14

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: qsTr("Серверы")
                                color: win.textHi
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            // Перекачать подписку (серверы могли поменяться).
                            Rectangle {
                                visible: Vpn.subscriptionUrl !== ""
                                Layout.preferredWidth: 92; Layout.preferredHeight: 26
                                color: subRefMouse.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.16)
                                                                 : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                                border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.35); border.width: 1
                                Text { anchors.centerIn: parent; text: qsTr("Обновить")
                                       color: win.accent; font.pixelSize: 12; font.weight: Font.Medium }
                                MouseArea { id: subRefMouse; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Vpn.loadSubscription(Vpn.subscriptionUrl) }
                            }
                            // TCP-пинг всех серверов (как в Happ).
                            Rectangle {
                                visible: Vpn.servers.length > 0
                                Layout.preferredWidth: 74; Layout.preferredHeight: 26
                                color: pingAllMouse.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.16)
                                                                  : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                                border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.35); border.width: 1
                                Text { anchors.centerIn: parent
                                       text: Vpn.pinging ? qsTr("Пинг…") : qsTr("Пинг")
                                       color: win.accent; font.pixelSize: 12; font.weight: Font.Medium }
                                MouseArea { id: pingAllMouse; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor; enabled: !Vpn.pinging
                                    onClicked: Vpn.pingAll() }
                            }
                        }

                        // Поле ввода ключа/подписки.
                        Rectangle {
                            Layout.fillWidth: true
                            height: 80
                            radius: 0
                            color: Qt.rgba(1,1,1,0.04)
                            border.color: Qt.rgba(1,1,1,0.08)
                            border.width: 1
                            TextArea {
                                id: keyInput
                                anchors.fill: parent
                                anchors.margins: 10
                                wrapMode: TextArea.WrapAnywhere
                                color: win.textHi
                                font.pixelSize: 12
                                placeholderText: qsTr("Вставьте ключ (vless, vmess, trojan, ss, hy2, tuic) или ссылку-подписку")
                                placeholderTextColor: Qt.rgba(0.5,0.56,0.45,0.6)
                                background: null
                                selectByMouse: true
                            }
                        }

                        // Кнопка "Добавить".
                        Rectangle {
                            Layout.fillWidth: true
                            height: 38
                            radius: 0
                            color: addMouse.containsMouse
                                   ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.16)
                                   : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.10)
                            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.35)
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Добавить")
                                color: win.accent
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }
                            MouseArea {
                                id: addMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var t = keyInput.text.trim();
                                    if (t.indexOf("happ://") === 0 || t.indexOf("http") === 0)
                                        Vpn.loadSubscription(t);
                                    else if (t.length > 0) {
                                        // Несколько ключей построчно — добавляем каждый.
                                        var lines = t.split(/\s+/);
                                        for (var i = 0; i < lines.length; ++i)
                                            if (lines[i].indexOf("://") > 0) Vpn.addKey(lines[i]);
                                    }
                                    keyInput.text = "";
                                }
                            }
                        }

                        // «Авто»: sing-box сам держит самый быстрый живой сервер.
                        Rectangle {
                            visible: Vpn.servers.length > 1
                            Layout.fillWidth: true
                            height: 48
                            color: Vpn.autoSelect ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                                                  : (autoHover.hovered ? Qt.rgba(1,1,1,0.04) : "transparent")
                            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b, Vpn.autoSelect ? 0.35 : 0.12)
                            border.width: 1
                            HoverHandler { id: autoHover }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 3; height: Vpn.autoSelect ? parent.height - 14 : 0
                                color: win.accent
                                Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 10
                                spacing: 1
                                Text {
                                    text: "⚡ " + qsTr("Авто — лучший сервер")
                                    color: Vpn.autoSelect ? win.accent : win.textHi
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: Vpn.autoSelect && Vpn.connected && Vpn.activeServerName !== ""
                                          ? qsTr("сейчас: ") + Vpn.activeServerName
                                          : qsTr("сам переключится, если сервер умрёт")
                                    color: win.textLo
                                    font.pixelSize: 10
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Vpn.autoSelect = true
                            }
                        }

                        // Список серверов.
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 8
                            model: Vpn.servers
                            boundsBehavior: Flickable.StopAtBounds

                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 54
                                property bool sel: !Vpn.autoSelect && index === Vpn.currentServer
                                opacity: modelData.unsupported !== "" ? 0.5 : 1.0
                                // Лёгкая подсветка строки выбранного/наведённого.
                                color: sel ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                                           : (rowHover.hovered ? Qt.rgba(1,1,1,0.04) : "transparent")
                                Behavior on color { ColorAnimation { duration: 120 } }
                                HoverHandler { id: rowHover }

                                // Вертикальная зелёная полоска-акцент слева у выбранного.
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 3; height: sel ? parent.height - 14 : 0
                                    color: win.accent
                                    Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                }
                                // Нижний разделитель.
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width; height: 1
                                    color: Qt.rgba(1,1,1,0.05)
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 8
                                    spacing: 6
                                    Rectangle {
                                        width: 7; height: 7; radius: 4
                                        color: parent.parent.sel ? win.accent : Qt.rgba(1,1,1,0.2)
                                    }
                                    ColumnLayout {
                                        spacing: 1
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: modelData.name
                                            color: win.textHi
                                            font.pixelSize: 13
                                            font.weight: Font.Medium
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: modelData.unsupported !== ""
                                                  ? qsTr("не поддерживается: ") + modelData.unsupported
                                                  : modelData.proto + " · " + modelData.host
                                            color: modelData.unsupported !== "" ? "#E8643C" : win.textLo
                                            font.pixelSize: 10
                                        }
                                    }
                                    // Пинг сервера.
                                    Text {
                                        readonly property var ms: Vpn.pings[modelData.host + ":" + modelData.port]
                                        visible: ms !== undefined && ms !== -1
                                        Layout.alignment: Qt.AlignVCenter
                                        text: ms === -3 ? "…" : (ms === -2 ? "—" : (ms === -4 ? "udp" : ms + " мс"))
                                        color: ms === -3 || ms === -4 ? win.textLo
                                             : ms === -2 ? "#E8643C"
                                             : ms < 150 ? win.accent
                                             : ms < 400 ? "#E8C33C" : "#E8643C"
                                        font.pixelSize: 11
                                    }
                                    // Поделиться (копировать + QR).
                                    Rectangle {
                                        Layout.alignment: Qt.AlignVCenter
                                        Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                        width: 22; height: 22; radius: 0
                                        color: shMouse.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.2) : Qt.rgba(1,1,1,0.05)
                                        Text { anchors.centerIn: parent; text: "⤴"; color: shMouse.containsMouse ? win.accent : win.textLo; font.pixelSize: 13 }
                                        MouseArea {
                                            id: shMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: { shareDialog.uri = modelData.uri; shareDialog.idx = index; shareDialog.open(); }
                                        }
                                    }
                                    // Изменить (✎).
                                    Rectangle {
                                        Layout.alignment: Qt.AlignVCenter
                                        Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                        width: 22; height: 22; radius: 0
                                        color: edMouse.containsMouse ? Qt.rgba(1,1,1,0.12) : Qt.rgba(1,1,1,0.05)
                                        Text { anchors.centerIn: parent; text: "✎"; color: edMouse.containsMouse ? win.textHi : win.textLo; font.pixelSize: 12 }
                                        MouseArea {
                                            id: edMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: { editDialog.idx = index; editField.text = modelData.uri; editDialog.open(); }
                                        }
                                    }
                                    // Кнопка удаления (×).
                                    Rectangle {
                                        Layout.alignment: Qt.AlignVCenter
                                        Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                        width: 22; height: 22; radius: 0
                                        color: delMouse.containsMouse
                                               ? Qt.rgba(0.9,0.3,0.2,0.25) : Qt.rgba(1,1,1,0.05)
                                        Text {
                                            anchors.centerIn: parent
                                            text: "×"; color: delMouse.containsMouse ? "#E8643C" : win.textLo; font.pixelSize: 14
                                        }
                                        MouseArea {
                                            id: delMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: Vpn.removeServer(index)
                                        }
                                    }
                                }
                                // Выбор сервера — клик по карточке (но не по кнопкам справа).
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.rightMargin: 80
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Vpn.currentServer = index
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                width: parent.width - 20
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                visible: Vpn.servers.length === 0
                                text: qsTr("Вставьте ключ или ссылку-подписку,\nчтобы добавить серверы")
                                color: Qt.rgba(0.5,0.56,0.45,0.6)
                                font.pixelSize: 12
                            }
                        }
                    }

                    // ═══ Вкладка Устройства ═══
                    ColumnLayout {
                        spacing: 14

                        ColumnLayout {
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: qsTr("Устройства")
                                    color: win.textHi
                                    font.pixelSize: 18
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                                // Счётчик найденных устройств.
                                Rectangle {
                                    visible: Scanner.devices.length > 0
                                    radius: 0; height: 22
                                    Layout.preferredWidth: cntTxt.implicitWidth + 18
                                    color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.14)
                                    Text {
                                        id: cntTxt
                                        anchors.centerIn: parent
                                        text: Scanner.devices.length
                                        color: win.accent; font.pixelSize: 11; font.weight: Font.Medium
                                    }
                                }
                            }
                            Text {
                                text: Scanner.subnet.length > 0 ? Scanner.subnet
                                                                : qsTr("сеть не определена")
                                color: win.textLo
                                font.pixelSize: 11
                            }
                        }

                        // Живой трафик VPN (когда подключён).
                        Rectangle {
                            Layout.fillWidth: true
                            visible: win.isOn
                            radius: 0
                            color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.25)
                            border.width: 1
                            Layout.preferredHeight: 52
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14; anchors.rightMargin: 14
                                Rectangle { width: 8; height: 8; radius: 4; color: win.accent }
                                ColumnLayout {
                                    spacing: 1
                                    Layout.leftMargin: 6
                                    Text { text: qsTr("VPN активен"); color: win.textHi; font.pixelSize: 12; font.weight: Font.Medium }
                                    Text {
                                        text: "↑ " + Stats.upMB.toFixed(1) + " МБ   ↓ " + Stats.downMB.toFixed(1) + " МБ"
                                        color: win.textLo; font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 38
                            radius: 0
                            color: scanMouse.containsMouse
                                   ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.16)
                                   : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.10)
                            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.35)
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: Scanner.scanning ? qsTr("Сканирование…")
                                                       : qsTr("Сканировать сеть")
                                color: win.accent
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }
                            MouseArea {
                                id: scanMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                enabled: !Scanner.scanning
                                onClicked: Scanner.scan()
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 8
                            model: Scanner.devices
                            boundsBehavior: Flickable.StopAtBounds

                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 56
                                color: modelData.isSelf ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.06)
                                                         : "transparent"
                                // Полоска-акцент у "этого ПК".
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 3; height: modelData.isSelf ? parent.height - 14 : 0
                                    color: win.accent
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width; height: 1
                                    color: Qt.rgba(1,1,1,0.05)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 12
                                    spacing: 10
                                    Rectangle { width: 8; height: 8; radius: 4; color: win.accent }
                                    ColumnLayout {
                                        spacing: 2
                                        Layout.fillWidth: true
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: {
                                                if (modelData.isSelf) return qsTr("Этот компьютер");
                                                var n = modelData.name ? modelData.name : "";
                                                return n.length > 0 ? n : modelData.ip;
                                            }
                                            color: win.textHi
                                            font.pixelSize: 13
                                            font.weight: Font.Medium
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: {
                                                var hasName = !modelData.isSelf && modelData.name
                                                              && modelData.name.length > 0;
                                                return hasName ? modelData.ip + "  ·  " + modelData.mac
                                                               : modelData.mac;
                                            }
                                            color: win.textLo
                                            font.pixelSize: 11
                                        }
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                width: parent.width - 20
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                visible: Scanner.devices.length === 0 && !Scanner.scanning
                                text: qsTr("Нажмите «Сканировать сеть»")
                                color: Qt.rgba(0.5,0.56,0.45,0.6)
                                font.pixelSize: 12
                            }
                        }
                    }

                    // ═══ Вкладка Опции (настройки) — минимал, без карточек ═══
                    ColumnLayout {
                        spacing: 0

                        Text {
                            text: qsTr("Настройки")
                            color: win.textHi
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                            Layout.bottomMargin: 14
                        }

                        // — Строка-тумблер: .ru напрямую —
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            spacing: 10
                            ColumnLayout {
                                spacing: 2; Layout.fillWidth: true; Layout.minimumWidth: 0
                                Text { Layout.fillWidth: true; elide: Text.ElideRight
                                       text: qsTr("Российский трафик напрямую"); color: win.textHi; font.pixelSize: 13 }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight
                                       text: qsTr(".ru-сайты идут мимо VPN"); color: win.textLo; font.pixelSize: 11 }
                            }
                            Rectangle {
                                width: 42; height: 23; radius: 12
                                color: Vpn.routeRuDirect ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.85) : Qt.rgba(1,1,1,0.12)
                                Behavior on color { ColorAnimation { duration: 160 } }
                                Rectangle { width:17; height:17; radius:9; color:"white"; anchors.verticalCenter: parent.verticalCenter
                                    x: Vpn.routeRuDirect ? parent.width-width-3 : 3
                                    Behavior on x { NumberAnimation { duration:160; easing.type: Easing.OutCubic } } }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Vpn.routeRuDirect = !Vpn.routeRuDirect }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1,1,1,0.06) }

                        // — Строка-тумблер: обход DPI (zapret есть только под Windows) —
                        RowLayout {
                            visible: Qt.platform.os === "windows"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            spacing: 10
                            ColumnLayout {
                                spacing: 2; Layout.fillWidth: true; Layout.minimumWidth: 0
                                Text { Layout.fillWidth: true; elide: Text.ElideRight
                                       text: qsTr("Обход DPI (zapret)"); color: win.textHi; font.pixelSize: 13 }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: qsTr("Фрагментация. Не нужен для Cloudflare"); color: win.textLo; font.pixelSize: 11 }
                            }
                            Rectangle {
                                width: 42; height: 23; radius: 12
                                color: Vpn.useZapret ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.85) : Qt.rgba(1,1,1,0.12)
                                Behavior on color { ColorAnimation { duration: 160 } }
                                Rectangle { width:17; height:17; radius:9; color:"white"; anchors.verticalCenter: parent.verticalCenter
                                    x: Vpn.useZapret ? parent.width-width-3 : 3
                                    Behavior on x { NumberAnimation { duration:160; easing.type: Easing.OutCubic } } }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Vpn.useZapret = !Vpn.useZapret }
                            }
                        }

                        // — Строка: выбор профиля zapret (для разных провайдеров) —
                        RowLayout {
                            visible: Qt.platform.os === "windows"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            Layout.topMargin: 4
                            spacing: 10
                            enabled: Vpn.useZapret
                            opacity: Vpn.useZapret ? 1.0 : 0.4
                            ColumnLayout {
                                spacing: 1; Layout.fillWidth: true; Layout.minimumWidth: 0
                                Text { text: qsTr("Стратегия"); color: win.textHi; font.pixelSize: 12 }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight
                                       text: qsTr("Если не работает — смени"); color: win.textLo; font.pixelSize: 10 }
                            }
                            // Кнопка-селектор
                            Rectangle {
                                Layout.preferredWidth: 120
                                height: 30
                                color: profSelMouse.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05)
                                border.color: Qt.rgba(1,1,1,0.12); border.width: 1
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10; anchors.rightMargin: 8
                                    Text {
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                        text: Zapret.selectedProfile === "" ? qsTr("Авто") : Zapret.selectedProfile
                                        color: win.textHi; font.pixelSize: 12
                                    }
                                    Text { text: "▾"; color: win.textLo; font.pixelSize: 11 }
                                }
                                MouseArea { id: profSelMouse; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor; onClicked: profilePopup.open() }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1,1,1,0.06) }

                        // — Секция Telegram —
                        Text { text: qsTr("Telegram-прокси"); color: win.textHi; font.pixelSize: 13; font.weight: Font.Medium
                               Layout.topMargin: 18; Layout.bottomMargin: 6 }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap
                               text: qsTr("Через Cloudflare — работает без VPN"); color: win.textLo; font.pixelSize: 11
                               Layout.bottomMargin: 8 }
                        Rectangle {
                            Layout.fillWidth: true; height: 30; color: "transparent"
                            TextField {
                                id: workerField
                                anchors.fill: parent
                                color: win.textLo; font.pixelSize: 11
                                text: Tg.workerDomain
                                placeholderText: qsTr("CF Worker домен")
                                placeholderTextColor: Qt.rgba(0.5,0.56,0.45,0.6)
                                background: Rectangle { color: "transparent"
                                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.12) } }
                                onEditingFinished: Tg.workerDomain = text
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.topMargin: 10
                            height: 40; radius: 0
                            color: tgToggleMouse.containsMouse
                                   ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.14)
                                   : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.3); border.width: 1
                            Text { anchors.centerIn: parent
                                text: Tg.running ? qsTr("Выключить TG-прокси") : qsTr("Включить TG-прокси")
                                color: win.accent; font.pixelSize: 13; font.weight: Font.Medium }
                            MouseArea { id: tgToggleMouse; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor; onClicked: Tg.toggle() }
                        }
                        Text { Layout.fillWidth: true; Layout.topMargin: 6; wrapMode: Text.WordWrap
                               text: Tg.statusText; color: Tg.running ? win.accent : win.textLo; font.pixelSize: 11 }
                        RowLayout {
                            Layout.fillWidth: true; Layout.topMargin: 8; spacing: 8
                            visible: Tg.tgLink.length > 0
                            Rectangle {
                                Layout.fillWidth: true; height: 36; radius: 0
                                color: tgOpenMouse.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.14) : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                                border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.3); border.width: 1
                                Text { anchors.centerIn: parent; text: qsTr("Открыть в Telegram"); color: win.accent; font.pixelSize: 12 }
                                TextEdit { id: tgLinkField; visible: false; text: Tg.tgLink }
                                MouseArea { id: tgOpenMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally(Tg.tgLink) }
                            }
                            Rectangle {
                                Layout.preferredWidth: 100; height: 36; radius: 0
                                color: copyMouse.containsMouse ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.04)
                                border.color: Qt.rgba(1,1,1,0.12); border.width: 1
                                Text { id: copyLabel; anchors.centerIn: parent; text: qsTr("Копировать"); color: win.textHi; font.pixelSize: 12 }
                                MouseArea { id: copyMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: { tgLinkField.selectAll(); tgLinkField.copy(); tgLinkField.deselect(); copyLabel.text = qsTr("Скопировано"); copyResetTimer.restart(); } }
                                Timer { id: copyResetTimer; interval: 1500; onTriggered: copyLabel.text = qsTr("Копировать") }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.topMargin: 16; height: 1; color: Qt.rgba(1,1,1,0.06) }

                        // — Секция Сайты-исключения —
                        Text { text: qsTr("Сайты-исключения"); color: win.textHi; font.pixelSize: 13; font.weight: Font.Medium
                               Layout.topMargin: 16; Layout.bottomMargin: 6 }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap
                               text: qsTr("Идут напрямую — мимо VPN и без обхода DPI"); color: win.textLo; font.pixelSize: 11
                               Layout.bottomMargin: 8 }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Rectangle {
                                Layout.fillWidth: true; height: 30; color: "transparent"
                                TextField {
                                    id: bypassInput; anchors.fill: parent
                                    color: win.textHi; font.pixelSize: 12
                                    placeholderText: qsTr("например: gosuslugi.ru")
                                    placeholderTextColor: Qt.rgba(0.5,0.56,0.45,0.6)
                                    background: Rectangle { color: "transparent"
                                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.12) } }
                                    onAccepted: { if (text.trim().length) { Vpn.addBypassSite(text.trim()); text=""; } }
                                }
                            }
                            Rectangle {
                                width: 32; height: 30; radius: 0
                                color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.10)
                                border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.3); border.width: 1
                                Text { anchors.centerIn: parent; text: "+"; color: win.accent; font.pixelSize: 16 }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: { if (bypassInput.text.trim().length) { Vpn.addBypassSite(bypassInput.text.trim()); bypassInput.text=""; } } }
                            }
                        }
                        // Fixed-height list: past 5 rows it scrolls instead of pushing
                        // the sections below (updates button) off the window.
                        ListView {
                            id: bypassList
                            readonly property int rowH: 24
                            readonly property int maxRows: 5
                            Layout.fillWidth: true
                            Layout.topMargin: count > 0 ? 6 : 0
                            Layout.preferredHeight: Math.min(count, maxRows) * rowH
                            clip: true
                            model: Vpn.bypassSites
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {
                                policy: bypassList.count > bypassList.maxRows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                            }
                            delegate: RowLayout {
                                width: bypassList.width - (bypassList.count > bypassList.maxRows ? 10 : 0)
                                height: bypassList.rowH; spacing: 6
                                Text { Layout.fillWidth: true; text: "•  " + modelData; color: win.textLo; font.pixelSize: 12; elide: Text.ElideRight }
                                Rectangle { width: 18; height: 18; radius: 0
                                    color: bdel.containsMouse ? Qt.rgba(0.9,0.3,0.2,0.25) : "transparent"
                                    Text { anchors.centerIn: parent; text: "×"; color: win.textLo; font.pixelSize: 12 }
                                    MouseArea { id: bdel; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Vpn.removeBypassSite(index) } }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.topMargin: 16; height: 1; color: Qt.rgba(1,1,1,0.06) }

                        // — Обновления —
                        Text { text: qsTr("Обновления"); color: win.textHi; font.pixelSize: 13; font.weight: Font.Medium
                               Layout.topMargin: 16; Layout.bottomMargin: 6 }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap
                               text: qsTr("IAMVPN · версия ") + Updater.currentVersion + " · " + Updater.statusText
                               color: win.textLo; font.pixelSize: 11; Layout.bottomMargin: 8 }
                        Rectangle {
                            Layout.fillWidth: true; height: 38
                            color: chkUpdM.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.14)
                                                          : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.3); border.width: 1
                            Text { anchors.centerIn: parent
                                   text: Updater.checking ? qsTr("Проверка…")
                                       : (Updater.updateAvailable ? qsTr("Скачать новую версию") : qsTr("Проверить обновления"))
                                   color: win.accent; font.pixelSize: 12; font.weight: Font.Medium }
                            MouseArea { id: chkUpdM; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor; enabled: !Updater.checking
                                onClicked: {
                                    if (Updater.updateAvailable)
                                        Updater.downloadAndInstall();   // прямо в приложении
                                    else
                                        Updater.check();
                                } }
                        }

                        Item { Layout.fillHeight: true }
                    }

                    // ═══ Вкладка Диагностика ═══
                    ColumnLayout {
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true; Layout.bottomMargin: 14
                            Text { Layout.fillWidth: true
                                   text: qsTr("Диагностика"); color: win.textHi
                                   font.pixelSize: 18; font.weight: Font.DemiBold }
                            Rectangle {
                                Layout.preferredWidth: 110; Layout.preferredHeight: 30
                                color: runDiagM.containsMouse
                                       ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.16)
                                       : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08)
                                border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.35); border.width: 1
                                Text { anchors.centerIn: parent
                                       text: Diag.running ? qsTr("Идёт…") : qsTr("Запустить")
                                       color: win.accent; font.pixelSize: 12; font.weight: Font.Medium }
                                MouseArea { id: runDiagM; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor; enabled: !Diag.running
                                    onClicked: Diag.runAll() }
                            }
                        }

                        // — список проверок —
                        Repeater {
                            model: Diag.results
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 44
                                spacing: 10
                                // цветовой индикатор
                                Rectangle {
                                    Layout.preferredWidth: 10; Layout.preferredHeight: 10
                                    radius: 5
                                    color: modelData.status === "ok" ? win.accent
                                         : modelData.status === "warn" ? "#E8B33C"
                                         : "#E8643C"
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 1
                                    Text { Layout.fillWidth: true; elide: Text.ElideRight
                                           text: modelData.title; color: win.textHi; font.pixelSize: 13 }
                                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap
                                           text: modelData.detail; color: win.textLo; font.pixelSize: 11 }
                                }
                            }
                        }

                        Text { Layout.fillWidth: true; visible: Diag.results.length === 0
                               text: qsTr("Нажмите «Запустить» — приложение проверит, что мешает подключению.")
                               color: win.textLo; font.pixelSize: 12; wrapMode: Text.WordWrap }

                        // — журнал действий приложения —
                        Rectangle { Layout.fillWidth: true; Layout.topMargin: 14; height: 1; color: Qt.rgba(1,1,1,0.08) }
                        Text { Layout.fillWidth: true; Layout.topMargin: 8
                               text: qsTr("Журнал действий")
                               color: win.textHi; font.pixelSize: 13; font.weight: Font.Medium }
                        // Ссылки-действия — отдельной строкой, чтобы при узкой
                        // панели не наезжали на заголовок.
                        RowLayout {
                            Layout.fillWidth: true; Layout.topMargin: 2; spacing: 14
                            Text { text: qsTr("Экспорт"); color: win.accent; font.pixelSize: 11
                                   MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                       onClicked: exportLogDialog.open() } }
                            Text { text: qsTr("Папка"); color: win.accent; font.pixelSize: 11
                                   MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                       onClicked: Logs.openLogsFolder() } }
                            Text { text: qsTr("Копировать"); color: win.accent; font.pixelSize: 11
                                   MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                       onClicked: Diag.copyToClipboard() } }
                            Item { Layout.fillWidth: true }
                        }
                        Text { Layout.fillWidth: true; Layout.topMargin: 2
                               text: qsTr("Пишется автоматически на каждое действие → ") + Logs.currentLogPath
                               color: win.textLo; font.pixelSize: 10; wrapMode: Text.Wrap; elide: Text.ElideMiddle }
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.topMargin: 6
                            Layout.minimumHeight: 160
                            color: Qt.rgba(0,0,0,0.35); border.color: Qt.rgba(1,1,1,0.08); border.width: 1
                            Flickable {
                                id: logFlick
                                anchors.fill: parent; anchors.margins: 8
                                contentWidth: width; contentHeight: logText.implicitHeight
                                clip: true
                                // авто-скролл вниз при появлении новых строк
                                onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
                                TextEdit {
                                    id: logText; width: parent.width
                                    text: Logs.tail.length > 0 ? Logs.tail
                                                               : qsTr("Логи появятся после первого действия.")
                                    color: Logs.tail.length > 0 ? win.textLo : Qt.rgba(0.5,0.56,0.45,0.6)
                                    font.family: "Consolas"; font.pixelSize: 10
                                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                                }
                            }
                        }
                    }

                    // ── Диалог сохранения лога ──
                    Platform.FileDialog {
                        id: exportLogDialog
                        title: qsTr("Сохранить лог")
                        fileMode: Platform.FileDialog.SaveFile
                        nameFilters: [ qsTr("Текстовый файл (*.txt)") ]
                        defaultSuffix: "txt"
                        currentFile: "file:///" + (Qt.platform.os === "windows"
                            ? (Qt.application.arguments.length > 0 ? "" : "") + "IAMVPN-log.txt"
                            : "IAMVPN-log.txt")
                        onAccepted: Logs.exportTo(file.toString())
                    }
                    Connections {
                        target: Logs
                        function onExported(ok, path) {
                            // тихо: статус в журнале уже записан. Можно добавить toast в будущем.
                        }
                    }

                }
            }
        }

        // ═══════════ ПРАВАЯ ЧАСТЬ ═══════════
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ── Баннер «Доступно обновление» ──
            Rectangle {
                id: updateBanner
                property bool dismissed: false
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: visible ? 40 : 0
                visible: Updater.updateAvailable && !dismissed
                z: 5
                color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.14)
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.4) }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18; anchors.rightMargin: 12
                    spacing: 12
                    Text { Layout.fillWidth: true; elide: Text.ElideRight
                           text: qsTr("Доступна новая версия ") + Updater.latestVersion
                           color: win.accent; font.pixelSize: 13; font.weight: Font.Medium }
                    Rectangle {
                        Layout.preferredWidth: 110; height: 28
                        color: instM.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.30) : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.18)
                        border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.6); border.width: 1
                        Text { anchors.centerIn: parent; text: qsTr("Установить"); color: win.accent; font.pixelSize: 12 }
                        MouseArea { id: instM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: Updater.downloadAndInstall() }
                    }
                    Text { text: "×"; color: win.textLo; font.pixelSize: 16
                           MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                               onClicked: updateBanner.dismissed = true } }
                }
            }

            Image {
                id: logo
                source: "qrc:/qt/qml/IAMVPN/assets/IamvpnLogoLight.png"
                anchors.top: parent.top
                anchors.topMargin: 40
                anchors.horizontalCenter: parent.horizontalCenter
                fillMode: Image.PreserveAspectFit
                width: 210
                smooth: true
                opacity: 0.96
            }

            GlowButton {
                id: powerBtn
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -10
                width: 168; height: 168
                active: win.isOn
                searching: win.isBusy
                accent: win.accent
                onClicked: {
                    // Главная кнопка: VPN + обход (zapret только если включён).
                    if (win.isOn || win.isBusy) {
                        Vpn.disconnectVpn();
                        Zapret.stop();
                    } else {
                        if (Vpn.useZapret) Zapret.start();   // обход DPI — опционально
                        Vpn.connectVpn();                     // туннель
                    }
                }
            }

            ColumnLayout {
                anchors.top: powerBtn.bottom
                anchors.topMargin: 40
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: {
                        if (win.isBusy) return qsTr("Подключение…");
                        return win.isOn ? qsTr("Подключено") : qsTr("Отключено");
                    }
                    color: win.isOn ? win.textHi : win.textLo
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    Behavior on color { ColorAnimation { duration: 400 } }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 440
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                    text: {
                        if (win.isBusy) return Vpn.statusText;
                        if (win.isOn) return Vpn.statusText + qsTr("  ·  .ru напрямую");
                        return qsTr("Нажмите кнопку, чтобы подключиться");
                    }
                    color: win.textLo
                    font.pixelSize: 14
                }
            }
        }
    }

    // ═══════════ ДИАЛОГ «ПОДЕЛИТЬСЯ» (копировать + QR) ═══════════
    Popup {
        id: shareDialog
        property string uri: ""
        property int idx: -1
        property string qrPath: ""
        anchors.centerIn: Overlay.overlay
        width: 320; height: 420
        modal: true
        padding: 0
        onOpened: qrPath = Vpn.qrForServer(idx)
        background: Rectangle {
            color: "#0E120C"
            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.3); border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: 14
            Text { Layout.fillWidth: true; Layout.topMargin: 16; horizontalAlignment: Text.AlignHCenter
                   text: qsTr("Поделиться ключом"); color: win.textHi; font.pixelSize: 16; font.weight: Font.DemiBold }
            // QR
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 220; height: 220; color: "white"
                Image {
                    anchors.fill: parent; anchors.margins: 8
                    source: shareDialog.qrPath
                    fillMode: Image.PreserveAspectFit
                }
                Text { anchors.centerIn: parent; visible: shareDialog.qrPath === ""
                       text: qsTr("QR недоступен"); color: "#888"; font.pixelSize: 12 }
            }
            // Копировать
            Rectangle {
                Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 220
                height: 38; color: shareCopyM.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.16) : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.10)
                border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.35); border.width: 1
                Text { id: shareCopyLabel; anchors.centerIn: parent; text: qsTr("Копировать ключ"); color: win.accent; font.pixelSize: 13 }
                TextEdit { id: shareCopyField; visible: false; text: shareDialog.uri }
                MouseArea { id: shareCopyM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { shareCopyField.selectAll(); shareCopyField.copy(); shareCopyField.deselect(); shareCopyLabel.text = qsTr("Скопировано"); shareResetT.restart(); } }
                Timer { id: shareResetT; interval: 1500; onTriggered: shareCopyLabel.text = qsTr("Копировать ключ") }
            }
            // Закрыть
            Text { Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 12
                   text: qsTr("Закрыть"); color: win.textLo; font.pixelSize: 12
                   MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: shareDialog.close() } }
        }
    }

    // ═══════════ ДИАЛОГ «ИЗМЕНИТЬ КЛЮЧ» ═══════════
    Popup {
        id: editDialog
        property int idx: -1
        anchors.centerIn: Overlay.overlay
        width: 420; height: 220
        modal: true; padding: 0
        background: Rectangle {
            color: "#0E120C"
            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.3); border.width: 1
        }
        contentItem: ColumnLayout {
            anchors.margins: 18; spacing: 12
            Text { text: qsTr("Изменить ключ"); color: win.textHi; font.pixelSize: 16; font.weight: Font.DemiBold }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: Qt.rgba(1,1,1,0.04); border.color: Qt.rgba(1,1,1,0.1); border.width: 1
                TextArea { id: editField; anchors.fill: parent; anchors.margins: 8
                    wrapMode: TextArea.WrapAnywhere; color: win.textHi; font.pixelSize: 11
                    selectByMouse: true; background: null }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Rectangle { Layout.fillWidth: true; height: 36
                    color: saveM.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.16) : Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.10)
                    border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.35); border.width: 1
                    Text { anchors.centerIn: parent; text: qsTr("Сохранить"); color: win.accent; font.pixelSize: 13 }
                    MouseArea { id: saveM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { Vpn.editServer(editDialog.idx, editField.text.trim()); editDialog.close(); } } }
                Rectangle { Layout.preferredWidth: 100; height: 36
                    color: cancelM.containsMouse ? Qt.rgba(1,1,1,0.1) : Qt.rgba(1,1,1,0.04)
                    border.color: Qt.rgba(1,1,1,0.12); border.width: 1
                    Text { anchors.centerIn: parent; text: qsTr("Отмена"); color: win.textHi; font.pixelSize: 13 }
                    MouseArea { id: cancelM; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: editDialog.close() } }
            }
        }
    }

    // ═══════════ ПОПОВЕР ВЫБОРА ПРОФИЛЯ ZAPRET ═══════════
    Popup {
        id: profilePopup
        anchors.centerIn: Overlay.overlay
        width: 280; height: Math.min(420, 60 + (Zapret.profileNames.length + 1) * 34)
        modal: true; padding: 0
        background: Rectangle {
            color: "#0E120C"
            border.color: Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.3); border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: 0
            Text { Layout.fillWidth: true; Layout.topMargin: 12; Layout.bottomMargin: 8
                   horizontalAlignment: Text.AlignHCenter
                   text: qsTr("Стратегия обхода DPI"); color: win.textHi; font.pixelSize: 14; font.weight: Font.DemiBold }
            Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1,1,1,0.08) }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 0
                model: [""].concat(Zapret.profileNames)   // первый — "Авто"
                delegate: Rectangle {
                    width: ListView.view.width; height: 34
                    property bool sel: (modelData === Zapret.selectedProfile)
                    color: itemMouse.containsMouse ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.14)
                                                   : (sel ? Qt.rgba(win.accent.r,win.accent.g,win.accent.b,0.08) : "transparent")
                    Rectangle { anchors.left: parent.left; width: 3; height: parent.height
                                color: sel ? win.accent : "transparent" }
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData === "" ? qsTr("Авто (перебор)") : modelData
                        color: sel ? win.accent : win.textHi; font.pixelSize: 13
                    }
                    MouseArea { id: itemMouse; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { Zapret.selectedProfile = modelData; profilePopup.close() } }
                }
            }
        }
    }
}
