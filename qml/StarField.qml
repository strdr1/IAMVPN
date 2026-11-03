// ─────────────────────────────────────────────────────────────────────────
//  StarField.qml — деликатный фон в стиле Apple-минимализм.
//
//  Вместо ярких "звёзд" — мягкие частицы разной глубины, медленно плывущие
//  вверх. Дальние тусклее и мельче, ближние чуть ярче. Никакой пестроты —
//  только тонкий намёк на движение поверх градиентного фона.
// ─────────────────────────────────────────────────────────────────────────
import QtQuick

Item {
    id: root
    property color particleColor: "#9FE812"
    property int particleCount: 70

    property var parts: []

    Component.onCompleted: initParts()
    onWidthChanged: initParts()
    onHeightChanged: initParts()

    function initParts() {
        if (width <= 0 || height <= 0)
            return;
        var arr = [];
        for (var i = 0; i < particleCount; ++i) {
            var depth = Math.random();           // 0 — далеко, 1 — близко
            arr.push({
                x: Math.random() * width,
                y: Math.random() * height,
                size: 0.6 + depth * 1.8,          // ближние крупнее
                baseAlpha: 0.04 + depth * 0.16,    // ближние ярче, но всё мягко
                phase: Math.random() * Math.PI * 2,
                speed: 0.05 + depth * 0.25         // ближние двигаются быстрее
            });
        }
        parts = arr;
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Threaded

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            var t = animClock.value;

            for (var i = 0; i < root.parts.length; ++i) {
                var p = root.parts[i];

                // Очень мягкое мерцание (амплитуда мала — без "дискотеки").
                var a = p.baseAlpha * (0.7 + 0.3 * Math.sin(t * 1.2 + p.phase));

                p.y -= p.speed;
                if (p.y < -2) {
                    p.y = root.height + 2;
                    p.x = Math.random() * root.width;
                }

                ctx.beginPath();
                ctx.globalAlpha = a;
                ctx.fillStyle = root.particleColor;
                ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
                ctx.fill();
            }
            ctx.globalAlpha = 1.0;
        }
    }

    QtObject { id: animClock; property real value: 0 }
    NumberAnimation {
        target: animClock; property: "value"
        from: 0; to: 100000; duration: 100000 * 1000
        loops: Animation.Infinite; running: true
    }
    Connections {
        target: animClock
        function onValueChanged() { canvas.requestPaint(); }
    }
}
