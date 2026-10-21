#pragma once

#include "subtitlewriter.h"
#include "translator.h"

#include <QString>

// 翻译服务预设（都是 OpenAI 兼容接口）
struct TranslateProvider {
    QString id;        // deepseek / openai / ollama / custom
    QString label;
    QString baseUrl;
    QString model;
    bool needsKey;
};

// 用户设置：普通选项存 QSettings；API key 只存系统钥匙串（macOS 钥匙串 / Windows 凭据管理器）
namespace Settings {

const QList<TranslateProvider>& providers();
TranslateProvider provider(const QString& id); // 找不到时返回 DeepSeek

QString providerId();
void setProviderId(const QString& id);
QString baseUrl();   // 当前服务的接口地址（用户改过就用改过的）
void setBaseUrl(const QString& url);
QString modelName();
void setModelName(const QString& model);

QString asrModel();
void setAsrModel(const QString& id);

SubtitleWriter::Format outputFormat();
void setOutputFormat(SubtitleWriter::Format format);
SubtitleWriter::Layout layout();
void setLayout(SubtitleWriter::Layout layout);

// 字幕外观：位置（顶部 / 底部）和字号倍数
SubtitleWriter::Appearance appearance();
void setAppearance(const SubtitleWriter::Appearance& a);

// 翻译风格
bool subtitleStyle();   // 句末不加标点、逗号换空格、译文尽量短（默认开）
void setSubtitleStyle(bool on);
bool translateLyrics(); // 英文歌词、口号也翻译（默认保留原文）
void setTranslateLyrics(bool on);
bool reviewEnabled();   // 翻译后全片校对：统一专有名词、纠正识别错误（默认开）
void setReviewEnabled(bool on);

// 每行 "日文=中文"
QString glossaryText();
void setGlossaryText(const QString& text);
QList<QPair<QString, QString>> parseGlossary(const QString& text);

// 钥匙串读写（同步，内部跑事件循环）。环境变量 JIMAKU_API_KEY 优先，方便命令行和测试
QString apiKey(const QString& providerId);
bool setApiKey(const QString& providerId, const QString& key, QString* error);
bool hasApiKey(const QString& providerId);

// 汇总成翻译配置
TranslatorConfig translatorConfig();

} // namespace Settings
