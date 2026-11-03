#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <functional>

class QProcess;
class QTimer;
class QNetworkAccessManager;
class Store;

// ─────────────────────────────────────────────────────────────────────────
//  VlessController — управляет VPN-туннелем через встроенный sing-box.
//
//  Что делает:
//    1. Принимает одиночный ключ (vless/vmess/trojan/ss/hysteria2/tuic)
//       либо ссылку-подписку (happ://add/<url> или прямой http(s)).
//    2. Парсит ключ(и) → строит JSON-конфиг sing-box с маршрутизацией:
//       российский трафик (.ru / geoip-ru) идёт direct (мимо VPN),
//       остальное — через туннель. В режиме «Авто» все серверы подписки
//       собираются в urltest-группу, sing-box сам держит самый быстрый
//       живой сервер и переключается, если он умер.
//    3. Запускает sing-box.exe (от администратора — нужен TUN-режим) и
//       проверяет РЕАЛЬНУЮ связь через clash API (delay-тест сквозь
//       туннель), а не TCP-коннект, который TUN подтверждает локально.
//
//  Наружу (в QML) отдаёт: connected, statusText, список серверов, выбранный.
// ─────────────────────────────────────────────────────────────────────────
class VlessController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    // Список серверов: [{name, host, port, proto, unsupported, uri}] для UI.
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    // Индекс выбранного сервера (в ручном режиме).
    Q_PROPERTY(int currentServer READ currentServer WRITE setCurrentServer NOTIFY currentServerChanged)
    // Режим «Авто»: sing-box сам выбирает лучший живой сервер (urltest).
    Q_PROPERTY(bool autoSelect READ autoSelect WRITE setAutoSelect NOTIFY autoSelectChanged)
    // Имя сервера, через который реально идёт трафик (в «Авто» — выбор urltest).
    Q_PROPERTY(QString activeServerName READ activeServerName NOTIFY activeServerNameChanged)
    // Ссылка подписки (пусто — подписки нет); для кнопки «Обновить».
    Q_PROPERTY(QString subscriptionUrl READ subscriptionUrl NOTIFY subscriptionUrlChanged)
    // Настройка: российский трафик мимо VPN (true) или весь через VPN (false).
    Q_PROPERTY(bool routeRuDirect READ routeRuDirect WRITE setRouteRuDirect NOTIFY routeRuDirectChanged)
    // tg://-ссылка на MTProto-прокси (для кнопки "Открыть Telegram").
    Q_PROPERTY(QString tgProxyLink READ tgProxyLink WRITE setTgProxyLink NOTIFY tgProxyLinkChanged)
    // Список доменов-исключений: мимо VPN И мимо zapret (direct).
    Q_PROPERTY(QStringList bypassSites READ bypassSites NOTIFY bypassSitesChanged)
    // Использовать ли обход DPI (zapret) вместе с VPN.
    Q_PROPERTY(bool useZapret READ useZapret WRITE setUseZapret NOTIFY useZapretChanged)
    // TCP-пинг серверов: ключ "host:port" -> мс; -2 не отвечает, -3 измеряется, -4 UDP.
    Q_PROPERTY(QVariantMap pings READ pings NOTIFY pingsChanged)
    Q_PROPERTY(bool pinging READ pinging NOTIFY pingingChanged)

public:
    explicit VlessController(Store *store, QObject *parent = nullptr);
    ~VlessController() override;

    bool connected() const { return m_connected; }
    bool connecting() const { return m_connecting; }
    QString statusText() const { return m_statusText; }
    QVariantList servers() const { return m_servers; }
    int currentServer() const { return m_currentServer; }
    void setCurrentServer(int idx);
    bool autoSelect() const { return m_autoSelect; }
    void setAutoSelect(bool v);
    QString activeServerName() const { return m_activeName; }
    QString subscriptionUrl() const { return m_subUrl; }
    bool routeRuDirect() const { return m_routeRuDirect; }
    void setRouteRuDirect(bool v);
    QString tgProxyLink() const { return m_tgProxyLink; }
    void setTgProxyLink(const QString &v);
    QStringList bypassSites() const { return m_bypassSites; }
    bool useZapret() const { return m_useZapret; }
    void setUseZapret(bool v);
    QVariantMap pings() const { return m_pings; }
    bool pinging() const { return m_pingPending > 0; }

public slots:
    // Добавить одиночный ключ (vless/vmess/trojan/ss/hy2/tuic).
    void addKey(const QString &uri);
    // Перечитать список ключей из Store.
    void reloadFromStore();
    // Загрузить подписку (happ://add/<url> или http(s)://...). Серверы
    // прошлой подписки заменяются новыми.
    void loadSubscription(const QString &url);
    // Перекачать сохранённую подписку.
    void refreshSubscription();
    // Удалить сервер из списка по индексу.
    void removeServer(int index);
    // Изменить ключ существующего сервера.
    void editServer(int index, const QString &newUri);
    // Сгенерировать QR-код ключа сервера, вернуть file:// путь к PNG (для UI).
    Q_INVOKABLE QString qrForServer(int index);
    // Замерить TCP-пинг до всех серверов (параллельно, таймаут 3 с).
    Q_INVOKABLE void pingAll();
    // Управление списком сайтов-исключений (мимо VPN и zapret).
    void addBypassSite(const QString &domain);
    void removeBypassSite(int index);
    // Запустить локальный Telegram-прокси (tg-ws-proxy через Cloudflare).
    void launchTgProxy();
    // Подключиться к выбранному серверу (или к группе «Авто»).
    void connectVpn();
    // Отключиться.
    void disconnectVpn();
    // Переключатель.
    void toggle();

