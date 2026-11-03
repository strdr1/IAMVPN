// ─────────────────────────────────────────────────────────────────────────
//  GlassPanel.qml — стеклянная панель (glassmorphism).
//
//  Полупрозрачный фон с лёгким градиентом, тонкая светящаяся рамка, мягкая
//  внутренняя подсветка сверху (как блик на стекле). Контент кладётся внутрь.
// ─────────────────────────────────────────────────────────────────────────
import QtQuick
import QtQuick.Effects

Item {
    id: root
    property color accent: "#9FE812"
    property real radius: 18
    property bool glow: false           // подсветить рамку акцентом
    default property alias content: holder.data

    // Тело стекла: вертикальный градиент от чуть светлого к тёмному.
    Rectangle {
        id: glass
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1,1,1,0.07) }
            GradientStop { position: 0.5; color: Qt.rgba(1,1,1,0.035) }
            GradientStop { position: 1.0; color: Qt.rgba(1,1,1,0.02) }
        }
        // Светящаяся рамка.
        border.width: 1
        border.color: root.glow
            ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.45)
            : Qt.rgba(1,1,1,0.10)
        Behavior on border.color { ColorAnimation { duration: 300 } }

        // Верхний блик (имитация преломления света на стекле).
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            anchors.margins: 1
            height: parent.height * 0.5
            radius: root.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1,1,1,0.06) }
                GradientStop { position: 1.0; color: Qt.rgba(1,1,1,0.0) }
            }
        }
    }

    // Мягкое внешнее свечение, когда glow=true.
    Rectangle {
        anchors.fill: glass
        radius: root.radius
        color: "transparent"
        visible: root.glow
        border.width: 2
        border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.25)
        layer.enabled: true
        layer.effect: MultiEffect { blurEnabled: true; blur: 1.0; blurMax: 24 }
    }

    // Контейнер для содержимого.
    Item {
        id: holder
        anchors.fill: parent
    }
}
