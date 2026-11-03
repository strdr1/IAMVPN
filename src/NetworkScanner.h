#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────
//  NetworkScanner — ищет устройства в локальной сети.
//
//  Как работает (простой и надёжный для прототипа способ):
//    1. Узнаём свой IP и маску (например 192.168.0.135 / 255.255.255.0).
//       Значит наша сеть — 192.168.0.1 .. 192.168.0.254.
//    2. "Будим" сеть: быстро пингуем все адреса, чтобы устройства попали
//       в ARP-таблицу Windows.
//    3. Читаем ARP-таблицу командой `arp -a` — там пары IP ↔ MAC всех,
//       кто недавно отвечал. Это и есть список устройств.
//
//  Результат отдаём в QML как список словарей {ip, mac, vendor}.
//  Сканирование идёт в отдельном потоке, чтобы интерфейс не подвисал.
// ─────────────────────────────────────────────────────────────────────────
class NetworkScanner : public QObject
{
    Q_OBJECT

    // Список найденных устройств. Каждый элемент — QVariantMap {ip, mac}.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

    // true, пока идёт сканирование (чтобы крутить индикатор в UI).
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

    // Подсеть, которую сканируем (для показа в интерфейсе).
    Q_PROPERTY(QString subnet READ subnet NOTIFY subnetChanged)

public:
    explicit NetworkScanner(QObject *parent = nullptr);

    QVariantList devices() const { return m_devices; }
    bool scanning() const { return m_scanning; }
    QString subnet() const { return m_subnet; }

public slots:
    // Запустить сканирование (вызывается из QML по кнопке).
    void scan();

signals:
    void devicesChanged();
    void scanningChanged();
    void subnetChanged();

private:
    void setScanning(bool s);
    void doScan();                 // выполняется в фоновом потоке

    QVariantList m_devices;
    bool m_scanning = false;
    QString m_subnet;
};
