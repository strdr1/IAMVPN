#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;

// ─────────────────────────────────────────────────────────────────────────
//  DiagController — диагностика для пользователя «почему не работает».
//
//  Делает несколько проверок и выводит понятный вердикт по каждой:
//   - есть ли вообще интернет (1.1.1.1, 8.8.8.8);
//   - доступен ли наш VPN-сервер по IP (TCP 443);
//   - резолвится ли Cloudflare-домен и доступен ли он (для CF-ключей);
//   - запущены ли winws (zapret) и sing-box (VPN);
//   - есть ли запись о подключении в логах sing-box (последние строки).
//
//  В UI открывает большой текстовый блок с подробностями (логи).
// ─────────────────────────────────────────────────────────────────────────
class DiagController : public QObject
{
    Q_OBJECT
    // Список проверок: [{title, status, detail}] (status: ok/warn/fail/pending).
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // Объединённый лог (sing-box stdout/stderr + ошибки запуска).
    Q_PROPERTY(QString logs READ logs NOTIFY logsChanged)

public:
    explicit DiagController(QObject *parent = nullptr);

    QVariantList results() const { return m_results; }
    bool running() const { return m_running; }
    QString logs() const { return m_logs; }

public slots:
    // Прогнать все проверки.
    void runAll();
    // Перечитать логи sing-box/winws с диска.
    void refreshLogs();
    // Скопировать диагностику в буфер обмена (для отправки разработчику).
    void copyToClipboard();

signals:
    void resultsChanged();
    void runningChanged();
    void logsChanged();

private:
    void setRunning(bool r);
    void addResult(const QString &title, const QString &status, const QString &detail);
    void resetResults();

    // Отдельные проверки.
    void checkInternet();
    void checkProcesses();
    void readEngineLogs();

    QNetworkAccessManager *m_net = nullptr;
    QVariantList m_results;
    bool m_running = false;
    QString m_logs;
    int m_pending = 0;   // сколько асинхронных проверок ещё идёт
};
