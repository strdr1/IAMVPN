#pragma once
//
//  TrayMenuHelper — нативное Windows-меню (TrackPopupMenu) для иконки трея.
//
//  Зачем не QML Menu: на Windows 11 при elevated-процессе Qt.labs.platform
//  SystemTrayIcon ловит все клики как Trigger (UIPI режет WM_RBUTTONUP), а
//  QML popup() некорректно позиционируется в screen-координатах. WinAPI
//  TrackPopupMenu рисует НАСТОЯЩЕЕ системное меню Windows ровно у курсора
//  — то же что Steam/Discord/OBS показывают по ПКМ на своих трей-иконках.
//
//  Использование из QML:
//      TrayMenu.show(vpnOn)
//  Сигналы:
//      vpnToggle(), openWindow(), openLogs(), quit()
//
#include <QObject>
#include <QCursor>
#include <QDebug>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

class TrayMenuHelper : public QObject
{
    Q_OBJECT
public:
    explicit TrayMenuHelper(QObject *parent = nullptr) : QObject(parent)
    {
#ifdef Q_OS_WIN
        // Регистрируем скрытое сообщение-окно, которое будет owner'ом
        // меню. TrackPopupMenu без owner'а ничего не показывает; брать
        // GetActiveWindow() ненадёжно — когда главное окно свёрнуто в
        // трей, активного окна у нашего процесса нет.
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance   = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"IamvpnTrayMenuOwner";
        RegisterClassW(&wc);   // повторная регистрация безопасна — вернёт 0

        m_owner = CreateWindowExW(
            0, L"IamvpnTrayMenuOwner", L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE,   // message-only window (невидимое, без UI)
            nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
    }

    ~TrayMenuHelper() override
    {
#ifdef Q_OS_WIN
        if (m_owner) DestroyWindow(m_owner);
#endif
    }

    // VPN сейчас включён? Чтоб подписать первый пункт.
    Q_INVOKABLE void show(bool vpnOn)
    {
#ifdef Q_OS_WIN
        const QPoint p = QCursor::pos();
        qInfo() << "TrayMenu::show at" << p << "owner=" << m_owner << "vpnOn=" << vpnOn;

        HMENU m = CreatePopupMenu();
        if (!m) { qWarning() << "CreatePopupMenu failed"; return; }

        enum { ID_VPN = 100, ID_WIN = 101, ID_LOGS = 102, ID_QUIT = 103 };

        AppendMenuW(m, MF_STRING, ID_VPN,
                    vpnOn ? L"Отключить VPN" : L"Подключить VPN");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, ID_WIN,  L"Открыть окно");
        AppendMenuW(m, MF_STRING, ID_LOGS, L"Папка логов");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, ID_QUIT, L"Выход");

        // КРИТИЧНО для трей-меню: SetForegroundWindow перед TrackPopupMenu,
        // иначе меню не получит фокус и закроется сразу же. Это
        // официальный приём из docs.microsoft.com/.../menus.
        SetForegroundWindow(m_owner);

        const int cmd = TrackPopupMenu(
            m,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
            p.x(), p.y(),
            0, m_owner, nullptr);

        // PostMessage(WM_NULL) — стандартный workaround, чтобы меню
        // корректно дочистилось при следующем клике мимо.
        PostMessageW(m_owner, WM_NULL, 0, 0);

        DestroyMenu(m);

        qInfo() << "TrayMenu cmd=" << cmd << "lastError=" << GetLastError();

        switch (cmd) {
            case ID_VPN:  emit vpnToggle();  break;
            case ID_WIN:  emit openWindow(); break;
            case ID_LOGS: emit openLogs();   break;
            case ID_QUIT: emit quit();       break;
            default: break;
        }
#else
        Q_UNUSED(vpnOn);
#endif
    }

signals:
    void vpnToggle();
    void openWindow();
    void openLogs();
    void quit();

private:
#ifdef Q_OS_WIN
    HWND m_owner = nullptr;
#endif
};
