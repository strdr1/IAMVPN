#include "VlessController.h"
#include "Store.h"
#include "AdminCheck.h"
#include "ProxyEnv.h"

#ifdef Q_OS_WIN
#  include <windows.h>  // CREATE_NO_WINDOW для скрытия консоли sing-box
#endif
#ifdef Q_OS_MACOS
#  include <signal.h>   // kill(pid, 0) — жив ли root-процесс
#  include <cerrno>
#endif

#include "AppPaths.h"

#include <QProcess>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QHostAddress>
#include <QHostInfo>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <QRegularExpression>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────
//  Реализация VlessController.
//
//  Конфиг sing-box строим по образцу рабочего Hiddify-конфига:
//   - inbound TUN (весь системный трафик заворачивается в туннель);
//   - outbound(ы) по ключам; при нескольких серверах в «Авто» —
//     urltest-группа "proxy" поверх s0..sN;
//   - правила маршрута: домены .ru и geoip-ru → direct (мимо VPN).
// ─────────────────────────────────────────────────────────────────────────

namespace {
// Порт локального mixed-inbound (SOCKS5 + HTTP на одном порту).
// 12335, а не 12334 — 12334 занят Hiddify. На одинаковом порту тот,
// кто стартует вторым, падает с "address already in use", а ещё
// HTTPS_PROXY двух клиентов затирают друг друга.
constexpr int kProxyPort = 12335;
// clash API sing-box (StatsTracker тоже берёт трафик отсюда).
const char kClashApi[] = "http://127.0.0.1:9090";
// URL для delay-теста сквозь туннель (204 без тела).
const char kTestUrl[] = "https://www.gstatic.com/generate_204";

QString proxyUrl(const char *scheme)
{
    return QStringLiteral("%1://127.0.0.1:%2")
        .arg(QLatin1String(scheme))
        .arg(kProxyPort);
}

// base64 из подписок/ключей: бывает url-safe, без паддинга, с переносами.
QByteArray decodeB64(QString s)
{
    s.remove(QRegularExpression(QStringLiteral("\\s")));
    s.replace(QLatin1Char('-'), QLatin1Char('+')).replace(QLatin1Char('_'), QLatin1Char('/'));
    while (s.size() % 4)
        s.append(QLatin1Char('='));
    const auto r = QByteArray::fromBase64Encoding(s.toLatin1(),
                       QByteArray::AbortOnBase64DecodingErrors);
    return r ? *r : QByteArray();
}

// Ручной разбор scheme://user@host:port/?query#name. QUrl ломается на
// диапазонах портов hysteria2 ("443,20000-30000") и на base64 с '/'.
struct UriParts {
    QString scheme, user, host, port, query, fragment;
    bool ok = false;
};

UriParts splitUri(const QString &uri)
{
    UriParts p;
    const int se = uri.indexOf(QStringLiteral("://"));
    if (se <= 0)
        return p;
    p.scheme = uri.left(se).toLower();
    QString rest = uri.mid(se + 3);
    const int hashPos = rest.indexOf(QLatin1Char('#'));
    if (hashPos >= 0) {
        p.fragment = QUrl::fromPercentEncoding(rest.mid(hashPos + 1).toUtf8()).trimmed();
        rest = rest.left(hashPos);
    }
    const int q = rest.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        p.query = rest.mid(q + 1);
        rest = rest.left(q);
    }
    const int at = rest.lastIndexOf(QLatin1Char('@'));
    QString hostport = rest;
    if (at >= 0) {
        p.user = QUrl::fromPercentEncoding(rest.left(at).toUtf8());
        hostport = rest.mid(at + 1);
    }
    const int slash = hostport.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        hostport = hostport.left(slash);
    if (hostport.startsWith(QLatin1Char('['))) {
        const int rb = hostport.indexOf(QLatin1Char(']'));
        if (rb < 0)
            return p;
        p.host = hostport.mid(1, rb - 1);
        const QString tail = hostport.mid(rb + 1);
        if (tail.startsWith(QLatin1Char(':')))
            p.port = tail.mid(1);
    } else {
        const int c = hostport.indexOf(QLatin1Char(':'));
        if (c >= 0) {
            p.host = hostport.left(c);
            p.port = hostport.mid(c + 1);
        } else {
            p.host = hostport;
        }
    }
    p.ok = !p.host.isEmpty();
    return p;
}

QString qv(const QUrlQuery &q, const char *key)
{
    return q.queryItemValue(QLatin1String(key), QUrl::FullyDecoded).trimmed();
}

bool isTrue(const QString &v)
{
    return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

bool isIpLiteral(const QString &host)
{
    QHostAddress a;
    return a.setAddress(host);
}

// Сервер с выходом в России (по подписи из подписки): через него
// заблокированное всё равно не откроется, а пинг у него лучший — urltest
// выбрал бы именно его. Поэтому в «Авто» такие не берём.
bool isRuExit(const QString &name)
{
    return name.contains(QStringLiteral("🇷🇺"))
        || name.contains(QStringLiteral("Росси"), Qt::CaseInsensitive)
        || name.contains(QStringLiteral("Russia"), Qt::CaseInsensitive);
}

// Протоколы без TCP — TCP-пинг к ним бессмысленен.
bool isUdpProto(const QString &proto)
{
    return proto == QLatin1String("hysteria2") || proto == QLatin1String("tuic");
}
} // namespace

VlessController::VlessController(Store *store, QObject *parent)
    : QObject(parent), m_store(store)
{
    // Запросы к локальному clash API и к подписке — без прокси из окружения.
    m_nam = new QNetworkAccessManager(this);
    m_nam->setProxy(QNetworkProxy::NoProxy);

    // Keep-alive: каждые 20 сек проверяем что туннель жив и проходит трафик.
    m_keepAliveTimer = new QTimer(this);
    m_keepAliveTimer->setInterval(20000);
    connect(m_keepAliveTimer, &QTimer::timeout, this, &VlessController::keepAliveTick);

    if (m_store) {
        // Восстанавливаем настройку маршрутизации.
        m_routeRuDirect = m_store->setting(
            QStringLiteral("routeRuDirect"), true).toBool();
        // White-label: no built-in MTProto proxy — the user sets their own.
        const QString kDefaultTgProxy;
        m_tgProxyLink = m_store->setting(QStringLiteral("tgProxyLink"),
            kDefaultTgProxy).toString();
        // Сайты-исключения.
        m_bypassSites = m_store->setting(QStringLiteral("bypassSites")).toStringList();
        // Использовать ли zapret (по умолчанию да).
#ifdef Q_OS_WIN
        m_useZapret = m_store->setting(QStringLiteral("useZapret"), true).toBool();
#else
        m_useZapret = false;   // zapret (winws) есть только под Windows
#endif
        // Режим выбора сервера и подписка.
        m_autoSelect = m_store->setting(QStringLiteral("autoSelect"), true).toBool();
        m_subUrl = m_store->setting(QStringLiteral("subUrl")).toString();
        m_subKeys = m_store->setting(QStringLiteral("subKeys")).toStringList();
        m_currentServer = m_store->setting(QStringLiteral("currentServer"), 0).toInt();

        // White-label: no built-in keys on first run — the user adds their own.
        for (const QString &uri : m_store->keys()) {
            const Server s = parseUri(uri);
            if (s.valid)
                m_serversData.append(s);
        }
        if (m_currentServer < 0 || m_currentServer >= m_serversData.size())
            m_currentServer = 0;
        rebuildServerList();
    }

    // Подписка могла обновиться (сервера переехали) — тихо перекачиваем.
    if (!m_subUrl.isEmpty())
        QTimer::singleShot(1500, this, &VlessController::refreshSubscription);
}

VlessController::~VlessController()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
#ifdef Q_OS_MACOS
    // Помощник и сам гасит sing-box, когда приложение завершится, —
    // флаг убираем, чтобы это случилось сразу.
    QFile::remove(dataDir() + QStringLiteral("/sb.run"));
#endif
}

