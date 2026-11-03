#include "NetworkScanner.h"

#include <QProcess>
#include <QNetworkInterface>
#include <QThreadPool>           // для запуска сканирования в фоне
#include <QRegularExpression>
#include <QHostAddress>
#include <QHostInfo>             // для определения имени устройства по IP
#include <QSet>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────
//  Реализация NetworkScanner.
// ─────────────────────────────────────────────────────────────────────────

NetworkScanner::NetworkScanner(QObject *parent)
    : QObject(parent)
{
}

void NetworkScanner::setScanning(bool s)
{
    if (m_scanning == s)
        return;
    m_scanning = s;
    emit scanningChanged();
}

void NetworkScanner::scan()
{
    if (m_scanning)
        return;  // уже идёт — не запускаем второй раз

    setScanning(true);
    m_devices.clear();
    emit devicesChanged();

    // Запускаем тяжёлую работу в отдельном потоке через QThreadPool.
    // Когда поток закончит — он сам обновит m_devices (через invokeMethod,
    // чтобы безопасно вернуться в главный поток UI).
    QThreadPool::globalInstance()->start([this]() { doScan(); });
}

// Это "настоящий" частный адрес локальной сети (а не APIPA/публичный)?
static bool isPrivateLan(const QHostAddress &a)
{
    const quint32 ip = a.toIPv4Address();
    const quint8 b0 = (ip >> 24) & 0xFF;
    const quint8 b1 = (ip >> 16) & 0xFF;
    if (b0 == 10) return true;                            // 10.0.0.0/8
    if (b0 == 192 && b1 == 168) return true;               // 192.168.0.0/16
    if (b0 == 172 && b1 >= 16 && b1 <= 31) return true;    // 172.16.0.0/12
    return false;     // всё прочее (вкл. 169.254 APIPA, multicast) — не LAN
}

// Это виртуальный/служебный адаптер (Hyper-V, VMware, VPN, Bluetooth...)?
// Такие сети сканировать не нужно — там нет реальных устройств.
static bool isVirtualAdapter(const QString &name)
{
    const QString n = name.toLower();
    static const char *bad[] = {
        "virtual", "hyper-v", "vethernet", "vmware", "virtualbox",
        "tailscale", "tap-windows", "tap", "tun", "loopback", "bluetooth",
        "wsl", "docker", "wireguard", "openvpn", "zerotier", "default switch"
    };
    for (const char *b : bad)
        if (n.contains(QLatin1String(b)))
            return true;
    return false;
}

