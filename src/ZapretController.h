#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

// ─────────────────────────────────────────────────────────────────────────
//  ZapretController — управляет встроенным движком обхода (winws/zapret).
//
//  Приложение несёт движок внутри себя (папка engine/ рядом с .exe:
//  bin/winws.exe + WinDivert + lists/ + профили general*.bat). Внешний
//  zapret больше не нужен.
//
//  Главная фишка: АВТОПЕРЕБОР профилей. При запуске контроллер по очереди
//  пробует профили (ALT3 первым — он рабочий, затем остальные). После
//  запуска профиля он проверяет, "поднялся" ли winws и проходит ли
//  тестовое соединение. Если профиль не сработал — пробует следующий.
// ─────────────────────────────────────────────────────────────────────────
class ZapretController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    // Имя профиля, который сейчас пробуется/работает (для показа в UI).
    Q_PROPERTY(QString currentProfile READ currentProfile NOTIFY currentProfileChanged)
    // true, пока идёт автоперебор профилей.
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // Список имён доступных профилей (для выпадашки в UI).
    Q_PROPERTY(QStringList profileNames READ profileNames CONSTANT)
    // Имя выбранного вручную профиля; "" = автоподбор.
    Q_PROPERTY(QString selectedProfile READ selectedProfile WRITE setSelectedProfile NOTIFY selectedProfileChanged)

public:
    explicit ZapretController(QObject *parent = nullptr);

    bool running() const { return m_running; }
    QString statusText() const { return m_statusText; }
    QString currentProfile() const { return m_currentProfile; }
    bool searching() const { return m_searching; }
    QStringList profileNames() const { return m_profileNames; }
    QString selectedProfile() const { return m_selectedProfile; }
    void setSelectedProfile(const QString &name);

public slots:
    void start();    // если selectedProfile задан — запустить его; иначе автоперебор
    void stop();      // остановить движок (завершить winws.exe)
    void toggle();    // включить/выключить

signals:
    void runningChanged();
    void statusTextChanged();
    void currentProfileChanged();
    void searchingChanged();
    void selectedProfileChanged();

private slots:
    void tryNextProfile();    // запустить следующий профиль из очереди
    void checkCurrentProfile(); // проверить, сработал ли текущий профиль

private:
    void setRunning(bool r);
    void setStatus(const QString &text);
    void setCurrentProfile(const QString &name);
    void setSearching(bool s);

    bool isWinwsRunning() const;
    bool testConnection() const;   // прошло ли тестовое соединение через обход
    void killWinws();              // принудительно завершить winws
    QString engineDir() const;     // путь к папке engine рядом с .exe

    bool m_running = false;
    bool m_searching = false;
    QString m_statusText = QStringLiteral("Выключено");
    QString m_currentProfile;

    QStringList m_profiles;       // список .bat-профилей в порядке приоритета
    QStringList m_profileNames;   // красивые имена для UI (ALT, ALT2, ...)
    QString m_selectedProfile;    // выбранный вручную; "" = авто
    int m_profileIndex = -1;      // какой профиль сейчас пробуем
    QTimer *m_checkTimer = nullptr;
};
