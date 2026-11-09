#include "UpdateChecker.h"
#include "Store.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QDir>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>

UpdateChecker::UpdateChecker(Store *store, QObject *parent)
    : QObject(parent), m_store(store)
{
    m_net = new QNetworkAccessManager(this);

    // Авто-проверка раз в 6 часов, пока приложение запущено.
    m_autoTimer = new QTimer(this);
    m_autoTimer->setInterval(6 * 60 * 60 * 1000);
    connect(m_autoTimer, &QTimer::timeout, this, &UpdateChecker::autoTick);
    m_autoTimer->start();

    // При каждом старте — проверка (через 5 сек, чтобы не мешать запуску).
    QTimer::singleShot(5000, this, [this]() { check(); });
}

QString UpdateChecker::currentVersion()
{
    return QCoreApplication::applicationVersion();
}

void UpdateChecker::autoTick()
{
    check();   // фоновая проверка раз в 6 часов
}

// Сравнение "1.2.3" vs "1.2.4": возвращает -1/0/+1.
int UpdateChecker::cmpVersions(const QString &a, const QString &b)
{
    const QStringList sa = a.split('.', Qt::SkipEmptyParts);
    const QStringList sb = b.split('.', Qt::SkipEmptyParts);
    const int n = qMax(sa.size(), sb.size());
    for (int i = 0; i < n; ++i) {
        int ai = i < sa.size() ? sa[i].toInt() : 0;
        int bi = i < sb.size() ? sb[i].toInt() : 0;
        if (ai < bi) return -1;
        if (ai > bi) return 1;
    }
    return 0;
}

void UpdateChecker::check(const QString &updateUrl)
{
    if (m_checking) return;

    const QString url = updateUrl.isEmpty() ? defaultUpdateUrl() : updateUrl;

    m_checking = true; emit checkingChanged();
    m_status = QStringLiteral("Проверяю…"); emit statusTextChanged();

    // Запомним время проверки (даже неудачной — чтобы не спамить).
    if (m_store)
        m_store->setSetting(QStringLiteral("lastUpdateCheck"),
                             QDateTime::currentDateTime().toString(Qt::ISODate));

    QNetworkRequest req{ QUrl(url) };
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("IAMVPN"));
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        m_checking = false; emit checkingChanged();
        if (r->error() != QNetworkReply::NoError) {
            m_status = QStringLiteral("Ошибка сети: ") + r->errorString();
            emit statusTextChanged();
            r->deleteLater();
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(r->readAll()).object();
        r->deleteLater();
        m_latest = obj.value(QStringLiteral("version")).toString();
#ifdef Q_OS_MACOS
        // Для Mac — отдельный DMG; нет его в манифесте — обновления нет.
        m_url    = obj.value(QStringLiteral("url_mac")).toString();
#else
        m_url    = obj.value(QStringLiteral("url")).toString();
#endif
        m_notes  = obj.value(QStringLiteral("notes")).toString();
        emit latestVersionChanged(); emit downloadUrlChanged(); emit notesChanged();

        if (m_latest.isEmpty() || m_url.isEmpty()) {
            m_status = QStringLiteral("Неверный ответ сервера");
        } else if (cmpVersions(m_latest, currentVersion()) > 0) {
            m_avail = true; emit updateAvailableChanged();
            m_status = QStringLiteral("Доступна новая версия %1").arg(m_latest);
        } else {
            m_avail = false; emit updateAvailableChanged();
            m_status = QStringLiteral("У вас актуальная версия (%1)").arg(currentVersion());
        }
        emit statusTextChanged();
    });
}

// Скачать установщик новой версии и запустить его.
// При запуске setup корректно перезапишет старую установку — ключи и
// настройки в %APPDATA% не пострадают.
void UpdateChecker::downloadAndInstall()
{
    if (m_url.isEmpty()) {
        m_status = QStringLiteral("Нет ссылки на установщик");
        emit statusTextChanged();
        return;
    }

    m_status = QStringLiteral("Скачиваю установщик…");
    emit statusTextChanged();

    QNetworkRequest req{ QUrl(m_url) };
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (r->error() != QNetworkReply::NoError) {
            m_status = QStringLiteral("Ошибка скачивания: ") + r->errorString();
            emit statusTextChanged();
            r->deleteLater();
            return;
        }
        const QByteArray data = r->readAll();
        r->deleteLater();

        const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QDir().mkpath(tmp);
#ifdef Q_OS_MACOS
        const QString path = tmp + QStringLiteral("/IAMVPN.dmg");
#else
        const QString path = tmp + QStringLiteral("/IAMVPN-Setup.exe");
#endif
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_status = QStringLiteral("Не удалось сохранить установщик");
            emit statusTextChanged();
            return;
        }
        f.write(data);
        f.close();

#ifdef Q_OS_MACOS
        // Тихой установки на Mac нет: открываем DMG, пользователь
        // перетаскивает IAMVPN в «Программы» (с заменой).
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        m_status = QStringLiteral("Откройте DMG и перетащите IAMVPN в «Программы» с заменой");
        emit statusTextChanged();
        return;
#endif
        m_status = QStringLiteral("Запускаю установщик…");
        emit statusTextChanged();

        // Тихая установка: setup сам прибьёт наш .exe (CloseApplications=force)
        // и перезапустит после копирования файлов (RestartApplications=yes).
        // /VERYSILENT — без окон, /SUPPRESSMSGBOXES — без подтверждений,
        // /NORESTART — не перезагружать ПК.
        const QStringList args{
            QStringLiteral("/VERYSILENT"),
            QStringLiteral("/SUPPRESSMSGBOXES"),
            QStringLiteral("/NORESTART"),
        };
        if (!QProcess::startDetached(path, args)) {
            m_status = QStringLiteral("Не удалось запустить установщик");
            emit statusTextChanged();
        }
        // Сами не выходим — Inno Setup (CloseApplications=force) корректно
        // закроет наш процесс и потом сам перезапустит (RestartApplications
        // в .iss + строка [Run] с Check: WizardSilent на случай /VERYSILENT).
    });
}
