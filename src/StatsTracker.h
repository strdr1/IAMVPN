#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVariantList>

class Store;
class QTimer;
class QNetworkAccessManager;

// ─────────────────────────────────────────────────────────────────────────
//  StatsTracker — учёт сессий и трафика.
//
//  Логика:
//    - sessionStart(server): запоминаем время начала и имя Windows-пользователя.
//    - во время сессии опрашиваем clash-api sing-box (порт 9090) и забираем
//      суммарный трафик (upload/download).
//    - sessionEnd(): считаем длительность, сохраняем сессию в Store и
//      (заглушка) отправляем на центральный сервер uploadStats().
//
//  Для QML отдаёт текущие живые цифры (upBytes/downBytes/elapsed) и историю.
// ─────────────────────────────────────────────────────────────────────────
class StatsTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString user READ user CONSTANT)
    Q_PROPERTY(double upMB READ upMB NOTIFY trafficChanged)
    Q_PROPERTY(double downMB READ downMB NOTIFY trafficChanged)
    Q_PROPERTY(int elapsedSec READ elapsedSec NOTIFY trafficChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    // Адрес центрального сервера (зададим позже). Пусто = не отправляем.
    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)

public:
    explicit StatsTracker(Store *store, QObject *parent = nullptr);

    QString user() const { return m_user; }
    double upMB() const { return m_upBytes / 1048576.0; }
    double downMB() const { return m_downBytes / 1048576.0; }
    int elapsedSec() const;
    bool active() const { return m_active; }
    QVariantList history() const;
    QString serverUrl() const { return m_serverUrl; }
    void setServerUrl(const QString &url);

public slots:
    void sessionStart(const QString &server);
    void sessionEnd();
    // Заглушка под будущий сервер: отправить накопленную стату.
    void uploadStats();

signals:
    void trafficChanged();
    void activeChanged();
    void historyChanged();
    void serverUrlChanged();

private slots:
    void poll();   // опрос clash-api

private:
    Store *m_store;
    QString m_user;
    QString m_server;
    QDateTime m_start;
    qint64 m_upBytes = 0;
    qint64 m_downBytes = 0;
    bool m_active = false;
    QString m_serverUrl;

    QTimer *m_timer = nullptr;
    QNetworkAccessManager *m_net = nullptr;
};
