#include "Store.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>

// ─────────────────────────────────────────────────────────────────────────
//  Реализация Store. Простой JSON-файл в AppData.
// ─────────────────────────────────────────────────────────────────────────

Store::Store(QObject *parent)
    : QObject(parent)
{
    load();
}

QString Store::filePath() const
{
    // %APPDATA%/AM.SALES/store.json
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/store.json");
}

void Store::load()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        // Файла ещё нет — создаём пустую структуру.
        m_root = QJsonObject{
            {"keys", QJsonArray{}},
            {"settings", QJsonObject{}},
            {"sessions", QJsonArray{}}
        };
        return;
    }
    const QByteArray data = f.readAll();
    f.close();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    m_root = doc.isObject() ? doc.object() : QJsonObject{};
    // Гарантируем наличие основных секций.
    if (!m_root.contains("keys"))     m_root["keys"] = QJsonArray{};
    if (!m_root.contains("settings")) m_root["settings"] = QJsonObject{};
    if (!m_root.contains("sessions")) m_root["sessions"] = QJsonArray{};
}

void Store::save()
{
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    f.close();
}

// ── Ключи ───────────────────────────────────────────────────────────────
QStringList Store::keys() const
{
    QStringList out;
    const QJsonArray arr = m_root.value("keys").toArray();
    for (const QJsonValue &v : arr)
        out << v.toString();
    return out;
}

void Store::setKeys(const QStringList &keys)
{
    QJsonArray arr;
    for (const QString &k : keys)
        arr.append(k);
    m_root["keys"] = arr;
    save();
}

void Store::addKey(const QString &uri)
{
    QStringList k = keys();
    if (!k.contains(uri)) {
        k << uri;
        setKeys(k);
    }
}

// ── Настройки ─────────────────────────────────────────────────────────────
QVariant Store::setting(const QString &name, const QVariant &def) const
{
    const QJsonObject s = m_root.value("settings").toObject();
    if (!s.contains(name))
        return def;
    return s.value(name).toVariant();
}

void Store::setSetting(const QString &name, const QVariant &value)
{
    QJsonObject s = m_root.value("settings").toObject();
    s[name] = QJsonValue::fromVariant(value);
    m_root["settings"] = s;
    save();
}

// ── Статистика ──────────────────────────────────────────────────────────
void Store::addSession(const QJsonObject &session)
{
    QJsonArray arr = m_root.value("sessions").toArray();
    arr.append(session);
    m_root["sessions"] = arr;
    save();
}

QVariantList Store::sessions() const
{
    QVariantList out;
    const QJsonArray arr = m_root.value("sessions").toArray();
    for (const QJsonValue &v : arr)
        out.append(v.toObject().toVariantMap());
    return out;
}

// ── Пароль админа (хранится хешем SHA-256) ───────────────────────────────
static QString hashPw(const QString &plain)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(plain.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
}

void Store::setAdminPassword(const QString &plain)
{
    setSetting(QStringLiteral("adminPwHash"), hashPw(plain));
}

bool Store::hasAdminPassword() const
{
    return !setting(QStringLiteral("adminPwHash")).toString().isEmpty();
}

bool Store::checkAdminPassword(const QString &plain) const
{
    const QString stored = setting(QStringLiteral("adminPwHash")).toString();
    if (stored.isEmpty()) {
        // Пароль ещё не задан — пускаем по дефолтному "admin".
        return plain == QStringLiteral("admin");
    }
    return hashPw(plain) == stored;
}