QString VlessController::engineDir() const
{
    return AppPaths::engineDir();
}
QString VlessController::dataDir() const
{
    return AppPaths::dataDir();
}
QString VlessController::singBoxPath() const
{
#ifdef Q_OS_WIN
    return engineDir() + QStringLiteral("/vpn/sing-box.exe");
#else
    return engineDir() + QStringLiteral("/vpn/sing-box");
#endif
}
QString VlessController::configPath() const
{
    return dataDir() + QStringLiteral("/config.json");
}

void VlessController::setConnected(bool c)
{
    if (m_connected == c) return;
    m_connected = c;
    emit connectedChanged();
}
void VlessController::setConnecting(bool c)
{
    if (m_connecting == c) return;
    m_connecting = c;
    emit connectingChanged();
}
void VlessController::setStatus(const QString &t)
{
    if (m_statusText == t) return;
    m_statusText = t;
    emit statusTextChanged();
}
void VlessController::setActiveName(const QString &n)
{
    if (m_activeName == n) return;
    m_activeName = n;
    emit activeServerNameChanged();
}

// Клик по серверу в списке = ручной выбор (режим «Авто» выключается).
// Если туннель уже поднят — сразу переключаемся на новый сервер.
void VlessController::setCurrentServer(int idx)
{
    if (idx < 0 || idx >= m_serversData.size())
        return;
    const bool changed = idx != m_currentServer || m_autoSelect;
    if (idx != m_currentServer) {
        m_currentServer = idx;
        if (m_store) m_store->setSetting(QStringLiteral("currentServer"), idx);
        emit currentServerChanged();
    }
    if (m_autoSelect) {
        m_autoSelect = false;
        if (m_store) m_store->setSetting(QStringLiteral("autoSelect"), false);
        emit autoSelectChanged();
    }
    if (changed && (m_connected || m_connecting))
        restartTunnel();
}

void VlessController::setAutoSelect(bool v)
{
    if (m_autoSelect == v) return;
    m_autoSelect = v;
    if (m_store) m_store->setSetting(QStringLiteral("autoSelect"), v);
    emit autoSelectChanged();
    if (m_connected || m_connecting)
        restartTunnel();
}

void VlessController::setRouteRuDirect(bool v)
{
    if (m_routeRuDirect == v) return;
    m_routeRuDirect = v;
    if (m_store) m_store->setSetting(QStringLiteral("routeRuDirect"), v);
    emit routeRuDirectChanged();
}

void VlessController::setTgProxyLink(const QString &v)
{
    if (m_tgProxyLink == v) return;
    m_tgProxyLink = v;
    if (m_store) m_store->setSetting(QStringLiteral("tgProxyLink"), v);
    emit tgProxyLinkChanged();
}

void VlessController::setUseZapret(bool v)
{
    if (m_useZapret == v) return;
    m_useZapret = v;
    if (m_store) m_store->setSetting(QStringLiteral("useZapret"), v);
    emit useZapretChanged();
}

// Запуск локального Telegram-прокси (tg-ws-proxy). Ищем .exe в типичных
// местах. Он поднимает MTProto на 127.0.0.1 и гонит TG через Cloudflare.
void VlessController::launchTgProxy()
{
    const QStringList candidates = {
        QDir::homePath() + QStringLiteral("/Downloads/TgWsProxy_windows.exe"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/TgWsProxy_windows.exe"),
        QDir::homePath() + QStringLiteral("/Desktop/TgWsProxy_windows.exe"),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            QProcess::startDetached(path, {}, QFileInfo(path).absolutePath());
            setStatus(QStringLiteral("Telegram-прокси запущен"));
            return;
        }
    }
    emit errorOccurred(QStringLiteral(
        "TgWsProxy не найден. Положите TgWsProxy_windows.exe в Downloads."));
}

void VlessController::saveKeys()
{
    if (!m_store) return;
    QStringList keys;
    for (const Server &s : m_serversData)
        keys << s.rawUri;
    m_store->setKeys(keys);
}

// ── Удаление сервера из списка ──────────────────────────────────────────
void VlessController::removeServer(int index)
{
    if (index < 0 || index >= m_serversData.size())
        return;
    m_subKeys.removeAll(m_serversData.at(index).rawUri);
    if (m_store) m_store->setSetting(QStringLiteral("subKeys"), m_subKeys);
    m_serversData.removeAt(index);
    saveKeys();
    if (m_currentServer > index
        || m_currentServer >= m_serversData.size()) {
        m_currentServer = qMax(0, m_currentServer - 1);
        emit currentServerChanged();
    }
    rebuildServerList();
    setStatus(QStringLiteral("Сервер удалён"));
}

// Изменить ключ сервера: парсим новую ссылку и заменяем на месте.
void VlessController::editServer(int index, const QString &newUri)
{
    if (index < 0 || index >= m_serversData.size())
        return;
    const Server s = parseUri(newUri);
    if (!s.valid) {
        emit errorOccurred(QStringLiteral("Неверный ключ"));
        return;
    }
    const int subIdx = m_subKeys.indexOf(m_serversData.at(index).rawUri);
    if (subIdx >= 0) {
        m_subKeys[subIdx] = s.rawUri;
        if (m_store) m_store->setSetting(QStringLiteral("subKeys"), m_subKeys);
    }
    m_serversData[index] = s;
    saveKeys();
    rebuildServerList();
    setStatus(QStringLiteral("Ключ изменён: %1").arg(s.name));
}

// Сгенерировать QR-код ключа через встроенный Python (qrcode).
// Возвращает file:// путь к PNG для отображения в QML Image.
QString VlessController::qrForServer(int index)
{
    if (index < 0 || index >= m_serversData.size())
        return QString();
    const QString uri = m_serversData.at(index).rawUri;
    const QString dir = QCoreApplication::applicationDirPath();
    const QString py = dir + QStringLiteral("/engine/python/python.exe");
    const QString out = dir + QStringLiteral("/engine/qr.png");

    if (!QFileInfo::exists(py))
        return QString();

    // Однострочный Python: qrcode → PNG. Если qrcode нет — вернём пусто.
    const QString code = QStringLiteral(
        "import sys\n"
        "try:\n"
        " import qrcode\n"
        " img=qrcode.make(sys.argv[1])\n"
        " img.save(sys.argv[2])\n"
        "except Exception as e:\n"
        " sys.exit(1)\n");
    const QString scriptPath = dir + QStringLiteral("/engine/_qr.py");
    QFile sf(scriptPath);
    if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sf.write(code.toUtf8());
        sf.close();
    }
    QProcess p;
    p.start(py, {scriptPath, uri, out});
    p.waitForFinished(5000);
    if (QFileInfo::exists(out) && p.exitCode() == 0)
        return QStringLiteral("file:///") + out;
    return QString();
}

// ── Сайты-исключения (мимо VPN и zapret) ────────────────────────────────
void VlessController::addBypassSite(const QString &domain)
{
    QString d = domain.trimmed().toLower();
    // Чистим от схемы/путей: оставляем только домен.
    d.remove(QRegularExpression(QStringLiteral("^https?://")));
    d = d.section('/', 0, 0);
    if (d.isEmpty() || m_bypassSites.contains(d))
        return;
    m_bypassSites << d;
    if (m_store) m_store->setSetting(QStringLiteral("bypassSites"), m_bypassSites);
    syncZapretExclude();
    emit bypassSitesChanged();
}

void VlessController::removeBypassSite(int index)
{
    if (index < 0 || index >= m_bypassSites.size())
        return;
    m_bypassSites.removeAt(index);
    if (m_store) m_store->setSetting(QStringLiteral("bypassSites"), m_bypassSites);
    syncZapretExclude();
    emit bypassSitesChanged();
}

// Пишем сайты-исключения в zapret list-exclude-user.txt, чтобы zapret их
// не трогал (не фрагментировал). engine/lists рядом с .exe.
void VlessController::syncZapretExclude()
{
#ifndef Q_OS_WIN
    return;   // zapret только под Windows
#endif
    const QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/engine/lists/list-exclude-user.txt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    for (const QString &d : m_bypassSites)
        f.write((d + "\n").toUtf8());
    f.close();
}

