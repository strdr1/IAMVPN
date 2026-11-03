#pragma once
#include <QString>

// Пишет/убирает системные переменные окружения HTTPS_PROXY / HTTP_PROXY / ALL_PROXY
// и NO_PROXY в HKCU\Environment. Броадкастит WM_SETTINGCHANGE так что новые
// процессы подхватывают. Работает на Windows; на других ОС — no-op.
//
// Используется когда VPN включён с локальным SOCKS-прокси: CLI-приложения
// (Claude Code, git, npm, pip, curl) начинают ходить через нашу дырку.
namespace ProxyEnv {
    // Установить HTTPS_PROXY/HTTP_PROXY/ALL_PROXY = url (например
    // "socks5://127.0.0.1:12335" или "http://127.0.0.1:12335").
    // Плюс NO_PROXY со стандартным списком локальных исключений.
    // scope: "User" (HKCU, без админа) или "Machine" (HKLM, с админом).
    void setProxy(const QString &socksUrl,
                  const QString &httpUrl,
                  const QString &scope = QStringLiteral("User"));

    // Очистить все три переменные (плюс NO_PROXY).
    void clearProxy(const QString &scope = QStringLiteral("User"));
}
