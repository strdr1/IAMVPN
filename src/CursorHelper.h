#pragma once
//
//  CursorHelper — Q_OBJECT-обёртка над глобальным состоянием курсора/мыши.
//  Нужен потому что на Windows + elevated Qt.labs.platform.SystemTrayIcon
//  присылает ВСЕ клики как reason=Trigger (UIPI режет WM_RBUTTONUP к
//  elevated-окну). Чтобы понять "это был ПКМ или ЛКМ?", спрашиваем у
//  системы напрямую через GetAsyncKeyState на момент срабатывания.
//
//  Также отдаёт глобальную позицию курсора (для popup() меню в нужной точке).
//

#include <QObject>
#include <QPoint>
#include <QCursor>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

class CursorHelper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QPoint pos READ pos)
    Q_PROPERTY(bool rightPressed READ rightPressed)
    Q_PROPERTY(bool leftPressed  READ leftPressed)

public:
    using QObject::QObject;

    QPoint pos() const { return QCursor::pos(); }

    bool rightPressed() const
    {
#ifdef Q_OS_WIN
        // GetAsyncKeyState возвращает high-bit=1 если клавиша/кнопка нажата
        // прямо сейчас. VK_RBUTTON корректно учитывает swap mouse buttons.
        return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
#else
        return false;
#endif
    }

    bool leftPressed() const
    {
#ifdef Q_OS_WIN
        return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#else
        return false;
#endif
    }
};