// IP самих VPN-серверов → zapret ipset-exclude-user.txt. Иначе winws может
// «десинхронизировать» рукопожатие с VPN-сервером (REALITY/TLS/QUIC) и
// туннель не поднимется, пока zapret включён. Домены резолвим асинхронно.
void VlessController::syncServerIpExclude()
{
#ifndef Q_OS_WIN
    return;   // zapret только под Windows
#endif
    const QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/engine/lists/ipset-exclude-user.txt");
    auto ips = std::make_shared<QSet<QString>>();
    QStringList names;
    for (const Server &s : m_serversData) {
        if (isIpLiteral(s.host))
            ips->insert(s.host);
        else if (!names.contains(s.host))
            names << s.host;
    }
    auto write = [path, ips]() {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        // Пустой ipset winws считает «всё подряд» — держим служебную заглушку.
        f.write("203.0.113.113/32\n");
        for (const QString &ip : *ips)
            f.write((ip + QLatin1Char('\n')).toUtf8());
    };
    write();
    for (const QString &n : names) {
        QHostInfo::lookupHost(n, this, [ips, write](const QHostInfo &hi) {
            bool added = false;
            for (const QHostAddress &a : hi.addresses()) {
                if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                    ips->insert(a.toString());
                    added = true;
                }
            }
            if (added)
                write();
        });
    }
}

// ── Парсинг ссылок ──────────────────────────────────────────────────────
//  vless://uuid@host:port?params#name
//  vmess://base64(json)
//  trojan://password@host:port?params#name
//  ss://base64(method:password)@host:port#name  (и legacy-варианты)
//  hysteria2:// | hy2://auth@host:port?sni=&obfs=&obfs-password=#name
//  tuic://uuid:password@host:port?congestion_control=&alpn=#name
VlessController::Server VlessController::parseUri(const QString &uri) const
{
    Server s;
    const QString t = uri.trimmed();
    s.rawUri = t;

    // Транспорт/TLS из query (vless/trojan) + проверка, что sing-box такое умеет.
    auto fillStream = [](Server &x, const QUrlQuery &q) {
        x.type        = qv(q, "type").toLower();
        x.security    = qv(q, "security").toLower();
        x.sni         = qv(q, "sni");
        if (x.sni.isEmpty()) x.sni = qv(q, "peer");
        x.fingerprint = qv(q, "fp");
        x.publicKey   = qv(q, "pbk");
        x.shortId     = qv(q, "sid");
        x.flow        = qv(q, "flow");
        x.path        = qv(q, "path");
        x.hostHeader  = qv(q, "host");
        x.serviceName = qv(q, "serviceName");
        const QString alpn = qv(q, "alpn");
        if (!alpn.isEmpty())
            x.alpn = alpn.split(QLatin1Char(','), Qt::SkipEmptyParts);
        x.insecure = isTrue(qv(q, "allowInsecure")) || isTrue(qv(q, "insecure"));
        if (qv(q, "headerType") == QLatin1String("http"))
            x.unsupported = QStringLiteral("tcp+http");
    };
    auto checkTransport = [](Server &x) {
        if (x.type.isEmpty() || x.type == QLatin1String("raw"))
            x.type = QStringLiteral("tcp");
        if (x.type == QLatin1String("h2"))
            x.type = QStringLiteral("http");
        static const QStringList ok = {
            QStringLiteral("tcp"), QStringLiteral("ws"), QStringLiteral("grpc"),
            QStringLiteral("httpupgrade"), QStringLiteral("http")};
        if (!ok.contains(x.type) && x.unsupported.isEmpty())
            x.unsupported = x.type;   // xhttp / splithttp / kcp / quic
    };
    auto portOf = [](const QString &p) {
        bool ok = false;
        const int v = p.section(QRegularExpression(QStringLiteral("[,-]")), 0, 0).toInt(&ok);
        return ok && v > 0 && v < 65536 ? v : 443;
    };

    if (t.startsWith(QStringLiteral("vmess://"), Qt::CaseInsensitive)) {
        const QByteArray js = decodeB64(t.mid(8).section(QLatin1Char('#'), 0, 0));
        const QJsonObject o = QJsonDocument::fromJson(js).object();
        if (!o.isEmpty()) {
            auto str = [&o](const char *k) {
                const QJsonValue v = o.value(QLatin1String(k));
                return (v.isDouble() ? QString::number(v.toInt()) : v.toString()).trimmed();
            };
            s.proto = QStringLiteral("vmess");
            s.host = str("add");
            s.port = portOf(str("port"));
            s.uuid = str("id");
            s.alterId = str("aid").toInt();
            s.vmessSecurity = str("scy");
            s.type = str("net").toLower();
            if (str("type") == QLatin1String("http"))
                s.unsupported = QStringLiteral("tcp+http");
            s.hostHeader = str("host");
            s.path = str("path");
            if (s.type == QLatin1String("grpc"))
                s.serviceName = s.path;
            s.security = str("tls").toLower();
            s.sni = str("sni");
            s.fingerprint = str("fp");
            const QString alpn = str("alpn");
            if (!alpn.isEmpty())
                s.alpn = alpn.split(QLatin1Char(','), Qt::SkipEmptyParts);
            s.name = str("ps");
            checkTransport(s);
            if (s.name.isEmpty()) s.name = s.host;
            s.valid = !s.uuid.isEmpty() && !s.host.isEmpty();
            return s;
        }
        // Не base64-JSON → формат как у vless (uuid@host:port?...).
    }

    UriParts p;
    if (t.startsWith(QStringLiteral("ss://"), Qt::CaseInsensitive)) {
        // Legacy: всё «method:password@host:port» закодировано в base64.
        QString body = t.mid(5);
        QString tail;
        const int cut = body.indexOf(QRegularExpression(QStringLiteral("[?#]")));
        if (cut >= 0) { tail = body.mid(cut); body = body.left(cut); }
        if (body.endsWith(QLatin1Char('/'))) body.chop(1);
        if (!body.contains(QLatin1Char('@')))
            body = QString::fromUtf8(decodeB64(body));
        p = splitUri(QStringLiteral("ss://") + body + tail);
    } else {
        p = splitUri(t);
    }
    if (!p.ok)
        return s;
    const QUrlQuery q(p.query);
    s.host = p.host;
    s.port = portOf(p.port);
    s.name = p.fragment;

    if (p.scheme == QLatin1String("vless") || p.scheme == QLatin1String("vmess")) {
        s.proto = p.scheme;
        s.uuid = p.user;
        fillStream(s, q);
        if (s.security.isEmpty()) s.security = QStringLiteral("none");
        if (p.scheme == QLatin1String("vmess"))
            s.vmessSecurity = qv(q, "encryption");
        checkTransport(s);
        s.valid = !s.uuid.isEmpty();
    } else if (p.scheme == QLatin1String("trojan")) {
        s.proto = QStringLiteral("trojan");
        s.password = p.user;
        fillStream(s, q);
        if (s.security.isEmpty()) s.security = QStringLiteral("tls");
        checkTransport(s);
        s.valid = !s.password.isEmpty();
    } else if (p.scheme == QLatin1String("ss")) {
        s.proto = QStringLiteral("shadowsocks");
        QString cred = p.user;
        if (!cred.contains(QLatin1Char(':')))
            cred = QString::fromUtf8(decodeB64(cred));
        s.method = cred.section(QLatin1Char(':'), 0, 0);
        s.password = cred.section(QLatin1Char(':'), 1);
        if (!qv(q, "plugin").isEmpty())
            s.unsupported = QStringLiteral("ss-plugin");
        s.valid = !s.method.isEmpty() && !s.password.isEmpty();
    } else if (p.scheme == QLatin1String("hysteria2") || p.scheme == QLatin1String("hy2")) {
        s.proto = QStringLiteral("hysteria2");
        s.password = p.user;
        // Port hopping: "443,20000-30000" / mport=20000-30000.
        QString ports = p.port;
        if (!qv(q, "mport").isEmpty())
            ports += QLatin1Char(',') + qv(q, "mport");
        const QStringList parts = ports.split(QLatin1Char(','), Qt::SkipEmptyParts);
        QStringList ranges;
        for (const QString &x : parts)
            if (x.contains(QLatin1Char('-')))
                ranges << x;
        s.portRange = ranges.join(QLatin1Char(','));
        s.sni = qv(q, "sni");
        s.insecure = isTrue(qv(q, "insecure")) || isTrue(qv(q, "allowInsecure"));
        s.obfs = qv(q, "obfs");
        s.obfsPassword = qv(q, "obfs-password");
        const QString alpn = qv(q, "alpn");
        if (!alpn.isEmpty())
            s.alpn = alpn.split(QLatin1Char(','), Qt::SkipEmptyParts);
        s.valid = !s.password.isEmpty();
    } else if (p.scheme == QLatin1String("tuic")) {
        s.proto = QStringLiteral("tuic");
        s.uuid = p.user.section(QLatin1Char(':'), 0, 0);
        s.password = p.user.section(QLatin1Char(':'), 1);
        s.sni = qv(q, "sni");
        s.insecure = isTrue(qv(q, "allow_insecure")) || isTrue(qv(q, "insecure"));
        s.congestion = qv(q, "congestion_control");
        s.udpRelayMode = qv(q, "udp_relay_mode");
        const QString alpn = qv(q, "alpn");
        s.alpn = alpn.isEmpty() ? QStringList{QStringLiteral("h3")}
                                : alpn.split(QLatin1Char(','), Qt::SkipEmptyParts);
        s.valid = !s.uuid.isEmpty();
    } else {
        return s;   // socks/http/wireguard и прочее — не поддерживаем
    }

    if (s.name.isEmpty())
        s.name = s.host;
    return s;
}

