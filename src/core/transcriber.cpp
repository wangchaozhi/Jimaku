#include "transcriber.h"

#include <QFileInfo>
#include <QObject>
#include <QThread>
#include <cstdio>
#include <whisper.h>

namespace {

void quietLog(ggml_log_level level, const char* text, void*)
{
    if (level >= GGML_LOG_LEVEL_ERROR || qEnvironmentVariableIsSet("JIMAKU_DEBUG"))
        fputs(text, stderr);
}

struct Callbacks {
    const std::function<void(double)>* progress;
    const std::function<void(const QList<Segment>&)>* onSegments;
    const std::atomic_bool* cancel;
    QList<Segment>* out;
    double offset;
};

// 第 s 段 → Segment：把字节级 token 攒成完整 UTF-8 再作为一个词，时间取首尾 token。没有文字时返回 false
bool extractSegment(whisper_context* ctx, int s, double offset, Segment* seg)
{
    const whisper_token eot = whisper_token_eot(ctx);
    seg->start = whisper_full_get_segment_t0(ctx, s) / 100.0 + offset;
    seg->end = whisper_full_get_segment_t1(ctx, s) / 100.0 + offset;
    QByteArray pending;
    double pendingStart = -1;
    const int nTok = whisper_full_n_tokens(ctx, s);
    for (int t = 0; t < nTok; ++t) {
        if (whisper_full_get_token_id(ctx, s, t) >= eot) // [_BEG_]、时间戳等特殊 token
            continue;
        const double t0 = whisper_full_get_token_t0(ctx, s, t) / 100.0 + offset;
        const double t1 = whisper_full_get_token_t1(ctx, s, t) / 100.0 + offset;
        if (pending.isEmpty())
            pendingStart = t0;
        pending += whisper_full_get_token_text(ctx, s, t);
        const int ok = completeUtf8Length(pending);
        if (ok == 0)
            continue;
        Word w{pendingStart, qMax(t1, pendingStart), QString::fromUtf8(pending.left(ok))};
        pending = pending.mid(ok);
        pendingStart = t1;
        if (!w.text.trimmed().isEmpty())
            seg->words.append(w);
    }
    if (seg->words.isEmpty())
        return false;
    seg->ja = joinWords(seg->words);
    // 词级时间比整段时间更贴近实际发声
    seg->start = qMax(seg->start, seg->words.first().start);
    seg->end = qMax(seg->start, qMin(seg->end, seg->words.last().end));
    return true;
}

} // namespace

int completeUtf8Length(const QByteArray& bytes)
{
    const int n = bytes.size();
    int i = n - 1;
    // 往回找最后一个字符的首字节（跳过 10xxxxxx 续字节，最多 3 个）
    while (i >= 0 && i > n - 4 && (uchar(bytes[i]) & 0xC0) == 0x80)
        --i;
    if (i < 0)
        return 0;
    const uchar lead = uchar(bytes[i]);
    int need = 1;
    if ((lead & 0xE0) == 0xC0)
        need = 2;
    else if ((lead & 0xF0) == 0xE0)
        need = 3;
    else if ((lead & 0xF8) == 0xF0)
        need = 4;
    return (n - i >= need) ? n : i;
}

Transcriber::~Transcriber()
{
    if (m_ctx)
        whisper_free(m_ctx);
}

bool Transcriber::load(const QString& modelPath, QString* error)
{
    if (m_ctx && modelPath == m_modelPath)
        return true;
    if (!QFileInfo::exists(modelPath)) {
        *error = QObject::tr("识别模型不存在：%1").arg(modelPath);
        return false;
    }
    if (m_ctx) {
        whisper_free(m_ctx);
        m_ctx = nullptr;
    }
    whisper_log_set(quietLog, nullptr);
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = true;
    cp.flash_attn = true;
    m_ctx = whisper_init_from_file_with_params(QFile::encodeName(modelPath).constData(), cp);
    if (!m_ctx) {
        *error = QObject::tr("识别模型加载失败（文件可能下载不完整）：%1").arg(modelPath);
        m_modelPath.clear();
        return false;
    }
    m_modelPath = modelPath;
    return true;
}

QList<Segment> Transcriber::transcribe(const std::vector<float>& pcm,
                                       const std::function<void(double)>& progress, const std::atomic_bool* cancel,
                                       QString* error, const std::function<void(const QList<Segment>&)>& onSegments,
                                       double timeOffset)
{
    if (!m_ctx) {
        *error = QObject::tr("识别模型还没有加载");
        return {};
    }

    const bool beam = m_decoding == Decoding::BeamSearch || m_decoding == Decoding::BeamSearchFallback;
    whisper_full_params wp = whisper_full_default_params(beam ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    if (beam)
        wp.beam_search.beam_size = 5;
    if (m_decoding == Decoding::GreedyNoFallback || m_decoding == Decoding::BeamSearch)
        wp.temperature_inc = 0.0f; // 不做温度回退：同样的音频每次结果一样
    wp.language = "ja";
    wp.translate = false;
    wp.token_timestamps = true;
    wp.no_context = true; // 不拿上一段当提示，避免一句话被重复输出很多遍
    wp.suppress_nst = true;
    wp.print_progress = false;
    wp.print_realtime = false;
    wp.print_timestamps = false;
    wp.print_special = false;
    wp.n_threads = qBound(1, QThread::idealThreadCount(), 8);

    QList<Segment> out;
    Callbacks cb{&progress, &onSegments, cancel, &out, timeOffset};
    // 每解码出几段就立即交出去，界面可以边识别边显示
    wp.new_segment_callback = [](whisper_context* ctx, whisper_state*, int nNew, void* user) {
        auto* c = static_cast<Callbacks*>(user);
        const int n = whisper_full_n_segments(ctx);
        QList<Segment> fresh;
        for (int s = n - nNew; s < n; ++s) {
            Segment seg;
            if (extractSegment(ctx, s, c->offset, &seg))
                fresh.append(seg);
        }
        if (fresh.isEmpty())
            return;
        c->out->append(fresh);
        if (*c->onSegments)
            (*c->onSegments)(fresh);
    };
    wp.new_segment_callback_user_data = &cb;
    wp.progress_callback = [](whisper_context*, whisper_state*, int percent, void* user) {
        auto* c = static_cast<Callbacks*>(user);
        if (*c->progress)
            (*c->progress)(percent / 100.0);
    };
    wp.progress_callback_user_data = &cb;
    wp.abort_callback = [](void* user) {
        auto* c = static_cast<Callbacks*>(user);
        return c->cancel && c->cancel->load();
    };
    wp.abort_callback_user_data = &cb;

    const int rc = whisper_full(m_ctx, wp, pcm.data(), int(pcm.size()));
    if (cancel && cancel->load())
        return {};
    if (rc != 0) {
        *error = QObject::tr("语音识别失败（whisper 错误码 %1）").arg(rc);
        return {};
    }

    if (progress)
        progress(1.0);
    return out;
}
