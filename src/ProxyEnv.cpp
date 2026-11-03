#include "ProxyEnv.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include <QDebug>

namespace {

#ifdef Q_OS_WIN
// Пишем в HKCU\Environment (или HKLM\...\Environment для Machine scope).
// Value type = REG_EXPAND_SZ чтобы соответствовать поведению стандартной
// системной панели «Переменные среды».
bool writeReg(const QString &scope, const QString &name, const QString &value)
{
    HKEY root = (scope == QLatin1String("Machine"))
        ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const wchar_t *subKey = (scope == QLatin1String("Machine"))
        ? L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
        : L"Environment";

    HKEY hKey;
    LONG rc = RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE, &hKey);
    if (rc != ERROR_SUCCESS) {
        qWarning() << "ProxyEnv: RegOpenKeyExW failed:" << rc;
        return false;
    }

    std::wstring wv = value.toStdWString();
    rc = RegSetValueExW(
        hKey,
        reinterpret_cast<const wchar_t*>(name.utf16()),
        0, REG_EXPAND_SZ,
        reinterpret_cast<const BYTE*>(wv.c_str()),
        static_cast<DWORD>((wv.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        qWarning() << "ProxyEnv: RegSetValueExW" << name << "failed:" << rc;
        return false;
    }
    return true;
}

bool deleteReg(const QString &scope, const QString &name)
{
    HKEY root = (scope == QLatin1String("Machine"))
        ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const wchar_t *subKey = (scope == QLatin1String("Machine"))
        ? L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
        : L"Environment";

    HKEY hKey;
    LONG rc = RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE, &hKey);
    if (rc != ERROR_SUCCESS) return false;
    rc = RegDeleteValueW(hKey, reinterpret_cast<const wchar_t*>(name.utf16()));
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

// Broadcast WM_SETTINGCHANGE так же как это делает GUI-панель «Переменные среды».
// Без этого свежие терминалы/приложения не заметят изменений в реестре до
// перезапуска explorer'а. С этим — новые процессы читают уже обновлённые env.
void broadcastEnvChange()
{
    DWORD_PTR result;
    SendMessageTimeoutW(
        HWND_BROADCAST,
        WM_SETTINGCHANGE,
        0,
        reinterpret_cast<LPARAM>(L"Environment"),
        SMTO_ABORTIFHUNG,
        3000,
        &result);
}
#endif // Q_OS_WIN

} // namespace

namespace ProxyEnv {

void setProxy(const QString &socksUrl,
              const QString &httpUrl,
              const QString &scope)
{
#ifdef Q_OS_WIN
    // HTTPS_PROXY — читает 99% CLI-tools (curl, git, npm, pip, requests,
    // Claude Code, Node fetch, cargo, go). Использует и http и socks-схемы.
    writeReg(scope, QStringLiteral("HTTPS_PROXY"), httpUrl);
    writeReg(scope, QStringLiteral("HTTP_PROXY"),  httpUrl);

    // ALL_PROXY — читают некоторые SOCKS-only клиенты (curl с
    // --socks5, ssh -o ProxyCommand). Даём SOCKS-версию.
    writeReg(scope, QStringLiteral("ALL_PROXY"),   socksUrl);

    // NO_PROXY — исключения. Локалхост, локалки, наш VPN-сервер и
    // домены Anthropic не пишем сюда — они должны идти через прокси.
    writeReg(scope, QStringLiteral("NO_PROXY"),
             QStringLiteral("localhost,127.0.0.1,::1,10.*,192.168.*,172.16.*,172.17.*,172.18.*,172.19.*,172.20.*,172.21.*,172.22.*,172.23.*,172.24.*,172.25.*,172.26.*,172.27.*,172.28.*,172.29.*,172.30.*,172.31.*"));

    broadcastEnvChange();
    qInfo().noquote() << "ProxyEnv: set" << scope << "→"
                      << "HTTPS_PROXY=" << httpUrl
                      << ", ALL_PROXY=" << socksUrl;
#else
    Q_UNUSED(socksUrl);
    Q_UNUSED(httpUrl);
    Q_UNUSED(scope);
#endif
}

void clearProxy(const QString &scope)
{
#ifdef Q_OS_WIN
    deleteReg(scope, QStringLiteral("HTTPS_PROXY"));
    deleteReg(scope, QStringLiteral("HTTP_PROXY"));
    deleteReg(scope, QStringLiteral("ALL_PROXY"));
    deleteReg(scope, QStringLiteral("NO_PROXY"));
    broadcastEnvChange();
    qInfo().noquote() << "ProxyEnv: cleared" << scope;
#else
    Q_UNUSED(scope);
#endif
}

} // namespace ProxyEnv