void VlessController::addKey(const QString &uri)
{
    const Server s = parseUri(uri);
    if (!s.valid) {
        emit errorOccurred(QStringLiteral(
            "Не удалось распознать ключ (поддерживаются vless, vmess, trojan, ss, hysteria2, tuic)"));
        return;
    }
    for (const Server &x : m_serversData) {
        if (x.rawUri == s.rawUri) {
            setStatus(QStringLiteral("Такой ключ уже есть: %1").arg(s.name));
            return;
        }
    }
    m_serversData.append(s);
    saveKeys();
    rebuildServerList();
    if (!s.unsupported.isEmpty())
        emit errorOccurred(QStringLiteral("Ключ добавлен, но транспорт «%1» не поддерживается")
                           .arg(s.unsupported));
    setStatus(QStringLiteral("Ключ добавлен: %1").arg(s.name));
}

void VlessController::reloadFromStore()
{
    if (!m_store) return;
    // Полная перезагрузка: пересобираем m_serversData с нуля по тому что
    // лежит в Store. Текущий currentServer стараемся сохранить.
    const QString currentRaw = (m_currentServer >= 0 && m_currentServer < m_serversData.size())
                               ? m_serversData[m_currentServer].rawUri : QString();
    m_serversData.clear();
    for (const QString &uri : m_store->keys()) {
        const Server s = parseUri(uri);
        if (s.valid) m_serversData.append(s);
    }
    if (!currentRaw.isEmpty()) {
        for (int i = 0; i < m_serversData.size(); ++i) {
            if (m_serversData[i].rawUri == currentRaw) {
                m_currentServer = i;
                emit currentServerChanged();
                break;
            }
        }
    }
    rebuildServerList();
}

// ── Подписка ────────────────────────────────────────────────────────────
void VlessController::loadSubscription(const QString &url)
{
    // happ://add/<real-url> → достаём настоящий URL.
    QString real = url.trimmed();
    const QString happPrefix = QStringLiteral("happ://add/");
    if (real.startsWith(happPrefix))
        real = real.mid(happPrefix.length());

    if (!real.startsWith(QStringLiteral("http"))) {
        emit errorOccurred(QStringLiteral("Неверная ссылка подписки"));
        return;
    }
    fetchSubscription(real, false);
}

void VlessController::refreshSubscription()
{
    if (!m_subUrl.isEmpty())
        fetchSubscription(m_subUrl, true);
}

void VlessController::fetchSubscription(const QString &url, bool silent)
{
    if (!silent)
        setStatus(QStringLiteral("Загрузка подписки…"));

    QNetworkRequest req((QUrl(url)));
    // UA клиента v2rayN: панели (Remnawave/Marzban/3x-ui) отдают на него
    // обычный base64-список ссылок, а не JSON/YAML под конкретный клиент.
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("v2rayN/7.14.4"));
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, silent]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(QStringLiteral("Ошибка загрузки подписки: %1")
                               .arg(reply->errorString()));
            if (!silent)
                setStatus(QStringLiteral("Подписка не загрузилась"));
            return;
        }
        applySubscriptionData(reply->readAll(), url, silent);
    });
}

void VlessController::applySubscriptionData(const QByteArray &data, const QString &url, bool silent)
{
    // Подписки часто закодированы base64 целиком.
    QString text = QString::fromUtf8(data).trimmed();
    if (!text.contains(QStringLiteral("://")))
        text = QString::fromUtf8(decodeB64(text));

    QList<Server> fresh;
    QStringList freshRaw;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const Server s = parseUri(line);
        if (s.valid && !freshRaw.contains(s.rawUri)) {
            fresh.append(s);
            freshRaw << s.rawUri;
        }
    }
    if (fresh.isEmpty()) {
        emit errorOccurred(QStringLiteral("В подписке не найдено ключей"));
        if (!silent)
            setStatus(QStringLiteral("В подписке нет ключей"));
        return;
    }

    // Серверы прошлой подписки заменяем новыми; выбранный стараемся сохранить.
    const QString currentRaw = (m_currentServer >= 0 && m_currentServer < m_serversData.size())
                               ? m_serversData[m_currentServer].rawUri : QString();
    for (int i = m_serversData.size() - 1; i >= 0; --i)
        if (m_subKeys.contains(m_serversData[i].rawUri) || freshRaw.contains(m_serversData[i].rawUri))
            m_serversData.removeAt(i);
    m_serversData.append(fresh);
    m_subKeys = freshRaw;

    const bool urlChanged = m_subUrl != url;
    m_subUrl = url;
    if (m_store) {
        m_store->setSetting(QStringLiteral("subUrl"), m_subUrl);
        m_store->setSetting(QStringLiteral("subKeys"), m_subKeys);
    }
    if (urlChanged)
        emit subscriptionUrlChanged();
    saveKeys();

    int idx = 0;
    for (int i = 0; i < m_serversData.size(); ++i)
        if (m_serversData[i].rawUri == currentRaw) { idx = i; break; }
    if (idx != m_currentServer) {
        m_currentServer = idx;
        emit currentServerChanged();
    }

    int unsupported = 0;
    QStringList kinds;
    for (const Server &s : fresh) {
        if (!s.unsupported.isEmpty()) {
            ++unsupported;
            if (!kinds.contains(s.unsupported))
                kinds << s.unsupported;
        }
    }
    // Свежая подписка из нескольких серверов → по умолчанию «Авто».
    if (!silent && fresh.size() - unsupported >= 2 && !m_autoSelect) {
        m_autoSelect = true;
        if (m_store) m_store->setSetting(QStringLiteral("autoSelect"), true);
        emit autoSelectChanged();
    }
    rebuildServerList();

    if (!silent) {
        QString msg = QStringLiteral("Подписка: серверов %1").arg(fresh.size());
        if (unsupported > 0)
            msg += QStringLiteral(" (не поддерживается %1: %2)")
                       .arg(unsupported).arg(kinds.join(QStringLiteral(", ")));
        setStatus(msg);
        pingAll();
    }
}

void VlessController::rebuildServerList()
{
    m_servers.clear();
    for (const Server &s : m_serversData) {
        QVariantMap m;
        m[QStringLiteral("name")] = s.name;
        m[QStringLiteral("host")] = s.host;
        m[QStringLiteral("port")] = s.port;
        m[QStringLiteral("proto")] = s.proto;
        m[QStringLiteral("unsupported")] = s.unsupported;
        m[QStringLiteral("uri")] = s.rawUri;   // для "поделиться"/копирования
        m_servers.append(m);
    }
    emit serversChanged();
    if (m_currentServer >= m_serversData.size()) {
        m_currentServer = 0;
        emit currentServerChanged();
    }
    syncServerIpExclude();
}

