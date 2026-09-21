#include "MediaEngine.h"
#include "MpvMediaEngine.h"
#include "QtMediaEngine.h"
#include "../app/Settings.h"
#include "../util/Subprocess.h"

namespace adoloop {

MediaEngine *MediaEngine::create(QObject *parent)
{
    // 优先 mpv：配置路径存在或在 PATH 中可找到
    QString mpv = Settings::instance().mpvPath();
    if (mpv.isEmpty())
        Subprocess::findExecutable(QStringLiteral("mpv"), &mpv);
    if (!mpv.isEmpty())
        return new MpvMediaEngine(parent);
    return new QtMediaEngine(parent);
}

} // namespace adoloop
