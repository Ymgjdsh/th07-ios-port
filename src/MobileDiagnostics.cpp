#include "MobileDiagnostics.hpp"

#include <SDL3/SDL.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#include "FileSystem.hpp"
#include "inttypes.hpp"

namespace
{
std::mutex g_LogMutex;
FILE *g_LogFile = nullptr;
u64 g_StartTicks = 0;

void OpenLog()
{
    if (g_LogFile)
    {
        return;
    }

    const std::string path = FileSystem::GetPrefPath("th07-mobile.log");
    g_LogFile = fopen(path.c_str(), "a");
    g_StartTicks = SDL_GetTicks();
    if (g_LogFile)
    {
        fprintf(g_LogFile, "\n=== TH07 iOS session start ===\n");
        fflush(g_LogFile);
    }
    SDL_Log("[mobile/init] log=%s open=%d", path.c_str(), g_LogFile ? 1 : 0);
}
} // namespace

void MobileDiagnostics::Initialize()
{
    std::lock_guard<std::mutex> lock(g_LogMutex);
    OpenLog();
}

void MobileDiagnostics::Log(const char *tag, const char *format, ...)
{
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_LogMutex);
    OpenLog();
    const unsigned long long elapsed = (unsigned long long)(SDL_GetTicks() - g_StartTicks);
    if (g_LogFile)
    {
        fprintf(g_LogFile, "%llums [%s] %s\n", elapsed, tag ? tag : "mobile", message);
        fflush(g_LogFile);
    }
    // Supervisor::DebugPrint forwards SDL logs here, so logging back through SDL
    // would recurse. stderr remains visible in Xcode and the macOS Console.
    fprintf(stderr, "[%s] %s\n", tag ? tag : "mobile", message);
    fflush(stderr);
}

void MobileDiagnostics::Shutdown()
{
    std::lock_guard<std::mutex> lock(g_LogMutex);
    if (g_LogFile)
    {
        fprintf(g_LogFile, "=== TH07 iOS session end ===\n");
        fclose(g_LogFile);
        g_LogFile = nullptr;
    }
}