int VlessController::supportedCount() const
{
    int n = 0;
    for (const Server &s : m_serversData)
        if (s.unsupported.isEmpty()) ++n;
    return n;
}

bool VlessController::useAutoGroup() const
{
    return m_autoSelect && supportedCount() >= 2;
}

QList<VlessController::Server> VlessController::serversForConnect() const
{
    QList<Server> list;
    if (useAutoGroup()) {
        QList<Server> ru;
        for (const Server &s : m_serversData) {
            if (!s.unsupported.isEmpty()) continue;
            (isRuExit(s.name) ? ru : list).append(s);
        }
        // Российские выходы — только если других серверов нет вообще.
        return list.isEmpty() ? ru : list;
    }
    if (m_autoSelect) {
        // «Авто», но живой сервер ровно один — берём его.
        for (const Server &s : m_serversData)
            if (s.unsupported.isEmpty()) return {s};
        return list;
    }
    if (m_currentServer >= 0 && m_currentServer < m_serversData.size()
        && m_serversData[m_currentServer].unsupported.isEmpty())
        list.append(m_serversData[m_currentServer]);
    return list;
}

// ── Генерация конфига sing-box ──────────────────────────────────────────
QJsonObject VlessController::buildOutbound(const Server &s, const QString &tag) const
{
    QJsonObject ob;
    ob["type"] = s.proto;
    ob["tag"] = tag;
    ob["server"] = s.host;
    ob["server_port"] = s.port;

    auto alpnArr = [&s]() {
        QJsonArray a;
        for (const QString &x : s.alpn) a.append(x);
        return a;
    };

    // TLS для vless/vmess/trojan (security=none → без TLS).
    auto streamTls = [&]() {
        if (s.security.isEmpty() || s.security == QLatin1String("none"))
            return;
        QJsonObject tls;
        tls["enabled"] = true;
        // server_name: sni, иначе host-заголовок (для ws через CF — это домен CF).
        tls["server_name"] = !s.sni.isEmpty() ? s.sni
                           : (!s.hostHeader.isEmpty() ? s.hostHeader : s.host);
        if (s.insecure) tls["insecure"] = true;
        if (!s.alpn.isEmpty()) tls["alpn"] = alpnArr();
        // REALITY без uTLS не работает; для обычного TLS — только если задан fp.
        const QString fp = !s.fingerprint.isEmpty() ? s.fingerprint
                         : (s.security == QLatin1String("reality") ? QStringLiteral("chrome") : QString());
        if (!fp.isEmpty())
            tls["utls"] = QJsonObject{{"enabled", true}, {"fingerprint", fp}};
        if (s.security == QLatin1String("reality")) {
            tls["reality"] = QJsonObject{
                {"enabled", true}, {"public_key", s.publicKey}, {"short_id", s.shortId}};
        }
        ob["tls"] = tls;
    };

    auto transport = [&]() {
        QJsonObject tr;
        if (s.type == QLatin1String("grpc")) {
            tr["type"] = "grpc";
            if (!s.serviceName.isEmpty()) tr["service_name"] = s.serviceName;
        } else if (s.type == QLatin1String("ws")) {
            tr["type"] = "ws";
            QString path = s.path;
            // "/path?ed=2048" — early data в Sec-WebSocket-Protocol (как в xray).
            const QRegularExpressionMatch m =
                QRegularExpression(QStringLiteral("[?&]ed=(\\d+)")).match(path);
            if (m.hasMatch()) {
                tr["max_early_data"] = m.captured(1).toInt();
                tr["early_data_header_name"] = "Sec-WebSocket-Protocol";
                path.remove(m.capturedStart(), m.capturedLength());
            }
            if (!path.isEmpty()) tr["path"] = path;
            // Host-заголовок (важно для Cloudflare-роутинга).
            tr["headers"] = QJsonObject{{"Host", !s.hostHeader.isEmpty() ? s.hostHeader : s.host}};
        } else if (s.type == QLatin1String("httpupgrade")) {
            tr["type"] = "httpupgrade";
            if (!s.path.isEmpty()) tr["path"] = s.path;
            if (!s.hostHeader.isEmpty()) tr["host"] = s.hostHeader;
        } else if (s.type == QLatin1String("http")) {
            tr["type"] = "http";
            if (!s.path.isEmpty()) tr["path"] = s.path;
            if (!s.hostHeader.isEmpty())
                tr["host"] = QJsonArray{s.hostHeader};
        } else {
            return;   // tcp — без transport
        }
        ob["transport"] = tr;
    };

    if (s.proto == QLatin1String("vless")) {
        ob["uuid"] = s.uuid;
        // Vision работает только поверх голого TCP.
        if (!s.flow.isEmpty() && s.type == QLatin1String("tcp"))
            ob["flow"] = s.flow;
        ob["packet_encoding"] = "xudp";
        streamTls();
        transport();
    } else if (s.proto == QLatin1String("vmess")) {
        ob["uuid"] = s.uuid;
        ob["security"] = s.vmessSecurity.isEmpty() || s.vmessSecurity == QLatin1String("none")
                         ? QStringLiteral("auto") : s.vmessSecurity;
        ob["alter_id"] = s.alterId;
        ob["packet_encoding"] = "xudp";
        streamTls();
        transport();
    } else if (s.proto == QLatin1String("trojan")) {
        ob["password"] = s.password;
        streamTls();
        transport();
    } else if (s.proto == QLatin1String("shadowsocks")) {
        ob["method"] = s.method;
        ob["password"] = s.password;
    } else if (s.proto == QLatin1String("hysteria2")) {
        ob["password"] = s.password;
        if (!s.portRange.isEmpty()) {
            QJsonArray ports;
            for (const QString &r : s.portRange.split(QLatin1Char(',')))
                ports.append(QString(r).replace(QLatin1Char('-'), QLatin1Char(':')));
            ob["server_ports"] = ports;
        }
        if (!s.obfs.isEmpty())
            ob["obfs"] = QJsonObject{{"type", s.obfs}, {"password", s.obfsPassword}};
        QJsonObject tls{{"enabled", true},
                        {"server_name", s.sni.isEmpty() ? s.host : s.sni}};
        if (s.insecure) tls["insecure"] = true;
        if (!s.alpn.isEmpty()) tls["alpn"] = alpnArr();
        ob["tls"] = tls;
    } else if (s.proto == QLatin1String("tuic")) {
        ob["uuid"] = s.uuid;
        ob["password"] = s.password;
        if (!s.congestion.isEmpty()) ob["congestion_control"] = s.congestion;
        if (!s.udpRelayMode.isEmpty()) ob["udp_relay_mode"] = s.udpRelayMode;
        QJsonObject tls{{"enabled", true},
                        {"server_name", s.sni.isEmpty() ? s.host : s.sni},
                        {"alpn", alpnArr()}};
        if (s.insecure) tls["insecure"] = true;
        ob["tls"] = tls;
    }
    return ob;
}

