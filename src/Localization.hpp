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
std::string Translate(const char *text);
} // namespace Localization
