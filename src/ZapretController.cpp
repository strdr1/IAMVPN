#include "ZapretController.h"
#include "AdminCheck.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QCoreApplication>
#include <QTcpSocket>

// ─────────────────────────────────────────────────────────────────────────
//  Реализация ZapretController с автоперебором профилей.
//
//  winws.exe требует прав администратора (драйвер WinDivert), поэтому
//  профили запускаем через их .bat от админа (PowerShell -Verb RunAs).
//  UAC спросит один раз при первом запуске.
// ─────────────────────────────────────────────────────────────────────────

ZapretController::ZapretController(QObject *parent)
    : QObject(parent)
{
    // Порядок перебора: сначала самый надёжный ALT3, потом остальные.
    // Имена соответствуют файлам в папке engine/.
    m_profiles = {
        QStringLiteral("general (ALT3).bat"),
        QStringLiteral("general (ALT4).bat"),
        QStringLiteral("general (ALT2).bat"),
        QStringLiteral("general (ALT6).bat"),
        QStringLiteral("general (ALT).bat"),
        QStringLiteral("general (ALT5).bat"),
        QStringLiteral("general (ALT7).bat"),
        QStringLiteral("general (ALT8).bat"),
        QStringLiteral("general (ALT9).bat"),
        QStringLiteral("general (ALT10).bat"),
        QStringLiteral("general (ALT11).bat"),
        QStringLiteral("general (FAKE TLS AUTO).bat"),
        QStringLiteral("general (SIMPLE FAKE).bat"),
        QStringLiteral("general.bat"),
    };

    // Красивые имена для UI: вытаскиваем из "general (ALTx).bat" → "ALTx".
    for (const QString &p : m_profiles) {
        QString n = p;
        n.remove(QStringLiteral("general")).remove(QStringLiteral(".bat"));
        n = n.trimmed();
        if (n.startsWith('(')) n = n.mid(1);
        if (n.endsWith(')'))   n.chop(1);
        if (n.isEmpty())       n = QStringLiteral("BASE");
        m_profileNames << n;
    }

    m_checkTimer = new QTimer(this);
    m_checkTimer->setSingleShot(true);
    connect(m_checkTimer, &QTimer::timeout,
            this, &ZapretController::checkCurrentProfile);

    setRunning(isWinwsRunning());
}

void ZapretController::setSelectedProfile(const QString &name)
{
    if (m_selectedProfile == name) return;
    m_selectedProfile = name;
    emit selectedProfileChanged();
}

QString ZapretController::engineDir() const
{
    // engine/ лежит рядом с исполняемым файлом.
    return QCoreApplication::applicationDirPath() + QStringLiteral("/engine");
}

void ZapretController::setRunning(bool r)
{
    if (m_running == r) return;
    m_running = r;
    emit runningChanged();
}

void ZapretController::setStatus(const QString &text)
{
    if (m_statusText == text) return;
    m_statusText = text;
    emit statusTextChanged();
}

void ZapretController::setCurrentProfile(const QString &name)
{
    if (m_currentProfile == name) return;
    m_currentProfile = name;
    emit currentProfileChanged();
}

void ZapretController::setSearching(bool s)
{
    if (m_searching == s) return;
    m_searching = s;
    emit searchingChanged();
}

bool ZapretController::isWinwsRunning() const
{
    QProcess proc;
    proc.start(QStringLiteral("tasklist"),
               {QStringLiteral("/FI"),
                QStringLiteral("IMAGENAME eq winws.exe"),
                QStringLiteral("/NH")});
    proc.waitForFinished(3000);
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    return out.contains(QStringLiteral("winws.exe"), Qt::CaseInsensitive);
}

// Тестовое соединение: пробуем достучаться до известного "трудного" хоста.
// Если обход работает — TLS-соединение установится. Это грубая проверка,
// но для автоперебора её достаточно: рабочий профиль = соединение проходит.
bool ZapretController::testConnection() const
{
    const QStringList hosts = {
        QStringLiteral("www.youtube.com"),
        QStringLiteral("rutracker.org")
    };
    for (const QString &h : hosts) {
        QTcpSocket sock;
        sock.connectToHost(h, 443);
        if (sock.waitForConnected(2500)) {
            sock.disconnectFromHost();
            return true;
        }
    }
    return false;
}

void ZapretController::killWinws()
{
    // Приложение уже от админа (манифест) — глушим напрямую.
    QProcess::startDetached(QStringLiteral("taskkill"),
        {QStringLiteral("/F"), QStringLiteral("/IM"),
         QStringLiteral("winws.exe")});
}

