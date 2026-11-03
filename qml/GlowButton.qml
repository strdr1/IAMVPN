// ─────────────────────────────────────────────────────────────────────────
//  GlowButton.qml — главная кнопка включения (Apple-минимализм).
//
//  Тонкое кольцо-трек, по которому при включении плавно "заливается" зелёная
//  дуга. В центре — мягкая иконка питания. Без кричащих неоновых обводок:
//  только аккуратное свечение и пружинная отдача при нажатии.
// ─────────────────────────────────────────────────────────────────────────
import QtQuick
import QtQuick.Effects

Item {
    id: root
    property bool active: false
    property bool searching: false    // идёт автоподбор стратегии
    property color accent: "#9FE812"
    signal clicked()

    implicitWidth: 168
    implicitHeight: 168

    // Прогресс заливки кольца: 0 — выключено, 1 — включено. Анимируется плавно.
    property real progress: active ? 1.0 : 0.0
    Behavior on progress {
        NumberAnimation { duration: 700; easing.type: Easing.InOutCubic }
    }

    // Угол вращения индикатора во время поиска (крутится по кругу).
    property real spin: 0
    RotationAnimation on spin {
        from: 0; to: 360; duration: 1100
        loops: Animation.Infinite
        running: root.searching
    }

    // Мягкое внешнее свечение (появляется только когда включено).
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.88; height: width; radius: width / 2
        color: root.accent
        opacity: root.progress * 0.16   // мягкое свечение только когда включено
        layer.enabled: true
        layer.effect: MultiEffect {
            blurEnabled: true
            blur: 1.0
            blurMax: 60
        }
    }

    // Кольцо: серый трек + зелёная дуга прогресса. Рисуем на Canvas.
    Canvas {
        id: ring
        anchors.fill: parent
        property real prog: root.progress
        property real spin: root.spin
        property bool searching: root.searching
        onProgChanged: requestPaint()
        onSpinChanged: requestPaint()
        onSearchingChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            var cx = width / 2, cy = height / 2;
            var r = width * 0.40;
            var lw = width * 0.045;
            ctx.lineCap = "round";

            // Трек (тонкий, приглушённый).
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.strokeStyle = "#1C2417";
            ctx.lineWidth = lw;
            ctx.stroke();

            if (searching) {
                // Режим поиска: вращающийся сегмент-индикатор.
                var s = (spin * Math.PI / 180) - Math.PI / 2;
                ctx.beginPath();
                ctx.arc(cx, cy, r, s, s + Math.PI * 0.5, false);
                ctx.strokeStyle = root.accent;
                ctx.lineWidth = lw;
                ctx.stroke();
            } else if (prog > 0.001) {
                // Обычный режим: дуга прогресса (сверху по часовой).
                var start = -Math.PI / 2;
                var end = start + Math.PI * 2 * prog;
                ctx.beginPath();
                ctx.arc(cx, cy, r, start, end, false);
                ctx.strokeStyle = root.accent;
                ctx.lineWidth = lw;
                ctx.stroke();
            }
        }
    }

    // Центральный диск.
    Rectangle {
        id: disc
        anchors.centerIn: parent
        width: parent.width * 0.62; height: width; radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.10,0.12,0.08,1) }
            GradientStop { position: 1.0; color: Qt.rgba(0.04,0.05,0.03,1) }
        }
        border.color: Qt.rgba(1,1,1, 0.06)
        border.width: 1

        scale: mouse.pressed ? 0.93 : 1.0
        Behavior on scale {
            NumberAnimation { duration: 260; easing.type: Easing.OutBack }
        }

        // Иконка питания (мягкая, меняет цвет по состоянию).
        Canvas {
            id: powerIcon
            anchors.centerIn: parent
            width: parent.width * 0.42; height: width
            property color col: Qt.tint("#7A8A66",
                Qt.rgba(root.accent.r, root.accent.g, root.accent.b, root.progress))
            onColChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.strokeStyle = col;
                ctx.lineWidth = Math.max(2.6, width * 0.10);
                ctx.lineCap = "round";
                var cx = width / 2, cy = height / 2;
                var r = width * 0.30;

                // Симметричный разрыв сверху: дуга идёт от правого-верха
                // по часовой вокруг до левого-верха, оставляя ровный зазор.
                var gap = Math.PI * 0.30;          // половина угла разрыва
                var top = -Math.PI / 2;             // верхняя точка (12 часов)
                ctx.beginPath();
                ctx.arc(cx, cy, r, top + gap, top - gap + Math.PI * 2, false);
                ctx.stroke();

                // Вертикальная черта строго по центральной оси.
                ctx.beginPath();
                ctx.moveTo(cx, cy - r * 1.18);
                ctx.lineTo(cx, cy - r * 0.10);
                ctx.stroke();
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
