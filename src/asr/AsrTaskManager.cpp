#include "AsrTaskManager.h"
#include "WhisperCppAsr.h"
#include "FasterWhisperAsr.h"
#include "../app/Settings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace adoloop {

AsrTaskManager::AsrTaskManager(QObject *parent)
    : QObject(parent)
{
    m_cacheDir = Settings::instance().cacheDir();
}

QString AsrTaskManager::cachePathFor(const QString &mediaPath, const QString &cacheDir)
{
    const QByteArray hash = QCryptographicHash::hash(mediaPath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(cacheDir).filePath(QString::fromLatin1(hash) + QStringLiteral(".asr.json"));
}

bool AsrTaskManager::readCache(const QString &path, QVector<AsrSegment> *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isArray())
        return false;
    const QJsonArray arr = doc.array();
    out->clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        AsrSegment s;
        s.start = Ms(qRound64(o.value(QStringLiteral("start")).toDouble()));
        s.end = Ms(qRound64(o.value(QStringLiteral("end")).toDouble()));
        s.text = o.value(QStringLiteral("text")).toString();
        for (const QJsonValue &wv : o.value(QStringLiteral("words")).toArray()) {
            const QJsonObject wo = wv.toObject();
            WordToken t;
            t.word = wo.value(QStringLiteral("word")).toString();
            t.start = Ms(qRound64(wo.value(QStringLiteral("start")).toDouble()));
            t.end = Ms(qRound64(wo.value(QStringLiteral("end")).toDouble()));
            s.words << t;
        }
        if (s.end > s.start)
            *out << s;
    }
    return !out->isEmpty();
}

void AsrTaskManager::writeCache(const QString &path, const QVector<AsrSegment> &segments)
{
    QJsonArray arr;
    for (const AsrSegment &s : segments) {
        QJsonObject o;
        o.insert(QStringLiteral("start"), double(s.start));
        o.insert(QStringLiteral("end"), double(s.end));
        o.insert(QStringLiteral("text"), s.text);
        QJsonArray ws;
        for (const WordToken &t : s.words) {
            QJsonObject wo;
            wo.insert(QStringLiteral("word"), t.word);
            wo.insert(QStringLiteral("start"), double(t.start));
            wo.insert(QStringLiteral("end"), double(t.end));
            ws.append(wo);
        }
        o.insert(QStringLiteral("words"), ws);
        arr.append(o);
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

AsrEngine *AsrTaskManager::createEngine(AsrEngine::Engine e)
{
    AsrEngine *eng = (e == AsrEngine::Engine::WhisperCpp)
        ? static_cast<AsrEngine *>(new WhisperCppAsr(this))
        : static_cast<AsrEngine *>(new FasterWhisperAsr(this));

    connect(eng, &AsrEngine::progress, this, [this](float f, const QString &stage) {
        for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
            if (it->status == EngineStatus::Running) {
                emit taskProgress(it.key(), f, stage);
                break;
            }
        }
    });
    connect(eng, &AsrEngine::finished, this, [this](const QVector<AsrSegment> &seg) {
        onEngineFinished(seg);
    });
    connect(eng, &AsrEngine::failed, this, [this](const QString &err) {
        onEngineFailed(err);
    });
    connect(eng, &AsrEngine::cancelled, this, [this]() {
        onEngineFailed(QStringLiteral("已取消"));
    });
    return eng;
}

quint64 AsrTaskManager::transcribe(const QString &mediaPath, AsrEngine::Engine engine,
                                   const AsrEngine::Options &options)
{
    if (mediaPath.isEmpty())
        return 0;

    // 缓存命中：直接完成
    QDir().mkpath(m_cacheDir);
    const QString cachePath = cachePathFor(mediaPath, m_cacheDir);
    QVector<AsrSegment> cached;
    if (readCache(cachePath, &cached)) {
        Task t;
        t.id = m_nextId++;
        t.mediaPath = mediaPath;
        t.engine = engine;
        t.options = options;
        t.status = EngineStatus::Finished;
        emit taskFinished(t.id, cached);
        return t.id;
    }

    Task t;
    t.id = m_nextId++;
    t.mediaPath = mediaPath;
    t.engine = engine;
    t.options = options;
    t.status = EngineStatus::Queued;
    m_tasks.insert(t.id, t);
    m_queue.enqueue(t.id);
    startNext();
    return t.id;
}

void AsrTaskManager::startNext()
{
    if (m_current || m_queue.isEmpty())
        return;

    while (!m_queue.isEmpty()) {
        const quint64 id = m_queue.head();
        if (!m_tasks.contains(id)) {
            m_queue.dequeue();
            continue;
        }
        Task &t = m_tasks[id];
        m_queue.dequeue();
        m_current = createEngine(t.engine);
        t.status = EngineStatus::Running;
        QString reason;
        if (!m_current->isAvailable(&reason)) {
            m_current->deleteLater();
            m_current = nullptr;
            t.status = EngineStatus::Failed;
            t.error = reason.isEmpty() ? QStringLiteral("引擎不可用") : reason;
            emit taskFailed(id, t.error);
            QTimer::singleShot(0, this, [this]() { startNext(); });
            return;
        }
        m_current->start(t.mediaPath, t.options);
        return;
    }
    emit allFinished();
}

void AsrTaskManager::onEngineFinished(const QVector<AsrSegment> &segments)
{
    AsrEngine *eng = m_current;
    m_current = nullptr;
    if (!eng)
        return;

    quint64 id = 0;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->status == EngineStatus::Running) {
            id = it.key();
            break;
        }
    }
    eng->deleteLater();
    if (id == 0)
        return;

    Task &t = m_tasks[id];
    t.status = EngineStatus::Finished;
    t.result = segments;
    const QString cachePath = cachePathFor(t.mediaPath, m_cacheDir);
    writeCache(cachePath, segments);
    emit taskFinished(id, segments);
    startNext();
}

void AsrTaskManager::onEngineFailed(const QString &error)
{
    AsrEngine *eng = m_current;
    m_current = nullptr;
    if (!eng)
        return;
    quint64 id = 0;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->status == EngineStatus::Running) {
            id = it.key();
            break;
        }
    }
    eng->deleteLater();
    if (id == 0)
        return;
    Task &t = m_tasks[id];
    t.status = EngineStatus::Failed;
    t.error = error;
    emit taskFailed(id, error);
    startNext();
}

void AsrTaskManager::cancel(quint64 taskId)
{
    if (!m_tasks.contains(taskId))
        return;
    Task &t = m_tasks[taskId];
    if (t.status == EngineStatus::Queued) {
        // 从队列移除
        QQueue<quint64> kept;
        while (!m_queue.isEmpty()) {
            const quint64 id = m_queue.dequeue();
            if (id != taskId)
                kept.enqueue(id);
        }
        m_queue = kept;
        t.status = EngineStatus::Cancelled;
        emit taskFailed(taskId, QStringLiteral("已取消"));
        return;
    }
    if (t.status == EngineStatus::Running && m_current)
        m_current->cancel(); // 将经 onEngineFailed 走完流程
}

void AsrTaskManager::cancelAll()
{
    if (m_current)
        m_current->cancel();
    while (!m_queue.isEmpty())
        m_queue.dequeue();
    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        if (it->status == EngineStatus::Queued) {
            it->status = EngineStatus::Cancelled;
            emit taskFailed(it.key(), QStringLiteral("已取消"));
            it = m_tasks.erase(it);
        } else {
            ++it;
        }
    }
}

bool AsrTaskManager::isBusy() const
{
    return m_current != nullptr || !m_queue.isEmpty();
}

} // namespace adoloop
