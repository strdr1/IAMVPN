#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFile>
#include <QMutex>

// ─────────────────────────────────────────────────────────────────────────
//  LogController — единый журнал действий приложения.
//
//  • На старте создаёт файл сессии:
//      %APPDATA%/AM.SALES/logs/session-YYYY-MM-DD_HH-mm-ss.txt
//    Старые файлы не трогает — каждая сессия = отдельный лог.
//    Удаляет только лишнее: оставляет последние 20 файлов.
//
//  • Принимает записи из любого контроллера: log(area, level, message).
//    Дублирует их в кольцевой буфер (последние N строк) — для показа в UI.
//
//  • Экспорт: copyLastLogTo(path) — копирует текущий лог-файл туда, куда
//    выбрал пользователь в диалоге. openLogsFolder() открывает папку
//    в проводнике Windows.
// ─────────────────────────────────────────────────────────────────────────
class LogController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLogPath READ currentLogPath CONSTANT)
    Q_PROPERTY(QString tail READ tail NOTIFY tailChanged)

public:
    explicit LogController(QObject *parent = nullptr);
    ~LogController() override;

    QString currentLogPath() const { return m_path; }
    QString tail() const;   // последние ~200 строк, для UI

public slots:
    // Универсальная запись. level: "INFO"/"WARN"/"ERROR"/"OK"/"DBG".
    void log(const QString &area, const QString &level, const QString &message);

    // Удобные шорткаты — не надо передавать level.
    void info(const QString &area, const QString &message)  { log(area, QStringLiteral("INFO"), message); }
    void warn(const QString &area, const QString &message)  { log(area, QStringLiteral("WARN"), message); }
    void error(const QString &area, const QString &message) { log(area, QStringLiteral("ERROR"), message); }
    void ok(const QString &area, const QString &message)    { log(area, QStringLiteral("OK"), message); }

    // Сохранить копию текущего лог-файла в указанный путь (диалог из QML).
    // Возвращает true при успехе. Принимает file:/// URL или обычный путь.
    bool exportTo(const QString &targetPath);

    // Открыть папку с логами в Проводнике.
    void openLogsFolder();

signals:
    void tailChanged();
    void exported(bool ok, const QString &path);

private:
    QString  m_path;            // абсолютный путь к файлу сессии
    QString  m_dir;             // папка %APPDATA%/AM.SALES/logs
    QFile    m_file;            // открытый файл для дозаписи
    mutable QMutex m_mx;        // запись возможна из разных потоков
    QStringList m_recent;       // кольцо последних строк для UI
    int      m_maxRecent = 400;

    void rotate();              // оставить только 20 последних .txt
};