QString VlessController::buildConfig(const QList<Server> &list) const
{
    // Outbounds: один сервер → "proxy"; несколько → s0..sN + urltest "proxy".
    QJsonArray outbounds;
    if (list.size() == 1) {
        outbounds.append(buildOutbound(list.first(), QStringLiteral("proxy")));
    } else {
        QJsonArray tags;
        for (int i = 0; i < list.size(); ++i) {
            const QString tag = QStringLiteral("s%1").arg(i);
            outbounds.append(buildOutbound(list[i], tag));
            tags.append(tag);
        }
        outbounds.append(QJsonObject{
            {"type", "urltest"}, {"tag", "proxy"}, {"outbounds", tags},
            {"url", QLatin1String(kTestUrl)}, {"interval", "3m"},
            {"tolerance", 80}, {"idle_timeout", "30m"}});
    }
    outbounds.append(QJsonObject{{"type","direct"},{"tag","direct"}});

    // Inbound TUN — заворачивает системный трафик.
    // strict_route=false: мягче к другим VPN на машине (Hiddify), меньше
    // риск намертво порвать инет. mtu стандартный.
    QJsonObject tun;
    tun["type"] = "tun";
    tun["tag"] = "tun-in";
#ifdef Q_OS_WIN
    tun["interface_name"] = "iamvpn0";   // на macOS имя только utunN — sing-box выберет сам
#endif
    tun["address"] = QJsonArray{ "172.19.0.1/30" };
    tun["mtu"] = 9000;
    tun["auto_route"] = true;
    tun["strict_route"] = false;   // не воюем жёстко за маршрут
    tun["stack"] = "system";

    // Inbound MIXED (SOCKS5 + HTTP на одном порту) — для CLI/Node/Python
    // приложений которые обходят TUN и требуют явный HTTPS_PROXY.
    // Локально на 127.0.0.1:12335 — не смотрит наружу (12334 занят Hiddify).
    QJsonObject mixed;
    mixed["type"] = "mixed";
    mixed["tag"] = "mixed-in";
    mixed["listen"] = "127.0.0.1";
    mixed["listen_port"] = kProxyPort;

    // DNS (формат 1.12). Без detour — иначе sing-box ругается
    // "detour to empty direct outbound". remote=DoH, local=обычный UDP.
    QJsonObject dns;
    QJsonArray dnsServers;
    dnsServers.append(QJsonObject{
        {"tag","remote"},{"type","https"},{"server","1.1.1.1"}});
    dnsServers.append(QJsonObject{
        {"tag","local"},{"type","udp"},{"server","77.88.8.8"}});
    dns["servers"] = dnsServers;
    dns["strategy"] = "prefer_ipv4";
    // .ru домены резолвим локально (если включён режим .ru мимо VPN).
    QJsonArray dnsRules;
    if (m_routeRuDirect) {
        dnsRules.append(QJsonObject{{"domain_suffix",".ru"},{"server","local"}});
        dnsRules.append(QJsonObject{{"rule_set","geosite-ru"},{"server","local"}});
    }
    dns["rules"] = dnsRules;
    dns["final"] = "remote";

    // Маршрутизация (новый формат: action вместо спец-outbound).
    QJsonObject route;
    QJsonArray rules;
    // Перехват DNS-запросов.
    rules.append(QJsonObject{{"action","sniff"}});
    rules.append(QJsonObject{{"protocol","dns"},{"action","hijack-dns"}});
    // КРИТИЧНО: трафик к самим VPN-серверам — direct (мимо туннеля),
    // иначе соединение sing-box зацикливается в своём же TUN и инет умирает.
    {
        QJsonArray cidrs;
        for (const Server &s : list) {
            QHostAddress a;
            if (a.setAddress(s.host)) {
                const QString c = s.host + (a.protocol() == QAbstractSocket::IPv6Protocol
                                            ? QStringLiteral("/128") : QStringLiteral("/32"));
                if (!cidrs.contains(c)) cidrs.append(c);
            }
        }
        if (!cidrs.isEmpty())
            rules.append(QJsonObject{
                {"ip_cidr", cidrs}, {"action","route"}, {"outbound","direct"}});
    }
    // Сайты-исключения пользователя — direct (мимо VPN). Эти же домены
    // приложение исключает и из zapret (см. ZapretController).
    if (!m_bypassSites.isEmpty()) {
        QJsonArray bypassDomains;
        for (const QString &d : m_bypassSites)
            bypassDomains.append(d);
        rules.append(QJsonObject{
            {"domain_suffix", bypassDomains},
            {"action","route"},{"outbound","direct"}});
    }
    // .ru домены и российские IP — direct (мимо VPN). Только если включено.
    if (m_routeRuDirect) {
        rules.append(QJsonObject{{"domain_suffix",".ru"},{"action","route"},{"outbound","direct"}});
        rules.append(QJsonObject{
            {"rule_set", QJsonArray{"geoip-ru","geosite-ru"}},
            {"action","route"},{"outbound","direct"}});
    }
    route["rules"] = rules;

    // Гео-наборы РФ.
    QJsonArray ruleSet;
    ruleSet.append(QJsonObject{
        {"type","remote"},{"tag","geoip-ru"},{"format","binary"},
        {"url","https://raw.githubusercontent.com/hiddify/hiddify-geo/rule-set/country/geoip-ru.srs"},
        {"download_detour","direct"}});
    ruleSet.append(QJsonObject{
        {"type","remote"},{"tag","geosite-ru"},{"format","binary"},
        {"url","https://raw.githubusercontent.com/hiddify/hiddify-geo/rule-set/country/geosite-ru.srs"},
        {"download_detour","direct"}});
    route["rule_set"] = ruleSet;
    route["final"] = "proxy";
    route["auto_detect_interface"] = true;
    // Резолвер по умолчанию для исходящих соединений (убирает warn 1.12).
    route["default_domain_resolver"] = QJsonObject{{"server","local"}};

    // clash-api на 9090 — отсюда StatsTracker берёт трафик, а мы — delay-тесты.
    QJsonObject experimental;
    experimental["clash_api"] = QJsonObject{
        {"external_controller","127.0.0.1:9090"}
    };

    QJsonObject root;
    root["log"] = QJsonObject{{"level","warn"}};
    root["dns"] = dns;
    root["inbounds"] = QJsonArray{ tun, mixed };
    root["outbounds"] = outbounds;
    root["route"] = route;
    root["experimental"] = experimental;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

// ── Подключение ─────────────────────────────────────────────────────────
void VlessController::connectVpn()
{
    if (m_connected || m_connecting)
        return;
    if (m_serversData.isEmpty()) {
        emit errorOccurred(QStringLiteral("Сначала добавьте ключ или подписку"));
        return;
    }
    // Проверка прав администратора. Без неё sing-box падает с
    // "configure tun interface: Access is denied" — а в UI это выглядит
    // как "не запустился (проверьте ключ)", что путает пользователя.
    if (!AdminCheck::isElevated()) {
        const QString msg = QStringLiteral(
            "Нет прав администратора — VPN не запустится. "
            "Закройте приложение и запустите от имени администратора "
            "(правой кнопкой по ярлыку → «Запуск от имени администратора»).");
        setStatus(msg);
        emit errorOccurred(msg);
        return;
    }

    const QList<Server> list = serversForConnect();
    if (list.isEmpty()) {
        QString msg = QStringLiteral("Нет серверов, которые поддерживает приложение");
        if (!m_autoSelect && m_currentServer < m_serversData.size())
            msg = QStringLiteral("Сервер «%1» использует транспорт «%2» — он не поддерживается. "
                                 "Выберите другой сервер или «Авто».")
                      .arg(m_serversData[m_currentServer].name,
                           m_serversData[m_currentServer].unsupported);
        setStatus(msg);
        emit errorOccurred(msg);
        return;
    }

    // Пользователь нажал «Включить» — теперь keep-alive обязан держать
    // соединение, пока пользователь сам не нажмёт «Выключить».
    m_userWantsConnected = true;
    m_consecFailures = 0;
    m_connectedSet = list;
    setActiveName(QString());
    setConnecting(true);
    setStatus(list.size() > 1
              ? QStringLiteral("Подключение · Авто (серверов: %1)…").arg(list.size())
              : QStringLiteral("Подключение к %1…").arg(list.first().name));

    // Пишем конфиг.
    const QString cfg = buildConfig(list);
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setConnecting(false);
        emit errorOccurred(QStringLiteral("Не удалось записать конфиг"));
        return;
    }
    f.write(cfg.toUtf8());
    f.close();

    const int run = ++m_runId;
    startEngine([this, run](bool ok, const QString &err) {
        if (run != m_runId)
            return;
        if (!ok) {
            m_userWantsConnected = false;
            setConnecting(false);
            setStatus(QStringLiteral("Не удалось подключиться"));
            emit errorOccurred(err);
            return;
        }
        QTimer::singleShot(2500, this, [this, run]() {
            if (run == m_runId)
                verifyAfterStart(0);
        });
    });
}

