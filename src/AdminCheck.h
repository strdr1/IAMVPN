#pragma once
//
//  AdminCheck — проверка, запущено ли приложение от имени администратора.
//  Нужно потому что:
//    • sing-box создаёт TUN-интерфейс — без админа падает с
//      "configure tun interface: Access is denied";
//    • winws (zapret) работает через WinDivert — драйверу нужен админ.
//  Возвращает true только если у текущего токена включена группа Administrators
//  (через CheckTokenMembership — самый надёжный способ на Windows).
//

#include <QString>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace AdminCheck {

inline bool isElevated()
{
#ifdef Q_OS_WIN
    BOOL isAdmin = FALSE;
    PSID adminSid = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&ntAuth, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &adminSid)) {
        return false;
    }
    if (!CheckTokenMembership(nullptr, adminSid, &isAdmin))
        isAdmin = FALSE;
    if (adminSid) FreeSid(adminSid);
    return isAdmin != FALSE;
#else
    return true;
#endif
}

}  // namespace AdminCheck
