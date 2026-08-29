#pragma once

#include <string>

namespace Localization
{
enum class Language : unsigned char
{
    Japanese = 0,
    Chinese = 1,
    English = 2,
};

void Initialize();
Language GetLanguage();
bool SetLanguage(Language language);
bool CycleLanguage();
const char *GetLanguageName();
bool ConsumeRestartRequest();
// The stock title screen already contains its final English artwork. Keep
// translation disabled while that screen is active so no second text layer
// is written over the original texture.
void SetTitlePageActive(bool active);
// Returns the original string when it is outside the explicit translation
// table. `translated` lets the renderer select a localized font only for text
// that was actually replaced, preserving stock English and stage text.
std::string Translate(const char *text, bool *translated = nullptr);
std::string TranslateDialogue(int stage, int script, int time, int opcode,
                              int occurrence, int line, const char *fallback,
                              bool *translated = nullptr);
std::string TranslateMusicTitle(int track, const char *fallback,
                                bool *translated = nullptr);
std::string TranslateMusicComment(int track, int line, const char *fallback,
                                  bool *translated = nullptr);
} // namespace Localization