// ── Движок sing-box: запуск/остановка по платформам ─────────────────────
#ifdef Q_OS_WIN
void VlessController::startEngine(std::function<void(bool, const QString &)> ready)
{
    // sing-box в TUN-режиме требует администратора. Приложение уже от админа.
    // ВАЖНО: пишем stdout/stderr sing-box в sing-box.log / sing-box.err —
    // диагностика читает их.
    const QString sb      = singBoxPath();
    const QString cfgPath = configPath();
    const QString workDir = dataDir();
    const QString logPath = workDir + QStringLiteral("/sing-box.log");
    const QString errLog  = workDir + QStringLiteral("/sing-box.err");

    // Запускаем напрямую через QProcess — он сам корректно квотит аргументы
    // с пробелами (а в "Program Files\..." пробелы есть).
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(1500);
        m_proc->deleteLater();
    }
    m_proc = new QProcess(this);
    m_proc->setProgram(sb);
    m_proc->setArguments({QStringLiteral("run"),
                          QStringLiteral("-c"),
                          QDir::toNativeSeparators(cfgPath)});
    m_proc->setWorkingDirectory(QDir::toNativeSeparators(workDir));
    m_proc->setStandardOutputFile(logPath, QIODevice::Truncate);
    m_proc->setStandardErrorFile(errLog,  QIODevice::Truncate);
    // Скрыть консольное окно (CREATE_NO_WINDOW).
    m_proc->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *a) {
            a->flags |= CREATE_NO_WINDOW;
        });
    m_proc->start();

    ready(true, QString());
}

void VlessController::stopEngine()
{
    if (m_proc)
        m_proc->kill();
    // Приложение уже от админа — глушим напрямую (и возможные «сироты»).
    QProcess::startDetached(QStringLiteral("taskkill"),
        {QStringLiteral("/F"), QStringLiteral("/IM"),
         QStringLiteral("sing-box.exe")});
}

bool VlessController::engineRunning() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}
#else
namespace {
// Жив ли процесс по pid-файлу. Процесс root-овый: kill(pid, 0) от
// пользователя вернёт EPERM — это тоже «жив».
bool pidFileAlive(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const int pid = QString::fromLatin1(f.readAll()).trimmed().toInt();
    if (pid <= 0)
        return false;
    return ::kill(pid, 0) == 0 || errno == EPERM;
}

void touchFile(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
}

// Строка в одинарных кавычках для /bin/sh.
QString shQuote(QString s)
{
    s.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + s + QLatin1Char('\'');
}
} // namespace

bool VlessController::macHelperAlive() const
{
    return pidFileAlive(dataDir() + QStringLiteral("/helper.pid"));
}

void VlessController::startEngine(std::function<void(bool, const QString &)> ready)
{
    const QString dir = dataDir();
    // restart + run: помощник прибьёт старый sing-box (если был) и поднимет
    // заново уже с новым config.json.
    touchFile(dir + QStringLiteral("/sb.restart"));
    touchFile(dir + QStringLiteral("/sb.run"));
    if (macHelperAlive()) {
        ready(true, QString());
        return;
    }
    if (m_authProc)
        return;   // окно с паролем уже открыто

    QFile::remove(dir + QStringLiteral("/sb.pid"));
    QFile::remove(dir + QStringLiteral("/helper.pid"));
    const QString helper = QDir::cleanPath(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/../Resources/iamvpn-helper.sh"));
    const QString cmd = QStringLiteral("/bin/sh %1 %2 %3 %4 >/dev/null 2>&1 &")
        .arg(shQuote(helper), shQuote(singBoxPath()), shQuote(dir),
             QString::number(QCoreApplication::applicationPid()));
    // Внутри строки AppleScript экранируем \ и ".
    QString as = cmd;
    as.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
      .replace(QLatin1Char('"'), QStringLiteral("\\\""));
    const QString script = QStringLiteral(
        "do shell script \"%1\" with administrator privileges "
        "with prompt \"IAMVPN нужен пароль администратора, чтобы создать VPN-туннель.\"").arg(as);

    m_authProc = new QProcess(this);
    connect(m_authProc, &QProcess::finished, this,
            [this, ready, dir](int code, QProcess::ExitStatus st) {
        m_authProc->deleteLater();
        m_authProc = nullptr;
        if (st != QProcess::NormalExit || code != 0) {
            QFile::remove(dir + QStringLiteral("/sb.run"));
            ready(false, QStringLiteral(
                "Без пароля администратора macOS не даст создать VPN-туннель."));
            return;
        }
        ready(true, QString());
    });
    m_authProc->start(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), script});
}

void VlessController::stopEngine()
{
    QFile::remove(dataDir() + QStringLiteral("/sb.run"));
}

bool VlessController::engineRunning() const
{
    return pidFileAlive(dataDir() + QStringLiteral("/sb.pid"));
}
#endif

// Delay-тест через clash API: реальный HTTP-запрос сквозь outbound "proxy".
// groupTest=true → сначала /group/proxy/delay: urltest перемеряет ВСЕ
// серверы и переключится на живой. Ответ группы не показателен (пока идёт
// её собственная проверка, sing-box отдаёт {}), поэтому решение всегда
// по delay самого "proxy".
void VlessController::probeTunnel(bool groupTest, std::function<void(bool, int)> cb)
{
    auto request = [this](const QString &path, int timeoutMs) {
        QUrl url(QLatin1String(kClashApi) + path);
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("url"), QLatin1String(kTestUrl));
        q.addQueryItem(QStringLiteral("timeout"), QStringLiteral("6000"));
        url.setQuery(q);
        QNetworkRequest req(url);
        req.setTransferTimeout(timeoutMs);
        return m_nam->get(req);
    };
    auto proxyDelay = [this, request, cb]() {
        QNetworkReply *reply = request(QStringLiteral("/proxies/proxy/delay"), 9000);
        connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
            reply->deleteLater();
            int delay = 0;
            if (reply->error() == QNetworkReply::NoError)
                delay = QJsonDocument::fromJson(reply->readAll())
                            .object().value(QStringLiteral("delay")).toInt();
            cb(delay > 0, delay);
        });
    };
    if (!groupTest) {
        proxyDelay();
        return;
    }
    QNetworkReply *g = request(QStringLiteral("/group/proxy/delay"), 15000);
    connect(g, &QNetworkReply::finished, this, [g, proxyDelay]() {
        g->deleteLater();
        proxyDelay();
    });
}

// Какой сервер сейчас реально несёт трафик (для «Авто» — выбор urltest).
void VlessController::queryActiveOutbound()
{
    const int run = m_runId;
    auto show = [this](const QString &name) {
        setActiveName(name);
        setStatus(m_connectedSet.size() > 1
                  ? QStringLiteral("Подключено · Авто → %1").arg(name)
                  : QStringLiteral("Подключено · %1").arg(name));
    };
    if (m_connectedSet.size() == 1) {
        show(m_connectedSet.first().name);
        return;
    }
    QNetworkRequest req(QUrl(QLatin1String(kClashApi) + QStringLiteral("/proxies/proxy")));
    req.setTransferTimeout(4000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, run, show]() {
        reply->deleteLater();
        if (run != m_runId)
            return;
        const QString now = QJsonDocument::fromJson(reply->readAll())
                                .object().value(QStringLiteral("now")).toString();
        bool ok = false;
        const int i = now.mid(1).toInt(&ok);
        if (now.startsWith(QLatin1Char('s')) && ok && i >= 0 && i < m_connectedSet.size())
            show(m_connectedSet[i].name);
        else
            show(QStringLiteral("лучший сервер"));
    });
}