signals:
    void connectedChanged();
    void connectingChanged();
    void statusTextChanged();
    void serversChanged();
    void currentServerChanged();
    void autoSelectChanged();
    void activeServerNameChanged();
    void subscriptionUrlChanged();
    void routeRuDirectChanged();
    void tgProxyLinkChanged();
    void bypassSitesChanged();
    void useZapretChanged();
    void pingsChanged();
    void pingingChanged();
    void errorOccurred(const QString &message);

private:
    struct Server {
        QString proto;      // vless / vmess / trojan / shadowsocks / hysteria2 / tuic
        QString name;       // подпись (#... из ключа)
        QString host;
        int port = 443;
        QString portRange;  // hysteria2: "20000-30000" (port hopping)
        QString uuid;
        QString password;   // trojan / ss / hy2 / tuic
        QString method;     // ss cipher
        QString vmessSecurity;
        int alterId = 0;
        QString flow;       // xtls-rprx-vision и т.п.
        QString sni;
        QString publicKey;  // reality pbk
        QString shortId;    // reality sid
        QString fingerprint;
        QStringList alpn;
        bool insecure = false;
        QString type;       // tcp / grpc / ws / httpupgrade / http
        QString security;   // reality / tls / none
        QString path;       // ws/httpupgrade/http path
        QString hostHeader; // ws Host-заголовок
        QString serviceName;// grpc serviceName
        QString obfs;       // hy2 obfs type (salamander)
        QString obfsPassword;
        QString congestion; // tuic congestion_control
        QString udpRelayMode;
        QString unsupported;// непусто → sing-box такое не умеет (причина)
        QString rawUri;     // исходная ссылка
        bool valid = false;
    };

    void setConnected(bool c);
    void setConnecting(bool c);
    void setStatus(const QString &t);
    void setActiveName(const QString &n);
    void rebuildServerList();
    void saveKeys();
    void syncZapretExclude();   // пишет bypass-сайты в zapret exclude
    void syncServerIpExclude(); // пишет IP серверов в zapret ipset-exclude

    // Парсинг одной ссылки в структуру Server (по схеме).
    Server parseUri(const QString &uri) const;
    // Outbound sing-box для одного сервера.
    QJsonObject buildOutbound(const Server &s, const QString &tag) const;
    // Сгенерировать JSON-конфиг sing-box для набора серверов
    // (1 сервер → outbound "proxy"; несколько → urltest "proxy").
    QString buildConfig(const QList<Server> &list) const;
    // Серверы, которые пойдут в конфиг при текущем режиме.
    QList<Server> serversForConnect() const;
    bool useAutoGroup() const;
    int supportedCount() const;

    // Clash API: проверка реальной связи сквозь туннель.
    // cb(ok, delayMs). groupTest — перетест всей urltest-группы.
    void probeTunnel(bool groupTest, std::function<void(bool, int)> cb);
    void queryActiveOutbound();
    void verifyAfterStart(int attempt);
    void restartTunnel();
    void fetchSubscription(const QString &url, bool silent);
    void applySubscriptionData(const QByteArray &data, const QString &url, bool silent);

    QString engineDir() const;
    QString singBoxPath() const;
    QString configPath() const;

    QList<Server> m_serversData;
    QList<Server> m_connectedSet;   // с чем реально запущен sing-box (s0..sN)
    QVariantList m_servers;         // зеркало для QML
    int m_currentServer = 0;
    bool m_autoSelect = true;
    QString m_activeName;
    QString m_subUrl;               // ссылка подписки
    QStringList m_subKeys;          // ключи, пришедшие из подписки
    QVariantMap m_pings;            // "host:port" -> мс
    int m_pingPending = 0;

    bool m_connected = false;
    bool m_connecting = false;
    bool m_routeRuDirect = true;   // по умолчанию .ru мимо VPN
    QString m_tgProxyLink;         // tg://-ссылка на MTProto-прокси
    QStringList m_bypassSites;     // домены-исключения (мимо VPN и zapret)
    bool m_useZapret = true;       // включён ли обход DPI вместе с VPN
    QString m_statusText = QStringLiteral("Не подключено");

    QProcess *m_proc = nullptr;     // процесс sing-box
    Store *m_store = nullptr;       // хранилище ключей/настроек
    QNetworkAccessManager *m_nam = nullptr;
    int m_runId = 0;                // растёт при каждом старте — гасит устаревшие колбэки

    // ── Keep-alive: соединение «никогда» не отключается ─────────────────
    // Каждые 20 сек delay-тест сквозь туннель. В «Авто» при неудаче
    // перетестируем группу (urltest сам уйдёт на живой сервер); если
    // мертво всё 2 раза подряд — перезапуск sing-box.
    QTimer *m_keepAliveTimer = nullptr;
    bool m_userWantsConnected = false;   // пользователь нажал «Включить»
    bool m_probeInFlight = false;
    int  m_consecFailures = 0;
    void keepAliveTick();
    void reconnectInternal();             // тихий пере-старт sing-box
};
