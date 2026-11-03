#include "TgController.h"
#include "Store.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QTcpServer>
#include <QHostAddress>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

// ─────────────────────────────────────────────────────────────────────────
//  Реализация TgController.
// ─────────────────────────────────────────────────────────────────────────

TgController::TgController(Store *store, QObject *parent)
    : QObject(parent), m_store(store)
{
    if (m_store) {
        // Домен воркера: из настроек либо наш дефолтный.
        m_workerDomain = m_store->setting(QStringLiteral("tgWorkerDomain"),
            QStringLiteral("shiny-hill-d2ef.danecc5678.workers.dev")).toString();
    }
}

TgController::~TgController()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

QString TgController::engineDir() const
{
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/engine/tgproxy");
}

QString TgController::pythonExe() const
{
    // Встроенный portable Python (engine/python) — система Python не нужна.
    const QString embedded = QCoreApplication::applicationDirPath()
        + QStringLiteral("/engine/python/python.exe");
    if (QFileInfo::exists(embedded))
        return embedded;
    return QStringLiteral("python");   // fallback на системный
}

void TgController::setRunning(bool r)
{
    if (m_running == r) return;
    m_running = r;
    emit runningChanged();
}
void TgController::setStatus(const QString &t)
{
    if (m_statusText == t) return;
    m_statusText = t;
    emit statusTextChanged();
}
void TgController::setTgLink(const QString &l)
{
    if (m_tgLink == l) return;
    m_tgLink = l;
    emit tgLinkChanged();
}

void TgController::setWorkerDomain(const QString &d)
{
    if (m_workerDomain == d) return;
    m_workerDomain = d.trimmed();
    if (m_store) m_store->setSetting(QStringLiteral("tgWorkerDomain"), m_workerDomain);
    emit workerDomainChanged();
}

void TgController::start()
{
    if (m_running)
        return;

    const QString dir = engineDir();
    if (!QFileInfo::exists(dir + QStringLiteral("/proxy/tg_ws_proxy.py"))) {
        setStatus(QStringLiteral("Движок Telegram не найден"));
        return;
    }

    setStatus(QStringLiteral("Запуск…"));
    setTgLink(QString());

    // Выбираем свободный порт начиная с 1443 (вдруг занят другим прокси).
    m_port = 1443;
    for (int p = 1443; p <= 1460; ++p) {
        QTcpServer probe;
        if (probe.listen(QHostAddress(QStringLiteral("127.0.0.1")), p)) {
            m_port = p;
            probe.close();
            break;
        }
    }

    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(dir);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    // Запускаем python НАПРЯМУЮ (без -m), указывая полный путь к скрипту-входу.
    // -m требует, чтобы пакет был в sys.path; прямой путь надёжнее.
    // Скрипт сам по __file__ найдёт свой пакет proxy.
    const QString py = pythonExe();
    QStringList args;
    args << QStringLiteral("-u")
         << QStringLiteral("-m") << QStringLiteral("proxy.tg_ws_proxy")
         << QStringLiteral("--port") << QString::number(m_port)
         << QStringLiteral("--host") << QStringLiteral("127.0.0.1");
    if (!m_workerDomain.isEmpty())
        args << QStringLiteral("--cfproxy-worker-domain") << m_workerDomain;

    // Ловим весь вывод (stdout+stderr слиты) — ищем tg://-ссылку.
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString out = QString::fromUtf8(m_proc->readAllStandardOutput());
        // Любой вывод = процесс жив и работает.
        if (!m_running && out.contains(QStringLiteral("Listening"))) {
            setRunning(true);
            setStatus(QStringLiteral("Работает · Telegram через Cloudflare"));
        }
        static const QRegularExpression re(
            QStringLiteral("tg://proxy\\?[^\\s]+"));
        const QRegularExpressionMatch m = re.match(out);
        if (m.hasMatch()) {
            setTgLink(m.captured(0));
            setRunning(true);
            setStatus(QStringLiteral("Работает · Telegram через Cloudflare"));
        }
    });

    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        // Сохраняем весь вывод в лог для диагностики.
        const QByteArray out = m_proc->readAll();
        QFile log(QCoreApplication::applicationDirPath()
                  + QStringLiteral("/engine/tgproxy/tg-last-error.log"));
        if (log.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            log.write("exit code: " + QByteArray::number(code) + "\n");
            log.write(out);
            log.close();
        }
        const bool wasRunning = m_running;
        setRunning(false);
        if (!wasRunning)
            setStatus(QStringLiteral("Ошибка (код %1) — см. tg-last-error.log").arg(code));
        else
            setStatus(QStringLiteral("Выключено"));
    });

    m_proc->start(py, args);
    if (!m_proc->waitForStarted(4000)) {
        setStatus(QStringLiteral("Python не запустился: ") + m_proc->errorString());
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

void TgController::stop()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    // На всякий случай добиваем по имени (python мог остаться).
    QProcess::startDetached(QStringLiteral("taskkill"),
        {QStringLiteral("/F"), QStringLiteral("/FI"),
         QStringLiteral("WINDOWTITLE eq tg_ws_proxy*")});
    setRunning(false);
    setTgLink(QString());
    setStatus(QStringLiteral("Выключено"));
}

void TgController::toggle()
{
    if (m_running)
        stop();
    else
        start();
}
