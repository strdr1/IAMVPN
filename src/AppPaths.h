#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────
//  AppPaths — где лежит встроенный движок и куда писать конфиги/логи.
//
//  Windows: всё рядом с .exe (engine/…), как было всегда.
//  macOS:   движок — внутри бандла (IAMVPN.app/Contents/Resources/engine),
//           а писать в бандл нельзя — конфиг и логи в Application Support.
// ─────────────────────────────────────────────────────────────────────────
namespace AppPaths {

inline QString engineDir()
{
#ifdef Q_OS_MACOS
    return QDir::cleanPath(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/../Resources/engine"));
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/engine");
#endif
}

inline QString dataDir()
{
#ifdef Q_OS_MACOS
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d;
#else
    return engineDir() + QStringLiteral("/vpn");
#endif
}

} // namespace AppPaths