void ZapretController::start()
{
    if (m_searching || m_running)
        return;

    // winws и WinDivert требуют админ-прав. Если их нет — сразу понятная
    // ошибка в статус, а не "процесс упал" через 3 секунды.
    if (!AdminCheck::isElevated()) {
        setStatus(QStringLiteral("Нет прав администратора (zapret не запустится)"));
        return;
    }

    setSearching(true);

    // Если пользователь выбрал конкретный профиль — стартуем именно его
    // (без перебора). Иначе — автоподбор с ALT3 в начале.
    if (!m_selectedProfile.isEmpty()) {
        const int idx = m_profileNames.indexOf(m_selectedProfile);
        if (idx >= 0) {
            m_profileIndex = idx - 1;
            setStatus(QStringLiteral("Запуск %1…").arg(m_selectedProfile));
            tryNextProfile();
            return;
        }
    }
    m_profileIndex = -1;
    setStatus(QStringLiteral("Подбор стратегии…"));
    tryNextProfile();
}

void ZapretController::tryNextProfile()
{
    // Перед запуском нового профиля глушим предыдущий winws.
    killWinws();

    ++m_profileIndex;
    if (m_profileIndex >= m_profiles.size()) {
        // Профили кончились — ни один не сработал.
        setSearching(false);
        setRunning(false);
        setCurrentProfile(QString());
        setStatus(QStringLiteral("Не удалось подобрать стратегию"));
        return;
    }

    const QString prof = m_profiles.at(m_profileIndex);
    const QString batPath = engineDir() + QStringLiteral("/") + prof;

    if (!QFileInfo::exists(batPath)) {
        QTimer::singleShot(50, this, &ZapretController::tryNextProfile);
        return;
    }

    // Красивое имя для UI (без "general" и ".bat", без скобок).
    QString nice = prof;
    nice.remove(QStringLiteral("general")).remove(QStringLiteral(".bat"));
    nice = nice.trimmed();
    if (nice.startsWith('(')) nice = nice.mid(1);
    if (nice.endsWith(')')) nice.chop(1);
    if (nice.isEmpty()) nice = QStringLiteral("BASE");
    setCurrentProfile(nice);
    setStatus(QStringLiteral("Проверяю стратегию %1…").arg(nice));

    // Приложение уже от админа — запускаем .bat напрямую.
    // Через PowerShell Start-Process со скрытым окном и рабочей папкой engine/
    // (там относительные пути bin\ и lists\). RunAs больше НЕ нужен.
    const QString workDir = engineDir();
    const QString ps = QStringLiteral(
        "Start-Process -FilePath '%1' -WorkingDirectory '%2' "
        "-WindowStyle Hidden").arg(batPath, workDir);
    QProcess::startDetached(QStringLiteral("powershell"),
        {QStringLiteral("-NoProfile"),
         QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
         QStringLiteral("-Command"), ps});

    // Даём профилю время подняться, затем проверим результат.
    m_checkTimer->start(3500);
}

void ZapretController::checkCurrentProfile()
{
    const bool up = isWinwsRunning();
    const bool ok = up && testConnection();

    if (ok) {
        setSearching(false);
        setRunning(true);
        setStatus(QStringLiteral("Защита включена · %1").arg(m_currentProfile));
        return;
    }
    // Ручной выбор: НЕ перебираем дальше. Если winws запущен — оставим как
    // есть (юзер сам решит); иначе считаем что профиль не стартовал.
    if (!m_selectedProfile.isEmpty()) {
        setSearching(false);
        if (up) {
            setRunning(true);
            setStatus(QStringLiteral("Запущен %1 (тест не прошёл)").arg(m_currentProfile));
        } else {
            setRunning(false);
            setStatus(QStringLiteral("%1 не стартовал").arg(m_currentProfile));
        }
        return;
    }
    // Авторежим — пробуем следующий.
    setStatus(QStringLiteral("Стратегия %1 не подошла, пробую дальше…")
              .arg(m_currentProfile));
    QTimer::singleShot(200, this, &ZapretController::tryNextProfile);
}

void ZapretController::stop()
{
    m_checkTimer->stop();
    setSearching(false);
    killWinws();
    setRunning(false);
    setCurrentProfile(QString());
    setStatus(QStringLiteral("Выключено"));
}

void ZapretController::toggle()
{
    if (m_running || m_searching)
        stop();
    else
        start();
}
