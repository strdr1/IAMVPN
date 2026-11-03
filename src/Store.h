#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QJsonObject>

// ─────────────────────────────────────────────────────────────────────────
//  Store — простое JSON-хранилище (мини-БД) приложения.
//
//  Файл лежит в %APPDATA%/AM.SALES/store.json и хранит:
//    - keys:      список добавленных vless:// ключей (строки)
//    - settings:  настройки (routeRuDirect, adminPasswordHash, serverUrl…)
//    - sessions:  история сессий статистики [{user, start, durationSec,
//                 upBytes, downBytes, server}]
//
//  Класс — синглтон-обёртка: создаётся один раз, читает файл при старте,
//  пишет при каждом изменении. Доступен и из C++, и из QML.
// ─────────────────────────────────────────────────────────────────────────
class Store : public QObject
{
    Q_OBJECT
public:
    explicit Store(QObject *parent = nullptr);

    // ── Ключи ───────────────────────────────────────────────────────────
    QStringList keys() const;
    void setKeys(const QStringList &keys);
    void addKey(const QString &uri);

    // ── Настройки (произвольные значения) ───────────────────────────────
    QVariant setting(const QString &name, const QVariant &def = {}) const;
    void setSetting(const QString &name, const QVariant &value);

    // ── Статистика сессий ───────────────────────────────────────────────
    // Добавить завершённую сессию в историю.
    void addSession(const QJsonObject &session);
    // Вернуть все сессии как список словарей (для UI/выгрузки).
    QVariantList sessions() const;

    // ── Пароль админа ───────────────────────────────────────────────────
    // Установить пароль (хранится как SHA-256 хеш, не в открытом виде).
    void setAdminPassword(const QString &plain);
    // Проверить введённый пароль. Если пароль ещё не задан — принимает
    // дефолтный "admin" (чтобы было чем войти в первый раз).
    bool checkAdminPassword(const QString &plain) const;
    bool hasAdminPassword() const;

private:
    QString filePath() const;
    void load();
    void save();

    QJsonObject m_root;   // корневой JSON-объект всего хранилища
};
