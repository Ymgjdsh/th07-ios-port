#include "FileSystem.hpp"

#include <cstdio>

#include "GameErrorContext.hpp"
#include "Supervisor.hpp"
#include "pbg4/Pbg4Archive.hpp"

u32 g_LastFileSize;

u8 *FileSystem::OpenFile(const char *filepath, i32 isExternalResource)
{
    SDL_RWops *file;
    u8 *buf;
    u32 fsize;
    const char *filename;

    if (!isExternalResource)
    {
        filename = strrchr(filepath, '\\');
        if (!filename)
        {
            filename = filepath;
        }
        else
        {
            filename++;
        }

        filename = strrchr(filename, '/');
        if (!filename)
        {
            filename = filepath;
        }
        else
        {
            filename++;
        }
        fsize = g_Pbg4Archive.GetEntrySize(filename);
        g_LastFileSize = fsize;
        if (fsize == 0)
        {
            g_GameErrorContext.Fatal("error : %s is not found in arcfile.\n", filename);
            return NULL;
        }
        if (fsize != 0)
        {
            Supervisor::DebugPrint("%s Decode ... \n", filename);
            buf = (u8 *)malloc(fsize);
            if (!buf)
            {
                return NULL;
            }

            g_Pbg4Archive.ReadDecompressEntry(filename, buf);
            return buf;
        }
    }
    Supervisor::DebugPrint("%s Load ... \n", filepath);
    file = SDL_RWFromFile(filepath, "rb");
    if (!file)
    {
        Supervisor::DebugPrint("error : %s is not found.\n", filepath);
        return NULL;
    }

    SDL_RWseek(file, 0, RW_SEEK_END);
    fsize = SDL_RWtell(file);
    buf = (u8 *)malloc(fsize);
    if (!buf)
    {
        SDL_RWclose(file);
        return NULL;
    }

    SDL_RWseek(file, 0, RW_SEEK_SET);
    if (SDL_RWread(file, buf, 1, fsize) != fsize)
    {
        SDL_RWclose(file);
        return NULL;
    }
    g_LastFileSize = fsize;
    SDL_RWclose(file);
    return buf;
}

i32 FileSystem::CheckFileExists(const char *file)
{
    SDL_RWops *fp;

    fp = SDL_RWFromFile(file, "rb");
    if (fp)
    {
        SDL_RWclose(fp);
        return true;
    }
    return false;
}

i32 FileSystem::WriteDataToFile(const char *filename, const void *out, u32 bytesToWrite)
{
    SDL_RWops *file;
    u32 bytesWritten;

    file = SDL_RWFromFile(filename, "wb");
    if (!file)
    {
        Supervisor::DebugPrint("error : %s write error\n", filename);
        return -1;
    }

    bytesWritten = SDL_RWwrite(file, out, 1, bytesToWrite);
    if (bytesToWrite != bytesWritten)
    {
        SDL_RWclose(file);
        Supervisor::DebugPrint("error : %s write error\n", filename);
        return -2;
    }
    SDL_RWclose(file);
    Supervisor::DebugPrint("%s write ...\n", filename);
    return 0;
}

std::string FileSystem::GetPrefPath(const char* filename) {
    #if defined(__EMSCRIPTEN__)
        return std::string("/savesth07/") + filename;
    #elif defined(__ANDROID__) || defined(__IPHONEOS__)
        static char* prefPath = SDL_GetPrefPath("TeamShanghaiAlice", "th07");
        if (prefPath) {
            return std::string(prefPath) + filename;
        }
        return std::string(filename);
    #else
        return std::string(filename);
    #endif
}
