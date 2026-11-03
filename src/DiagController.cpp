#include "DiagController.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QHostInfo>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QTimer>
#include <QDateTime>

// ─────────────────────────────────────────────────────────────────────────
//  DiagController — реализация диагностики.
// ─────────────────────────────────────────────────────────────────────────

DiagController::DiagController(QObject *parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
}

void DiagController::setRunning(bool r)
{
    if (m_running == r) return;
    m_running = r;
    emit runningChanged();
}

void DiagController::resetResults()
{
    m_results.clear();
    emit resultsChanged();
}

void DiagController::addResult(const QString &title, const QString &status,
                                const QString &detail)
{
    QVariantMap row;
    row[QStringLiteral("title")] = title;
    row[QStringLiteral("status")] = status;   // ok / warn / fail / pending
    row[QStringLiteral("detail")] = detail;
    m_results.append(row);
    emit resultsChanged();
}

void DiagController::runAll()
{
    if (m_running) return;
    setRunning(true);
    resetResults();
    m_pending = 0;

    checkInternet();        // 1
    checkProcesses();        // 4
    readEngineLogs();        // 5 — заодно перечитываем логи

    // Если асинхронных проверок не осталось — сразу завершить.
    if (m_pending == 0)
        setRunning(false);
}

// 1. Есть ли вообще интернет: TCP-коннект к 1.1.1.1:443.
void DiagController::checkInternet()
{
    QTcpSocket sock;
    sock.connectToHost(QStringLiteral("1.1.1.1"), 443);
    const bool ok = sock.waitForConnected(2500);
    sock.abort();
    if (ok)
        addResult(QStringLiteral("Интернет (1.1.1.1)"), QStringLiteral("ok"),
                  QStringLiteral("TCP 443 проходит"));
    else
        addResult(QStringLiteral("Интернет"), QStringLiteral("fail"),
                  QStringLiteral("Нет связи. Проверьте подключение"));
}

// 4. Запущены ли наши процессы.
void DiagController::checkProcesses()
{
    auto isRunning = [](const QString &name) -> bool {
        QProcess p;
        p.start(QStringLiteral("tasklist"),
                {QStringLiteral("/FI"),
                 QStringLiteral("IMAGENAME eq ") + name,
                 QStringLiteral("/NH")});
        p.waitForFinished(2500);
        return QString::fromLocal8Bit(p.readAllStandardOutput())
                .contains(name, Qt::CaseInsensitive);
    };

    addResult(QStringLiteral("winws (zapret)"),
              isRunning(QStringLiteral("winws.exe")) ? QStringLiteral("ok")
                                                     : QStringLiteral("warn"),
              isRunning(QStringLiteral("winws.exe"))
                  ? QStringLiteral("процесс работает")
                  : QStringLiteral("не запущен (включите VPN или Обход DPI)"));

    addResult(QStringLiteral("sing-box (VPN)"),
              isRunning(QStringLiteral("sing-box.exe")) ? QStringLiteral("ok")
                                                        : QStringLiteral("warn"),
              isRunning(QStringLiteral("sing-box.exe"))
                  ? QStringLiteral("процесс работает")
                  : QStringLiteral("не запущен (нажмите главную кнопку)"));
}

// 5. Читаем последние строки логов движков.
void DiagController::readEngineLogs()
{
    const QString dir = QCoreApplication::applicationDirPath();
    QString out;
    out += QStringLiteral("=== IAMVPN diagnostics ===\n");
    out += QStringLiteral("time: ") + QDateTime::currentDateTime().toString(Qt::ISODate) + "\n\n";

    // sing-box log (записывается VlessController-ом).
    const QString sbLog = dir + QStringLiteral("/engine/vpn/sing-box.log");
    QFile f(sbLog);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QByteArray data = f.readAll();
        out += QStringLiteral("--- sing-box.log (последние 4 КБ) ---\n");
        out += QString::fromUtf8(data.right(4096));
        out += QStringLiteral("\n\n");
        f.close();
    } else {
        out += QStringLiteral("--- sing-box.log: нет файла (VPN ни разу не запускался)\n\n");
    }

    // Последняя ошибка tg-proxy (если была).
    const QString tgErr = dir + QStringLiteral("/engine/tgproxy/tg-last-error.log");
    QFile ft(tgErr);
    if (ft.exists() && ft.open(QIODevice::ReadOnly)) {
        out += QStringLiteral("--- tg-proxy последняя ошибка ---\n");
        out += QString::fromUtf8(ft.readAll().right(2048));
        out += QStringLiteral("\n\n");
        ft.close();
    }

    m_logs = out;
    emit logsChanged();
}

void DiagController::refreshLogs()
{
    readEngineLogs();
}

void DiagController::copyToClipboard()
{
    QString text = m_logs + QStringLiteral("\n--- Проверки ---\n");
    for (const QVariant &v : m_results) {
        const QVariantMap m = v.toMap();
        text += QString("[%1] %2: %3\n")
            .arg(m.value("status").toString().toUpper(),
                 m.value("title").toString(),
                 m.value("detail").toString());
    }
    QGuiApplication::clipboard()->setText(text);
}