// Через пару секунд после старта: жив ли sing-box и ходит ли трафик.
// До 3 попыток; если ничего не ответило — откат, чтобы вернуть инет.
void VlessController::verifyAfterStart(int attempt)
{
    if (!m_connecting)
        return;
    const int run = m_runId;

    if (!engineRunning()) {
        QString reason;
        QFile ef(dataDir() + QStringLiteral("/sing-box.err"));
        if (ef.open(QIODevice::ReadOnly)) {
            const QStringList lines = QString::fromUtf8(ef.readAll())
                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (int i = lines.size() - 1; i >= 0; --i) {
                if (lines[i].contains(QStringLiteral("FATAL"))
                    || lines[i].contains(QStringLiteral("ERROR"))) {
                    reason = lines[i].trimmed();
                    break;
                }
            }
        }
        m_userWantsConnected = false;
        setConnecting(false);
        setConnected(false);
        setStatus(QStringLiteral("Не удалось подключиться"));
        emit errorOccurred(reason.isEmpty()
            ? QStringLiteral("sing-box не запустился (проверьте ключ)")
            : QStringLiteral("sing-box не запустился: %1").arg(reason.right(300)));
        return;
    }

    // Первая попытка в «Авто» — сразу тест всей группы, чтобы urltest
    // не сидел на первом (возможно мёртвом) сервере до своего таймера.
    const bool group = m_connectedSet.size() > 1 && attempt == 0;
    probeTunnel(group, [this, run, attempt](bool ok, int) {
        if (run != m_runId || !m_connecting)
            return;
        if (ok) {
            setConnecting(false);
            setConnected(true);
            m_consecFailures = 0;
            queryActiveOutbound();
            // urltest называет выбранный сервер только когда домерит все
            // (мёртвые — по таймауту), поэтому уточняем имя чуть позже.
            if (m_connectedSet.size() > 1)
                QTimer::singleShot(8000, this, [this, run]() {
                    if (run == m_runId && m_connected)
                        queryActiveOutbound();
                });
            // Прописываем env-переменные для CLI (git, npm и т.д.).
            // MIXED-inbound слушает 127.0.0.1:12335 — там и SOCKS5 и HTTP proxy.
            ProxyEnv::setProxy(
                proxyUrl("socks5"),
                proxyUrl("http"),
                QStringLiteral("User"));
            // С этого момента keep-alive дёргается каждые 20 сек —
            // при обрыве сам поднимет sing-box обратно.
            if (m_userWantsConnected && !m_keepAliveTimer->isActive())
                m_keepAliveTimer->start();
            return;
        }
        if (attempt < 2) {
            QTimer::singleShot(2000, this, [this, run, attempt]() {
                if (run == m_runId)
                    verifyAfterStart(attempt + 1);
            });
            return;
        }
        // Туннель поднялся, но трафик не идёт — откатываемся, спасаем инет.
        const bool many = m_connectedSet.size() > 1;
        const QString name = m_connectedSet.isEmpty() ? QString() : m_connectedSet.first().name;
        disconnectVpn();
        setStatus(QStringLiteral("Сервер не отвечает — откат (инет возвращён)"));
        emit errorOccurred(many
            ? QStringLiteral("Ни один сервер не ответил. Обновите подписку или "
                             "попробуйте выключить обход DPI (zapret) в опциях.")
            : QStringLiteral("Сервер «%1» не пропускает трафик. Выберите другой сервер "
                             "или «Авто», либо выключите обход DPI (zapret).").arg(name));
    });
}

void VlessController::disconnectVpn()
{
    // Пользователь сам нажал «Выключить» — гасим keep-alive, чтобы он
    // не вернул нас обратно через 20 сек.
    m_userWantsConnected = false;
    ++m_runId;
    if (m_keepAliveTimer && m_keepAliveTimer->isActive())
        m_keepAliveTimer->stop();

    stopEngine();
    // Убираем env-переменные — иначе CLI-программы будут стучать в
    // мёртвый 127.0.0.1:12335 и получать "connection refused".
    ProxyEnv::clearProxy(QStringLiteral("User"));
    setActiveName(QString());
    setConnected(false);
    setConnecting(false);
    setStatus(QStringLiteral("Не подключено"));
}

// ─────────────────────────────────────────────────────────────────────────
//  Keep-alive: каждые 20 сек проверяем что туннель реально пропускает
//  трафик (delay-тест через clash API).
//
//  При неудаче:
//    – «Авто»: перетест всей группы — urltest уйдёт на живой сервер;
//    – 2 неудачи подряд: тихо рестартуем sing-box (reconnectInternal).
//
//  m_userWantsConnected = false → keep-alive не работает (юзер сам выключил).
// ─────────────────────────────────────────────────────────────────────────
void VlessController::keepAliveTick()
{
    if (!m_userWantsConnected || !m_connected || m_probeInFlight)
        return;
    const int run = m_runId;

    auto fail = [this]() {
        if (++m_consecFailures < 2)
            return;   // одна неудача может быть флуктуацией сети
        m_consecFailures = 0;
        reconnectInternal();
    };

    if (!engineRunning()) {
        m_consecFailures = 1;
        fail();
        return;
    }

    m_probeInFlight = true;
    probeTunnel(false, [this, run, fail](bool ok, int) {
        m_probeInFlight = false;
        if (run != m_runId)
            return;
        if (ok) {
            m_consecFailures = 0;
            if (m_connectedSet.size() > 1)
                queryActiveOutbound();
            return;
        }
        if (m_connectedSet.size() <= 1) {
            fail();
            return;
        }
        m_probeInFlight = true;
        probeTunnel(true, [this, run, fail](bool ok2, int) {
            m_probeInFlight = false;
            if (run != m_runId)
                return;
            if (ok2) {
                m_consecFailures = 0;
                queryActiveOutbound();
                return;
            }
            fail();
        });
    });
}

// Тихий рестарт sing-box на том же наборе серверов.
void VlessController::reconnectInternal()
{
    setStatus(QStringLiteral("Восстанавливаю соединение…"));
    emit errorOccurred(QStringLiteral("Связь пропала — переподключаюсь"));
    restartTunnel();
}

// Перезапуск туннеля с текущими настройками (смена сервера/режима, обрыв).
void VlessController::restartTunnel()
{
    ++m_runId;
#ifdef Q_OS_WIN
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
    // Аккуратно прибиваем возможный зомби-процесс.
    QProcess kill;
    kill.start(QStringLiteral("taskkill"),
               {QStringLiteral("/F"), QStringLiteral("/IM"),
                QStringLiteral("sing-box.exe")});
    kill.waitForFinished(2000);
#endif
    // macOS: startEngine сам выставит sb.restart — помощник перезапустит.

    setConnected(false);
    setConnecting(false);
    // m_userWantsConnected остаётся true — connectVpn поднимет заново.
    connectVpn();
}

void VlessController::toggle()
{
    if (m_connected || m_connecting)
        disconnectVpn();
    else
        connectVpn();
}

// ── TCP-пинг всех серверов ──────────────────────────────────────────────
// Меряем время TCP-рукопожатия (от момента, когда имя уже разрезолвлено),
// как «TCP ping» в Happ/v2rayN. Для заблокированного IP честно покажет «—».
// hysteria2/tuic работают по UDP — для них пинг не меряем (-4).
void VlessController::pingAll()
{
    if (m_pingPending > 0)
        return;
    QSet<QString> seen;
    for (const Server &s : m_serversData) {
        const QString key = s.host + QLatin1Char(':') + QString::number(s.port);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        if (isUdpProto(s.proto)) {
            m_pings[key] = -4;
            continue;
        }
        m_pings[key] = -3;

        auto *sock = new QTcpSocket(this);
        // Пинг идёт мимо прокси из окружения и мимо системного.
        sock->setProxy(QNetworkProxy::NoProxy);
        auto timer = std::make_shared<QElapsedTimer>();
        auto *deadline = new QTimer(sock);
        deadline->setSingleShot(true);
        ++m_pingPending;

        auto finish = [this, sock, key](int ms) {
            if (sock->property("done").toBool())
                return;
            sock->setProperty("done", true);
            m_pings[key] = ms;
            sock->abort();
            sock->deleteLater();
            --m_pingPending;
            emit pingsChanged();
            if (m_pingPending == 0)
                emit pingingChanged();
        };
        connect(sock, &QTcpSocket::hostFound, this, [timer]() { timer->restart(); });
        connect(sock, &QTcpSocket::connected, this, [timer, finish]() {
            finish(int(qMax<qint64>(1, timer->elapsed())));
        });
        connect(sock, &QTcpSocket::errorOccurred, this, [finish](QAbstractSocket::SocketError) { finish(-2); });
        connect(deadline, &QTimer::timeout, this, [finish]() { finish(-2); });

        timer->start();
        deadline->start(3000);
        sock->connectToHost(s.host, quint16(s.port));
    }
    emit pingsChanged();
    emit pingingChanged();
}
