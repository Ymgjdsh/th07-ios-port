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
std::string Translate(const char *text);
} // namespace Localization