// Эта функция выполняется в ФОНОВОМ потоке — нельзя трогать UI напрямую.
void NetworkScanner::doScan()
{
    // ── Шаг 1: находим ПРАВИЛЬНЫЙ интерфейс локальной сети ──────────────
    // Берём ФИЗИЧЕСКИЙ адаптер (Ethernet/Wi-Fi) с настоящим частным IP.
    // Отсекаем виртуальные адаптеры (Hyper-V 172.30.x, VPN, Bluetooth и т.п.) —
    // именно из-за них раньше скан уходил в пустую виртуальную сеть.
    QString myIp;
    int prefix = 24;

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;

        // Без аппаратного MAC — обычно туннель/VPN, пропускаем.
        if (iface.hardwareAddress().isEmpty())
            continue;

        // Пропускаем виртуальные/служебные адаптеры по их имени.
        if (isVirtualAdapter(iface.humanReadableName()) ||
            isVirtualAdapter(iface.name()))
            continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() == QAbstractSocket::IPv4Protocol &&
                !addr.isLoopback() && isPrivateLan(addr)) {
                myIp = addr.toString();
                prefix = entry.prefixLength();
                break;
            }
        }
        if (!myIp.isEmpty())
            break;
    }

    if (myIp.isEmpty()) {
        QMetaObject::invokeMethod(this, [this]() {
            setScanning(false);
        }, Qt::QueuedConnection);
        return;
    }

    // Берём первые три октета как базу подсети: "192.168.0."
    const QStringList parts = myIp.split('.');
    const QString base = parts[0] + "." + parts[1] + "." + parts[2] + ".";
    // Для отображения показываем /24 (стандартная домашняя сеть).
    const QString subnetText = base + "0/24";

    // Сообщаем UI, какую подсеть сканируем.
    QMetaObject::invokeMethod(this, [this, subnetText]() {
        m_subnet = subnetText;
        emit subnetChanged();
    }, Qt::QueuedConnection);

    // ── Шаг 2: "будим" сеть — пингуем все адреса 1..254 ─────────────────
    // Один короткий пинг (-n 1) с маленьким таймаутом (-w 200 мс).
    // Запускаем пачкой параллельно, чтобы было быстро.
    QList<QProcess*> pings;
    for (int i = 1; i <= 254; ++i) {
        QProcess *p = new QProcess();
        p->start(QStringLiteral("ping"),
                 {QStringLiteral("-n"), QStringLiteral("1"),
                  QStringLiteral("-w"), QStringLiteral("200"),
                  base + QString::number(i)});
        pings.append(p);
    }
    // Ждём, пока все пинги отработают (с запасом по времени).
    for (QProcess *p : pings) {
        p->waitForFinished(1500);
        p->deleteLater();
    }

    // ── Шаг 3: читаем ARP-таблицу — там осели IP+MAC ответивших ─────────
    QProcess arp;
    arp.start(QStringLiteral("arp"), {QStringLiteral("-a")});
    arp.waitForFinished(5000);
    const QString arpOut = QString::fromLocal8Bit(arp.readAllStandardOutput());

    // Парсим строки вида:  192.168.0.1   aa-bb-cc-dd-ee-ff   динамический
    QVariantList found;
    QSet<QString> seen;   // чтобы не было дублей по IP
    QRegularExpression re(
        QStringLiteral("(\\d{1,3}(?:\\.\\d{1,3}){3})\\s+"
                       "([0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})"));

    // Сначала добавляем САМ ПК (его в ARP-таблице может не быть).
    {
        QVariantMap self;
        self[QStringLiteral("ip")] = myIp;
        self[QStringLiteral("mac")] = QStringLiteral("(этот компьютер)");
        self[QStringLiteral("isSelf")] = true;
        found.append(self);
        seen.insert(myIp);
    }

    auto it = re.globalMatch(arpOut);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString ip = m.captured(1);
        const QString mac = m.captured(2).toLower();

        // 1) Только адреса нашей подсети (тот же первый три октета).
        if (!ip.startsWith(base))
            continue;
        // 2) Без сетевого (.0) и broadcast (.255).
        const QString lastOctet = ip.mid(base.length());
        if (lastOctet == QStringLiteral("0") || lastOctet == QStringLiteral("255"))
            continue;
        // 3) Без multicast/broadcast MAC (01-00-5e..., ff-ff-...).
        if (mac.startsWith(QStringLiteral("01-00-5e")) ||
            mac == QStringLiteral("ff-ff-ff-ff-ff-ff"))
            continue;
        // 4) Без дублей.
        if (seen.contains(ip))
            continue;
        seen.insert(ip);

        QVariantMap dev;
        dev[QStringLiteral("ip")] = ip;
        dev[QStringLiteral("mac")] = mac.toUpper();
        dev[QStringLiteral("isSelf")] = false;
        found.append(dev);
    }

    // Сортируем по последнему октету, чтобы список был аккуратным.
    std::sort(found.begin(), found.end(), [](const QVariant &a, const QVariant &b) {
        return QHostAddress(a.toMap().value("ip").toString()).toIPv4Address()
             < QHostAddress(b.toMap().value("ip").toString()).toIPv4Address();
    });

    // ── Шаг 3.5: пытаемся определить ИМЯ устройства по IP ───────────────
    // Способ 1: обратный DNS (QHostInfo). Способ 2 (запасной): NetBIOS через
    // команду nbtstat -A <ip>. Имя есть не у всех устройств — это нормально.
    for (int i = 0; i < found.size(); ++i) {
        QVariantMap dev = found[i].toMap();
        const QString ip = dev.value(QStringLiteral("ip")).toString();
        QString name;

        // -- обратный DNS --
        const QHostInfo hi = QHostInfo::fromName(ip);
        if (hi.error() == QHostInfo::NoError) {
            const QString hn = hi.hostName();
            // Если вернулся не сам IP — значит есть осмысленное имя.
            if (!hn.isEmpty() && hn != ip) {
                name = hn.section('.', 0, 0);  // берём только первую часть
            }
        }

        // -- запасной вариант: NetBIOS-имя через nbtstat --
        if (name.isEmpty()) {
            QProcess nb;
            nb.start(QStringLiteral("nbtstat"),
                     {QStringLiteral("-A"), ip});
            if (nb.waitForFinished(1200)) {
                const QString out = QString::fromLocal8Bit(nb.readAllStandardOutput());
                // Строка с UNIQUE и флагом <00> — это имя компьютера.
                QRegularExpression nbRe(
                    QStringLiteral("\\s*([^\\s]+)\\s+<00>\\s+UNIQUE"));
                const QRegularExpressionMatch m = nbRe.match(out);
                if (m.hasMatch())
                    name = m.captured(1).trimmed();
            }
        }

        dev[QStringLiteral("name")] = name;  // пусто, если имя не определилось
        found[i] = dev;
    }

    // ── Шаг 4: возвращаем результат в главный поток для обновления UI ───
    QMetaObject::invokeMethod(this, [this, found]() {
        m_devices = found;
        emit devicesChanged();
        setScanning(false);
    }, Qt::QueuedConnection);
}
