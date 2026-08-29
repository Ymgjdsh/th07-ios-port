#include "Localization.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "FileSystem.hpp"
#include "TextHelper.hpp"

namespace
{
constexpr unsigned int kConfigMagic = 0x31474e4cu; // LNG1
constexpr unsigned char kConfigVersion = 1;

Localization::Language g_Language = Localization::Language::Japanese;
bool g_RestartRequested = false;

struct LanguageConfig
{
    unsigned int magic;
    unsigned char version;
    unsigned char language;
    unsigned char reserved[2];
};

struct Translation
{
    const char *japanese;
    const char *chinese;
    const char *english;
};

constexpr Translation kTranslations[] = {
    {"ゲームを開始します", "开始游戏", "Start the game"},
    {"エキストラステージを開始します", "开始 Extra 关卡", "Start the Extra stage"},
    {"ステージを選択し、練習を開始します", "选择关卡并开始练习", "Select a stage for practice"},
    {"リプレイを鑑賞できます", "观看回放", "Watch replays"},
    {"過去のスコアやスペルカードの取得歴を見られます", "查看历史分数和符卡记录", "View scores and spell records"},
    {"音楽を聴けます", "聆听音乐", "Listen to music"},
    {"各種設定できます", "打开设置", "Configure options"},
    {"いろいろと終了します", "退出游戏", "Quit the game"},
    {"通信方法を選択して二人プレイを開始します", "选择连接方式开始双人游戏", "Choose a connection for two-player mode"},
    {"ショット、決定ボタンを設定します", "设置射击/确认键", "Configure shoot and confirm"},
    {"ボム、キャンセルボタンを設定します", "设置炸弹/取消键", "Configure bomb and cancel"},
    {"低速移動ボタンを設定します", "设置低速移动键", "Configure focus movement"},
    {"メッセージスキップボタンを設定します", "设置对话跳过键", "Configure message skip"},
    {"ポーズボタンを設定します", "设置暂停键", "Configure pause"},
    {"上移動ボタンを設定します", "设置上移键", "Configure move up"},
    {"下移動ボタンを設定します", "设置下移键", "Configure move down"},
    {"左移動ボタンを設定します", "设置左移键", "Configure move left"},
    {"右移動ボタンを設定します", "设置右移键", "Configure move right"},
    {"ショット押しっぱなしで低速移動になるようにします", "按住射击键时低速移动", "Hold shoot to focus"},
    {"初期設定に戻します", "恢复默认设置", "Restore defaults"},
    {"おおよそ終了します", "返回上一级", "Go back"},
    {"プレイヤーの初期数を変更します。（初期設定　３）", "更改初始残机数（默认 3）", "Change starting lives (default 3)"},
    {"画面の色数を変更します。３２ＢＩＴだと最も綺麗に表示されます。", "更改色深度，32 位效果最佳", "Change color depth; 32-bit looks best"},
    {"ＢＧＭの再生方法を変更します。（初期設定　ＷＡＶ）", "更改 BGM 播放方式（默认 WAV）", "Change BGM playback (default WAV)"},
    {"効果音を再生するか選択します", "开关音效", "Toggle sound effects"},
    {"ウィンドウかフルスクリーンか選択します", "选择窗口或全屏", "Choose windowed or fullscreen"},
    {"弾が多い場面でわざと処理落ちさせます(スコア、リプレイ記録不可)", "弹幕密集时主动降速（不可记录分数/回放）", "Slow down in bullet-heavy scenes (no score/replay)"},
    {"全て初期設定にします", "全部恢复默认", "Restore all defaults"},
    {"パッド操作のボタン配置を変更します", "更改手柄按键布局", "Configure gamepad buttons"},
    {"おいそれと終了します", "退出设置", "Exit options"},
    {"プレイ回数", "游戏次数", "Attempts"},
    {"クリア回数", "通关次数", "Clears"},
    {"コンティニュー", "续关次数", "Continues"},
    {"プラクティス", "练习次数", "Practice"},
    {"リトライ回数", "重试次数", "Retries"},
    {"総起動時間", "总运行时间", "Total uptime"},
    {"総プレイ時間", "总游戏时间", "Total play time"},
    {"スペルカード取得", "符卡取得", "Spell cards"},
    {"キャラ切り替え↓↑", "切换角色 ↓↑", "Change character ↓↑"},
    {"霊符「夢想封印　散」", "灵符「梦想封印·散」", "Spirit Sign \"Fantasy Seal -Spread-\""},
    {"霊符「夢想封印　集」", "灵符「梦想封印·集」", "Spirit Sign \"Fantasy Seal -Concentrate-\""},
    {"夢符「封魔陣」", "梦符「封魔阵」", "Dream Sign \"Evil-Sealing Circle\""},
    {"夢符「二重結界」", "梦符「二重结界」", "Dream Sign \"Duplex Barrier\""},
    {"魔符「スターダストレヴァリエ」", "魔符「星尘幻想」", "Magic Sign \"Stardust Reverie\""},
    {"魔符「ミルキーウェイ」", "魔符「银河」", "Magic Sign \"Milky Way\""},
    {"恋符「ノンディレクショナルレーザー」", "恋符「非定向激光」", "Love Sign \"Non-Directional Laser\""},
    {"恋符「マスタースパーク」", "恋符「极限火花」", "Love Sign \"Master Spark\""},
    {"幻符「インディスクリミネイト」", "幻符「不分青红皂白」", "Illusion Sign \"Indiscriminate\""},
    {"幻符「殺人ドール」", "幻符「杀人玩偶」", "Illusion Sign \"Killing Doll\""},
    {"時符「パーフェクトスクウェア」", "时符「完美方阵」", "Time Sign \"Perfect Square\""},
    {"時符「プライベートスクウェア」", "时符「私人空间」", "Time Sign \"Private Square\""},
};

std::string ReplaceAll(std::string value, const char *from, const char *to)
{
    if (!from || !*from) return value;
    size_t offset = 0;
    const size_t length = std::strlen(from);
    while ((offset = value.find(from, offset)) != std::string::npos)
    {
        value.replace(offset, length, to);
        offset += std::strlen(to);
    }
    return value;
}

std::string TranslateKnownPhrases(const char *text, Localization::Language language)
{
    std::string value = text ? text : "";
    for (const Translation &entry : kTranslations)
    {
        const char *replacement = language == Localization::Language::Chinese
                                      ? entry.chinese
                                      : entry.english;
        value = ReplaceAll(value, entry.japanese, replacement);
    }
    return value;
}
} // namespace

