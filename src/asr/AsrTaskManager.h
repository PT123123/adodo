#pragma once

#include "../core/Types.h"
#include "AsrEngine.h"

#include <QHash>
#include <QObject>
#include <QQueue>

namespace adoloop {

// ASR 任务队列：串行执行；结果缓存到 <cacheDir>/asr-<hash>.json，命中直接返回。
// 对外 API 以 taskId 关联进度/结果。
class AsrTaskManager : public QObject {
    Q_OBJECT
public:
    explicit AsrTaskManager(QObject *parent = nullptr);

    struct Task {
        quint64 id = 0;
        QString mediaPath;
        AsrEngine::Engine engine = AsrEngine::Engine::WhisperCpp;
        AsrEngine::Options options;
        QVector<AsrSegment> result;
        QString error;
        EngineStatus status = EngineStatus::Idle;
    };

    // 入队并立即开始（如空闲）。返回 taskId；0 表示入队失败。
    quint64 transcribe(const QString &mediaPath, AsrEngine::Engine engine,
                       const AsrEngine::Options &options);

    void cancel(quint64 taskId);
    void cancelAll();
    bool isBusy() const;

    static QString cachePathFor(const QString &mediaPath, const QString &cacheDir);
    static bool readCache(const QString &path, QVector<AsrSegment> *out);
    static void writeCache(const QString &path, const QVector<AsrSegment> &segments);

signals:
    void taskProgress(quint64 taskId, float fraction, const QString &stage);
    void taskFinished(quint64 taskId, const QVector<adoloop::AsrSegment> &segments);
    void taskFailed(quint64 taskId, const QString &error);
    void allFinished();

private:
    void startNext();
    void onEngineFinished(const QVector<AsrSegment> &segments);
    void onEngineFailed(const QString &error);

    AsrEngine *createEngine(AsrEngine::Engine e);

    QQueue<quint64> m_queue;
    QHash<quint64, Task> m_tasks;
    AsrEngine *m_current = nullptr;
    quint64 m_nextId = 1;
    QString m_cacheDir;
};

} // namespace adoloop
