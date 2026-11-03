#include "LogController.h"

#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QMutexLocker>
#include <QDesktopServices>
#include <QUrl>
#include <QSysInfo>
#include <QCoreApplication>

LogController::LogController(QObject *parent)
    : QObject(parent)
{
    // %APPDATA%/AM.SALES/logs/  — рядом со store.json, чтобы апдейт не сносил.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_dir = QDir(base).absoluteFilePath(QStringLiteral("logs"));
    QDir().mkpath(m_dir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    m_path = QDir(m_dir).absoluteFilePath(QStringLiteral("session-%1.txt").arg(stamp));

    m_file.setFileName(m_path);
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    rotate();

    // Шапка — чтобы при экспорте сразу было видно окружение.
    log(QStringLiteral("APP"), QStringLiteral("INFO"),
        QStringLiteral("IAMVPN started, version %1, OS %2 (%3)")
            .arg(QCoreApplication::applicationVersion().isEmpty()
                     ? QStringLiteral("1.0.1") : QCoreApplication::applicationVersion(),
                 QSysInfo::prettyProductName(),
                 QSysInfo::currentCpuArchitecture()));
}

LogController::~LogController()
{
    log(QStringLiteral("APP"), QStringLiteral("INFO"), QStringLiteral("Session ended"));
    if (m_file.isOpen())
        m_file.close();
}

QString LogController::tail() const
{
    QMutexLocker lk(&m_mx);
    return m_recent.join(QLatin1Char('\n'));
}

void LogController::log(const QString &area, const QString &level, const QString &message)
{
    const QString line = QStringLiteral("[%1] [%2] [%3] %4")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             level, area, message);

    {
        QMutexLocker lk(&m_mx);
        if (m_file.isOpen()) {
            m_file.write(line.toUtf8());
            m_file.write("\r\n");
            m_file.flush();   // важная штука: не теряем лог при крэше
        }
        m_recent.append(line);
        if (m_recent.size() > m_maxRecent)
            m_recent.removeFirst();
    }
    emit tailChanged();
}

bool LogController::exportTo(const QString &targetPath)
{
    QString target = targetPath;
    if (target.startsWith(QStringLiteral("file:///")))
        target = QUrl(target).toLocalFile();
    if (target.isEmpty()) {
        emit exported(false, QString());
        return false;
    }

    // Сбросим буферы перед копированием.
    {
        QMutexLocker lk(&m_mx);
        m_file.flush();
    }

    // QFile::copy не перезаписывает существующий → удалим заранее.
    if (QFile::exists(target))
        QFile::remove(target);

    const bool ok = QFile::copy(m_path, target);
    log(QStringLiteral("LOG"), ok ? QStringLiteral("OK") : QStringLiteral("ERROR"),
        ok ? QStringLiteral("Exported to ") + target
           : QStringLiteral("Failed to export to ") + target);
    emit exported(ok, target);
    return ok;
}

void LogController::openLogsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_dir));
}

void LogController::rotate()
{
    // Оставить только 20 самых свежих .txt — чтобы папка не разрасталась.
    QDir d(m_dir);
    const auto files = d.entryInfoList(QStringList{QStringLiteral("session-*.txt")},
                                       QDir::Files, QDir::Time);   // новые сверху
    const int keep = 20;
    for (int i = keep; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}
