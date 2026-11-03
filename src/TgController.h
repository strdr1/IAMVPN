#pragma once

#include <QObject>
#include <QString>

class QProcess;
class Store;

// ─────────────────────────────────────────────────────────────────────────
//  TgController — управляет встроенным Telegram-прокси (tg-ws-proxy ядро).
//
//  Запускает Python-ядро (engine/tgproxy) headless: оно поднимает локальный
//  MTProto-прокси на 127.0.0.1 и гонит Telegram через наш Cloudflare Worker
//  (трафик к Telegram идёт через IP Cloudflare → провайдер не режет).
//
//  Из вывода ядра парсим готовую tg://proxy-ссылку и отдаём в UI.
// ─────────────────────────────────────────────────────────────────────────
class TgController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString tgLink READ tgLink NOTIFY tgLinkChanged)
    // Домен Cloudflare Worker (можно менять из UI).
    Q_PROPERTY(QString workerDomain READ workerDomain WRITE setWorkerDomain NOTIFY workerDomainChanged)

public:
    explicit TgController(Store *store, QObject *parent = nullptr);
    ~TgController() override;

    bool running() const { return m_running; }
    QString statusText() const { return m_statusText; }
    QString tgLink() const { return m_tgLink; }
    QString workerDomain() const { return m_workerDomain; }
    void setWorkerDomain(const QString &d);

public slots:
    void start();   // запустить локальный TG-прокси
    void stop();     // остановить
    void toggle();

signals:
    void runningChanged();
    void statusTextChanged();
    void tgLinkChanged();
    void workerDomainChanged();

private:
    void setRunning(bool r);
    void setStatus(const QString &t);
    void setTgLink(const QString &l);
    QString engineDir() const;     // путь к engine/tgproxy
    QString pythonExe() const;     // путь к python

    Store *m_store = nullptr;
    QProcess *m_proc = nullptr;
    bool m_running = false;
    QString m_statusText = QStringLiteral("Выключено");
    QString m_tgLink;
    QString m_workerDomain;
    int m_port = 1443;
};