namespace Localization
{
void Initialize()
{
    g_Language = Language::Japanese;
    g_RestartRequested = false;
    const std::string path = FileSystem::GetPrefPath("language.cfg");
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file)
    {
        LanguageConfig config = {};
        if (std::fread(&config, sizeof(config), 1, file) == 1 &&
            config.magic == kConfigMagic && config.version == kConfigVersion &&
            config.language <= static_cast<unsigned char>(Language::English))
        {
            g_Language = static_cast<Language>(config.language);
        }
        std::fclose(file);
    }
}

Language GetLanguage()
{
    return g_Language;
}

bool SetLanguage(Language language)
{
    if (static_cast<unsigned char>(language) >
            static_cast<unsigned char>(Language::English) ||
        language == g_Language)
        return false;
    g_Language = language;
    LanguageConfig config = {kConfigMagic, kConfigVersion,
                             static_cast<unsigned char>(g_Language), {0, 0}};
    const std::string path = FileSystem::GetPrefPath("language.cfg");
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file || std::fwrite(&config, sizeof(config), 1, file) != 1)
    {
        if (file) std::fclose(file);
        return false;
    }
    std::fclose(file);
    TextHelper::ReloadFont();
    g_RestartRequested = true;
    return true;
}

bool CycleLanguage()
{
    const unsigned char next =
        (static_cast<unsigned char>(g_Language) + 1u) % 3u;
    return SetLanguage(static_cast<Language>(next));
}

const char *GetLanguageName()
{
    switch (g_Language)
    {
    case Language::Chinese: return "CHINESE";
    case Language::English: return "ENGLISH";
    default: return "JAPANESE";
    }
}

bool ConsumeRestartRequest()
{
    const bool requested = g_RestartRequested;
    g_RestartRequested = false;
    return requested;
}

std::string Translate(const char *text)
{
    if (g_Language == Language::Japanese) return text ? text : "";
    return TranslateKnownPhrases(text, g_Language);
}
} // namespace Localization
