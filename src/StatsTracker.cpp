#include "StatsTracker.h"
#include "Store.h"

#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcessEnvironment>

// ─────────────────────────────────────────────────────────────────────────
//  Реализация StatsTracker.
//
//  Трафик берём из clash-api sing-box: GET http://127.0.0.1:9090/traffic
//  отдаёт поток {up, down} (мгновенная скорость) — мы накапливаем. Проще и
//  надёжнее — /connections, где есть суммарные uploadTotal/downloadTotal.
// ─────────────────────────────────────────────────────────────────────────

StatsTracker::StatsTracker(Store *store, QObject *parent)
    : QObject(parent), m_store(store)
{
    // Имя текущего пользователя Windows.
    m_user = QProcessEnvironment::systemEnvironment()
                 .value(QStringLiteral("USERNAME"), QStringLiteral("user"));

    m_net = new QNetworkAccessManager(this);

    m_timer = new QTimer(this);
    m_timer->setInterval(2000);   // опрос каждые 2 сек
    connect(m_timer, &QTimer::timeout, this, &StatsTracker::poll);

    // Восстановим сохранённый адрес сервера, если был.
    if (m_store)
        m_serverUrl = m_store->setting(QStringLiteral("serverUrl")).toString();
}

int StatsTracker::elapsedSec() const
{
    if (!m_active || !m_start.isValid())
        return 0;
    return static_cast<int>(m_start.secsTo(QDateTime::currentDateTime()));
}

QVariantList StatsTracker::history() const
{
    return m_store ? m_store->sessions() : QVariantList{};
}

void StatsTracker::setServerUrl(const QString &url)
{
    if (m_serverUrl == url) return;
    m_serverUrl = url;
    if (m_store) m_store->setSetting(QStringLiteral("serverUrl"), url);
    emit serverUrlChanged();
}

void StatsTracker::sessionStart(const QString &server)
{
    m_server = server;
    m_start = QDateTime::currentDateTime();
    m_upBytes = 0;
    m_downBytes = 0;
    m_active = true;
    emit activeChanged();
    emit trafficChanged();
    m_timer->start();
}

void StatsTracker::sessionEnd()
{
    if (!m_active)
        return;
    m_timer->stop();
    m_active = false;

    // Сохраняем сессию в историю.
    QJsonObject s;
    s["user"] = m_user;
    s["server"] = m_server;
    s["start"] = m_start.toString(Qt::ISODate);
    s["durationSec"] = elapsedSec();
    s["upBytes"] = static_cast<double>(m_upBytes);
    s["downBytes"] = static_cast<double>(m_downBytes);
    if (m_store)
        m_store->addSession(s);

    emit activeChanged();
    emit historyChanged();

    // Заглушка: отправить на центральный сервер (когда он появится).
    uploadStats();
}

void StatsTracker::poll()
{
    // Спрашиваем суммарный трафик у clash-api sing-box.
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:9090/connections")));
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject obj =
                QJsonDocument::fromJson(reply->readAll()).object();
            // sing-box clash-api: downloadTotal / uploadTotal в байтах.
            if (obj.contains("uploadTotal"))
                m_upBytes = static_cast<qint64>(obj.value("uploadTotal").toDouble());
            if (obj.contains("downloadTotal"))
                m_downBytes = static_cast<qint64>(obj.value("downloadTotal").toDouble());
        }
        reply->deleteLater();
        emit trafficChanged();
    });
}

void StatsTracker::uploadStats()
{
    // ── ЗАГЛУШКА под будущий центральный сервер ────────────────────────
    // Когда дашь адрес сервера (setServerUrl), здесь будем POST'ить JSON со
    // статой всех сессий. Сейчас, если serverUrl пуст — просто выходим.
    if (m_serverUrl.isEmpty())
        return;

    QNetworkRequest req{ QUrl(m_serverUrl) };
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));

    QJsonObject payload;
    payload["user"] = m_user;
    QJsonArray arr;
    const QVariantList hist = history();
    for (const QVariant &v : hist)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    payload["sessions"] = arr;

    // Отправляем (ответ нам не важен для прототипа).
    QNetworkReply *reply = m_net->post(req,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}
