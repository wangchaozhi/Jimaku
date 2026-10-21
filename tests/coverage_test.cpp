#include "check.h"
#include "core/coverage.h"
#include "core/mediadecoder.h"
#include "core/segment.h"

#include <cmath>

int main()
{
    // 从头开始，逐段推进
    Coverage c(600);
    expectEq("start at zero", c.nextStart(-1), 0.0);
    c.add(0, 60);
    expectEq("continue after first chunk", c.nextStart(-1), 60.0);
    expectEq("limit is end of video", c.limitAfter(60), 600.0);

    // 跳到 400 秒：从那里开始；回头补的那一段不能越过 400
    expectEq("jump to playback position", c.nextStart(400), 400.0);
    c.add(400, 460);
    expectEq("back to earliest gap", c.nextStart(-1), 60.0);
    expectEq("gap limited by next island", c.limitAfter(60), 400.0);
    expectEq("priority inside covered range ignored", c.nextStart(430), 60.0);
    expectEq("priority near gap start snaps", c.nextStart(462), 460.0);

    // 补满后合并成一个区间
    c.add(60, 400);
    expectEq("merged ranges", int(c.ranges().size()), 1);
    c.add(460, 600);
    check("complete", c.complete());
    expectEq("nothing left", c.nextStart(-1), -1.0);
    expectEq("covered seconds", c.coveredSeconds(), 600.0);

    // 安静位置：中间 1 秒静音
    std::vector<float> pcm(size_t(MediaDecoder::kSampleRate) * 10);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = float(0.5 * std::sin(i * 0.05));
    for (size_t i = size_t(MediaDecoder::kSampleRate) * 6; i < size_t(MediaDecoder::kSampleRate) * 7; ++i)
        pcm[i] = 0;
    const double cut = MediaDecoder::quietestPoint(pcm, 2, 9);
    check("quietest point inside silence", cut > 6.0 && cut < 7.0, QString::number(cut));

    // 重复的识别结果
    auto seg = [](const QString& ja) {
        Segment s;
        s.ja = ja;
        return s;
    };
    const QString loop = QStringLiteral("ご視聴ありがとうございました");
    const auto kept = dropRepeats({seg(loop), seg(loop), seg(loop), seg(loop), seg(QStringLiteral("次"))});
    expectEq("long repeats trimmed to two", int(kept.size()), 3);
    const auto chants = dropRepeats({seg(QStringLiteral("XG!")), seg(QStringLiteral("XG!")), seg(QStringLiteral("XG!")),
                                     seg(QStringLiteral("XG!"))});
    expectEq("short chants kept", int(chants.size()), 4);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
