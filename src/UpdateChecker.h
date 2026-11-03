#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>

class QNetworkAccessManager;
class QTimer;
class Store;

// ─────────────────────────────────────────────────────────────────────────
//  UpdateChecker — проверка обновлений приложения.
//
//  Делает HTTPS GET к updateUrl (JSON: { "version": "x.y", "url": "...",
//  "notes": "..." }) и сравнивает с текущей версией. Если новее — отдаёт UI
//  ссылку на скачивание.
//
//  Манифест лежит в репо strdr1/IAMVPN (не AM.SALES!), установщик — в его
//  Releases. Кнопки «Проверить» / «Скачать новую версию» в UI.
// ─────────────────────────────────────────────────────────────────────────
class UpdateChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString downloadUrl READ downloadUrl NOTIFY downloadUrlChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY notesChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)

public:
    // URL манифеста обновлений IAMVPN.
    static QString defaultUpdateUrl() {
        return QStringLiteral(
            "https://raw.githubusercontent.com/strdr1/IAMVPN/main/latest.json");
    }

    explicit UpdateChecker(Store *store = nullptr, QObject *parent = nullptr);

    // Версия задаётся одна — QApplication::setApplicationVersion в main.cpp.
    static QString currentVersion();
    QString latestVersion() const { return m_latest; }
    QString downloadUrl() const { return m_url; }
    QString notes() const { return m_notes; }
    QString statusText() const { return m_status; }
    bool checking() const { return m_checking; }
    bool updateAvailable() const { return m_avail; }

public slots:
    // Проверить через HTTPS. Если url пустой — используется defaultUpdateUrl().
    void check(const QString &updateUrl = QString());
    // Скачать установщик во временную папку и запустить.
    void downloadAndInstall();

signals:
    void latestVersionChanged();
    void downloadUrlChanged();
    void notesChanged();
    void statusTextChanged();
    void checkingChanged();
    void updateAvailableChanged();

private slots:
    void autoTick();   // вызывается таймером раз в N часов

private:
    static int cmpVersions(const QString &a, const QString &b);

    Store *m_store = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QTimer *m_autoTimer = nullptr;
    QString m_latest;
    QString m_url;
    QString m_notes;
    QString m_status = QStringLiteral("Не проверялось");
    bool m_checking = false;
    bool m_avail = false;
};
