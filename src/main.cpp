// ─────────────────────────────────────────────────────────────────────────
//  main.cpp — точка входа приложения IAMVPN (white-label сборка AM.SALES VPN).
//
//  Здесь мы:
//    1. Создаём приложение Qt.
//    2. Регистрируем наши C++ классы (ZapretController, NetworkScanner),
//       чтобы интерфейс на QML мог ими пользоваться.
//    3. Загружаем главный QML-файл с интерфейсом.
// ─────────────────────────────────────────────────────────────────────────

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFontDatabase>
#include <QFont>
#include <QIcon>

#include "ZapretController.h"
#include "NetworkScanner.h"
#include "VlessController.h"
#include "Store.h"
#include "UpdateChecker.h"
#include "StatsTracker.h"
#include "TgController.h"
#include "DiagController.h"
#include "LogController.h"
#include "AdminCheck.h"
#include "CursorHelper.h"
#include "TrayMenuHelper.h"

int main(int argc, char *argv[])
{
    // QApplication (не QGuiApplication) — нужно для системного трея
    // (Qt.labs.platform.SystemTrayIcon) на Windows.
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("IAMVPN"));
    QApplication::setApplicationVersion(QStringLiteral("1.1.1"));
    QApplication::setOrganizationName(QStringLiteral("IAMVPN"));
    // Не выходим при закрытии окна — приложение живёт в трее.
    QApplication::setQuitOnLastWindowClosed(false);

    // Иконка приложения (окно + панель задач).
    QApplication::setWindowIcon(QIcon(
        QStringLiteral(":/qt/qml/IAMVPN/assets/appicon.png")));

    // ── Загружаем шрифт Manrope из ресурсов и делаем его шрифтом по
    //    умолчанию для всего приложения (приятная типографика). ──────────
    const int fid = QFontDatabase::addApplicationFont(
        QStringLiteral(":/qt/qml/IAMVPN/assets/fonts/Manrope-Regular.ttf"));
    if (fid != -1) {
        const QStringList fam = QFontDatabase::applicationFontFamilies(fid);
        if (!fam.isEmpty()) {
            QFont f(fam.first());
            f.setStyleStrategy(QFont::PreferAntialias);
            QApplication::setFont(f);
        }
    }

    QQmlApplicationEngine engine;

    // ── Создаём объекты бэкенда ─────────────────────────────────────────
    LogController logs;                 // единый журнал действий → файл
    // Сразу пишем в журнал, запущены ли с правами админа — это самая частая
    // причина "VPN не подключается": без админа sing-box не создаёт TUN.
    logs.log(QStringLiteral("APP"),
             AdminCheck::isElevated() ? QStringLiteral("OK") : QStringLiteral("WARN"),
             AdminCheck::isElevated()
                 ? QStringLiteral("Running as administrator")
                 : QStringLiteral("NOT running as administrator — VPN will fail"
                                  " (sing-box cannot create TUN). Re-run with admin."));
    Store store;                       // JSON-хранилище (ключи, настройки, стата)
    ZapretController zapret;
    NetworkScanner scanner;
    VlessController vless(&store);      // получает доступ к хранилищу
    StatsTracker stats(&store);         // учёт сессий/трафика
    TgController tg(&store);            // встроенный Telegram-прокси
    DiagController diag;                // диагностика «почему не работает»
    // White-label build: no Cloudflare key fetch (talks to AM.SALES infra).
    // Updates come from IAMVPN's own repo (strdr1/IAMVPN), never AM.SALES.
    UpdateChecker updater(&store);
    CursorHelper cursor;                // глобальные позиция/кнопки мыши
    TrayMenuHelper trayMenu;            // нативное Windows-меню трея

    // ── Логируем ключевые события каждого контроллера в LogController ───
    // Подписываемся на сигналы — никаких знаний о LogController в самих
    // контроллерах не нужно, всё связывается здесь.
    QObject::connect(&vless, &VlessController::connectedChanged, &logs, [&]() {
        logs.log(QStringLiteral("VPN"), vless.connected() ? QStringLiteral("OK") : QStringLiteral("INFO"),
                 vless.connected() ? QStringLiteral("Connected") : QStringLiteral("Disconnected"));
    });
    QObject::connect(&zapret, &ZapretController::runningChanged, &logs, [&]() {
        logs.log(QStringLiteral("ZAPRET"), zapret.running() ? QStringLiteral("OK") : QStringLiteral("INFO"),
                 zapret.running()
                     ? QStringLiteral("Started (profile: ") + zapret.selectedProfile() + QStringLiteral(")")
                     : QStringLiteral("Stopped"));
    });
    QObject::connect(&tg, &TgController::runningChanged, &logs, [&]() {
        logs.log(QStringLiteral("TG"), tg.running() ? QStringLiteral("OK") : QStringLiteral("INFO"),
                 tg.running() ? QStringLiteral("Telegram proxy started")
                              : QStringLiteral("Telegram proxy stopped"));
    });
    QObject::connect(&scanner, &NetworkScanner::scanningChanged, &logs, [&]() {
        logs.log(QStringLiteral("SCAN"), QStringLiteral("INFO"),
                 scanner.scanning() ? QStringLiteral("Network scan started")
                                    : QStringLiteral("Network scan finished"));
    });
    QObject::connect(&updater, &UpdateChecker::statusTextChanged, &logs, [&]() {
        logs.log(QStringLiteral("UPDATE"), QStringLiteral("INFO"), updater.statusText());
    });
    QObject::connect(&vless, &VlessController::errorOccurred, &logs,
                     [&](const QString &msg) {
        logs.log(QStringLiteral("VPN"), QStringLiteral("ERROR"), msg);
    });

    // Связываем VPN-подключение со счётчиком статистики: при подключении
    // стартуем сессию, при отключении — завершаем (она пишется в Store).
    QObject::connect(&vless, &VlessController::connectedChanged,
                     &stats, [&]() {
        if (vless.connected())
            stats.sessionStart(QStringLiteral("server"));
        else
            stats.sessionEnd();
    });

    // ── Прокидываем в QML ───────────────────────────────────────────────
    engine.rootContext()->setContextProperty(QStringLiteral("Zapret"), &zapret);
    engine.rootContext()->setContextProperty(QStringLiteral("Scanner"), &scanner);
    engine.rootContext()->setContextProperty(QStringLiteral("Vpn"), &vless);
    engine.rootContext()->setContextProperty(QStringLiteral("Updater"), &updater);
    engine.rootContext()->setContextProperty(QStringLiteral("Store"), &store);
    engine.rootContext()->setContextProperty(QStringLiteral("Stats"), &stats);
    engine.rootContext()->setContextProperty(QStringLiteral("Tg"), &tg);
    engine.rootContext()->setContextProperty(QStringLiteral("Diag"), &diag);
    engine.rootContext()->setContextProperty(QStringLiteral("Logs"), &logs);
    engine.rootContext()->setContextProperty(QStringLiteral("Cursor"), &cursor);
    engine.rootContext()->setContextProperty(QStringLiteral("TrayMenu"), &trayMenu);

    // Связываем нативное меню трея с действиями приложения.
    QObject::connect(&trayMenu, &TrayMenuHelper::vpnToggle, &vless, [&]() {
        if (vless.connected() || vless.connecting()) {
            vless.disconnectVpn();
            zapret.stop();
        } else {
            if (vless.useZapret()) zapret.start();
            vless.connectVpn();
        }
    });
    QObject::connect(&trayMenu, &TrayMenuHelper::openLogs, &logs, &LogController::openLogsFolder);
    QObject::connect(&trayMenu, &TrayMenuHelper::quit, &app, &QCoreApplication::quit);
    // openWindow — обрабатываем в QML (нужен showWindow), там слот висит.

    // ── Загружаем главный интерфейс ─────────────────────────────────────
    // Если QML не загрузится (ошибка синтаксиса) — приложение закроется.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("IAMVPN"), QStringLiteral("Main"));

    return app.exec();
}
