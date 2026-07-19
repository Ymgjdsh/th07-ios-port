#include <SDL2/SDL.h>
#include <cstdio>

// pull in gameerrorcontext::flush before anmmanager::releasesurfaces
#include "AnmManager.hpp"
#include "Chain.hpp"
#include "Controller.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "GameManager.hpp"
#include "GameWindow.hpp"
#include "ResultScreen.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "Touch.hpp"
#include "ZunResult.hpp"
#include "dxutil.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void main_loop();
bool cleanup();
bool stop();

static i32 renderRes = RENDER_RESULT_KEEP_RUNNING;

void AnmManager::TakeScreenshotIfRequested()
{
    if (this->screenshotTextureId >= 0)
    {
        TakeScreenshot(this->screenshotTextureId, this->screenshotSrcLeft, this->screenshotSrcTop,
                       this->screenshotSrcWidth, this->screenshotSrcHeight, this->screenshotDstLeft,
                       this->screenshotDstTop, this->screenshotDstWidth, this->screenshotDstHeight);
        this->screenshotTextureId = -1;
    }
}

void main_loop()
{
    SDL_Event e;

    while (SDL_PollEvent(&e))
    {
        switch (e.type)
        {
        case SDL_WINDOWEVENT:
            switch (e.window.event)
            {
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                g_GameWindow.isAppActive = 1;
                g_GameWindow.isAppInactive = 0;
                if (!g_Supervisor.cfg.windowed)
                {
                    SDL_ShowCursor(SDL_DISABLE);
                }
                break;
            case SDL_WINDOWEVENT_FOCUS_LOST:
                g_GameWindow.isAppActive = 0;
                g_GameWindow.isAppInactive = 1;
                SDL_ShowCursor(SDL_ENABLE);
                break;
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!g_Supervisor.controller)
            {
                g_Supervisor.controller = SDL_GameControllerOpen(e.cdevice.which);
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_Supervisor.controller)
            {
                SDL_Joystick *joy = SDL_GameControllerGetJoystick(g_Supervisor.controller);

                if (SDL_JoystickInstanceID(joy) == e.cdevice.which)
                {
                    SDL_GameControllerClose(g_Supervisor.controller);
                    g_Supervisor.controller = nullptr;
                }
            }
            break;
        case SDL_FINGERDOWN:
            Touch::FingerDown(e.tfinger);
            break;
        case SDL_FINGERUP:
            Touch::FingerUp(e.tfinger);
            break;
        case SDL_FINGERMOTION:
            Touch::FingerMotion(e.tfinger);
            break;
        case SDL_QUIT:
            g_GameWindow.isAppClosing = true;
            break;
        }
    }

    renderRes = g_GameWindow.Render();
    if (renderRes != RENDER_RESULT_KEEP_RUNNING)
    {
        g_GameWindow.isAppClosing = true;
#ifdef __EMSCRIPTEN__
        cleanup();
        emscripten_cancel_main_loop();
#endif
    }
    g_Supervisor.flags = g_Supervisor.flags & 0xffffffef;
}

bool cleanup()
{
    if (g_GameManager.plst.base.magic != 0)
    {
        ResultScreen::RegisterChain(2);
    }
    g_Chain.Release();
    while (g_SoundPlayer.ProcessQueues())
        ;
    return stop();
}

bool stop()
{
    g_SoundPlayer.Release();
    delete g_AnmManager;
    g_AnmManager = NULL;

    SAFE_DELETE(g_Supervisor.gfxDevice);
    if (g_GameWindow.window)
    {
        SDL_DestroyWindow(g_GameWindow.window);
        g_GameWindow.window = NULL;
    }
    SDL_ShowCursor(SDL_ENABLE);
    if (renderRes == RENDER_RESULT_EXIT_ERROR)
    {
        g_GameErrorContext.m_BufferEnd = g_GameErrorContext.m_Buffer;
        *g_GameErrorContext.m_BufferEnd = '\0';
        g_GameErrorContext.Log("再起動を要するオプションが変更されたので再起動します\n");
        return false;
    }
    FileSystem::WriteDataToFile(FileSystem::GetPrefPath("th07.cfg").c_str(), &g_Supervisor.cfg, sizeof(GameConfiguration));
    g_GameErrorContext.Flush();
    return true;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (g_Supervisor.LoadConfig(FileSystem::GetPrefPath("th07.cfg").c_str()) != ZUN_SUCCESS)
    {
        stop();
        return 0;
    }

    GameWindow::ChecksumExecutable();
    g_GameWindow.frequency = SDL_GetPerformanceFrequency();

start:
    if (GameWindow::CreateGameWindow())
    {
        stop();
        return 0;
    }

    if (GameWindow::InitInterface())
    {
        stop();
        return 0;
    }

    if (GameWindow::InitRendering())
    {
        stop();
        return 0;
    }

    g_SoundPlayer.InitializeSound();
    Controller::ResetKeyboard();
    g_AnmManager = new AnmManager();
    if (!g_Supervisor.cfg.windowed)
    {
        SDL_ShowCursor(SDL_DISABLE);
    }
    renderRes = g_Supervisor.RegisterChain();
    if (renderRes != ZUN_SUCCESS)
    {
        if (renderRes == ZUN_ERROR)
        {
            cleanup();
            return 0;
        }
        renderRes = RENDER_RESULT_EXIT_ERROR;
        if (!cleanup())
        {
            goto start;
        }
    }
    renderRes = RENDER_RESULT_KEEP_RUNNING;
    g_GameWindow.curFrame = -30;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop, 0, true);
#else
    while (!g_GameWindow.isAppClosing)
    {
        main_loop();
    }
#endif

    if (!cleanup())
    {
        goto start;
    }

    return 0;
}
