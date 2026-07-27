#include "GameWindow.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "AnmManager.hpp"
#include "Chain.hpp"
#include "Controller.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "ScreenEffect.hpp"
#include "SoundPlayer.hpp"
#include "Stage.hpp"
#include "Supervisor.hpp"
#include "graphics/Gles.hpp"
#include "graphics/ZunGraphics.hpp"

GameWindow g_GameWindow;
i32 g_FrameCount;
f64 g_LastFrameTime;
u64 g_LastPerfCounter;

static GfxInit g_RenderingBackends[] = {
    GlesGraphics::Init,
};

void GameWindow::Present()
{
    char snapshotPath[252];
    i32 i;

    g_Supervisor.gfxDevice->SwapBuffers();

    if (WAS_PRESSED_RAW(TH_BUTTON_HOME))
    {
        std::filesystem::create_directory(FileSystem::GetPrefPath("snapshot"));
        for (i = 0; i < 1000; i++)
        {
            sprintf(snapshotPath, "snapshot/th%.3d.bmp", i);
            if (FileSystem::CheckFileExists(snapshotPath) == 0)
            {
                break;
            }
        }
        if (i < 1000)
        {
            g_Supervisor.SnapshotScreen(snapshotPath);
        }
    }
    if (g_Supervisor.renderSkipFrames != 0)
    {
        g_Supervisor.renderSkipFrames--;
    }
}

RenderResult GameWindow::Render()
{
    f64 perfDiff;
    u64 perfCounter;
    i32 chainRes;

    if (!this->isAppActive)
    {
#ifndef __EMSCRIPTEN__
        SDL_WaitEventTimeout(NULL, 1000);
#endif
        return RENDER_RESULT_KEEP_RUNNING;
    }

    if (!g_Supervisor.VsyncEnabled())
    {
        g_Supervisor.viewport.x = 0;
        g_Supervisor.viewport.y = 0;
        g_Supervisor.viewport.width = 640;
        g_Supervisor.viewport.height = 480;
        g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);

        chainRes = g_Chain.RunCalcChain();
        g_SoundPlayer.ProcessQueues();

        if (!chainRes)
        {
            return RENDER_RESULT_EXIT_SUCCESS;
        }
        if (chainRes == -1)
        {
            return RENDER_RESULT_EXIT_ERROR;
        }

        g_Supervisor.gfxDevice->BeginFrame();
        g_AnmManager->ResetVertexBuffer();
        g_Supervisor.fogEnabled = 255;
        g_Supervisor.DisableFog();
        g_Chain.RunDrawChain();
        g_AnmManager->Flush();
        g_Supervisor.gfxDevice->BindTexture({0});
        g_Supervisor.gfxDevice->EndFrame();

        Present();
        this->curFrame = 0;
        g_FrameCount++;

        return RENDER_RESULT_KEEP_RUNNING;
    }

    if (this->curFrame == 0)
    {
    begin_loop:
        if ((i32)g_Supervisor.cfg.frameskipConfig <= (i32)this->curFrame)
        {
            g_Supervisor.gfxDevice->BeginFrame();
            g_AnmManager->ResetVertexBuffer();
            g_Supervisor.fogEnabled = 255;
            g_Supervisor.DisableFog();
            g_Chain.RunDrawChain();
            g_AnmManager->Flush();
            g_Supervisor.gfxDevice->BindTexture({0});
            g_Supervisor.gfxDevice->EndFrame();
        }

        g_AnmManager->Flush();
        g_Supervisor.viewport.x = 0;
        g_Supervisor.viewport.y = 0;
        g_Supervisor.viewport.width = 640;
        g_Supervisor.viewport.height = 480;
        g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);

        chainRes = g_Chain.RunCalcChain();
        g_SoundPlayer.ProcessQueues();

        if (!chainRes)
        {
            return RENDER_RESULT_EXIT_SUCCESS;
        }
        if (chainRes == -1)
        {
            return RENDER_RESULT_EXIT_ERROR;
        }

        this->curFrame++;
    }

    perfCounter = SDL_GetPerformanceCounter();
    perfDiff = (f64)(perfCounter - g_LastPerfCounter) / (f64)g_GameWindow.frequency;

    if (perfDiff < 0.0)
    {
        g_LastPerfCounter = perfCounter;
    }

    if (perfDiff >= (1.0 / 60.0) || g_GameWindow.usesRelativePath)
    {
        u64 frameTicks = g_GameWindow.frequency / 60.0;

        while (perfDiff >= (1.0 / 60.0))
        {
            g_LastPerfCounter += frameTicks;
            perfDiff -= (1.0 / 60.0);
        }

        if ((i32)g_Supervisor.cfg.frameskipConfig < (i32)this->curFrame)
        {
            Present();
            this->curFrame = 0;
            g_FrameCount++;
        }
        else
        {
            goto begin_loop;
        }
    }
    else
    {
        SDL_Delay(1);
    }

    return RENDER_RESULT_KEEP_RUNNING;
}

ZunResult GameWindow::InitInterface()
{
    for (auto gfxInit : g_RenderingBackends)
    {
        g_Supervisor.gfxDevice = gfxInit();
        if (g_Supervisor.gfxDevice)
        {
            g_Supervisor.flags |= 2;
            g_Supervisor.lockableBackBuffer = 1;
            return ZUN_SUCCESS;
        }
    }

    g_GameErrorContext.Fatal("Direct3D オブジェクトは何故か作成出来なかった\n");
    return ZUN_ERROR;
}

ZunResult GameWindow::CreateGameWindow()
{
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        g_GameErrorContext.Fatal("Direct3D オブジェクトは何故か作成出来なかった\n");
        return ZUN_ERROR;
    }

    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props)
    {
        Supervisor::DebugPrint("SDL_CreateProperties failed: %s\n", SDL_GetError());
        return ZUN_ERROR;
    }

    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                          "東方妖々夢　〜 Perfect Cherry Blossom. ver 1.00b");
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
#if defined(__APPLE__) && TARGET_OS_IPHONE
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
#elif !defined(__EMSCRIPTEN__)
    if (!g_Supervisor.cfg.windowed)
    {
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
    }
    else
    {
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 640);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 480);
    }
#endif

    g_GameWindow.isAppActive = 1;
    g_LastPerfCounter = SDL_GetPerformanceCounter();

#ifdef USING_GL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);

    g_GameWindow.window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!g_GameWindow.window)
    {
        Supervisor::DebugPrint("sdl window create failed: %s\n", SDL_GetError());
        return ZUN_ERROR;
    }

#if defined(__APPLE__) && TARGET_OS_IPHONE
    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.refresh_rate = 60.0f;
    SDL_SetWindowDisplayMode(g_GameWindow.window, &mode);
#endif

    SDL_ShowWindow(g_GameWindow.window);
    SDL_SyncWindow(g_GameWindow.window);

    SDL_RaiseWindow(g_GameWindow.window);
    return ZUN_SUCCESS;
}

ZunResult GameWindow::InitRendering()
{
    ZunVec3 pEye;
    ZunVec3 pAt;
    ZunVec3 pUp;
    f32 fov;
    f32 aspectRatio;
    f32 halfWidth;
    f32 halfHeight;
    f32 halfCameraDistance;

    halfWidth = 320.0f;
    halfHeight = 240.0f;
    aspectRatio = 1.3333334f;
    fov = 0.5235988f;
    halfCameraDistance = halfHeight / tanf(fov / 2.0f);
    pUp.x = 0.0f;
    pUp.y = 1.0f;
    pUp.z = 0.0f;
    pAt.x = halfWidth;
    pAt.y = -halfHeight;
    pAt.z = 0.0f;
    pEye.x = halfWidth;
    pEye.y = -halfHeight;
    pEye.z = -halfCameraDistance;
    g_Supervisor.viewMatrix.LookAtLH(&pEye, &pAt, &pUp);
    g_Supervisor.projectionMatrix.PerspectiveFovLH(fov, aspectRatio, 100.0f, 10000.0f);
    g_Supervisor.viewProjectionMatrix = g_Supervisor.viewMatrix * g_Supervisor.projectionMatrix;

    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_VIEW, g_Supervisor.viewMatrix);
    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_PROJECTION, g_Supervisor.projectionMatrix);

    g_Supervisor.viewport.x = 0;
    g_Supervisor.viewport.y = 0;
    g_Supervisor.viewport.width = 640;
    g_Supervisor.viewport.height = 480;
    g_Supervisor.viewport.minZ = 0.0f;
    g_Supervisor.viewport.maxZ = 1.0f;
    g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);

    ResetRenderState();
    ScreenEffect::SetViewport(0xff000000);
    g_Supervisor.lastFrameTime = 0;
    g_Supervisor.cfg.colorMode16bit = 0;

    return ZUN_SUCCESS;
}

void GameWindow::ResetRenderState()
{
    ZunColor fogColor;

    if (!g_Supervisor.cfg.disableZBuffer)
    {
        g_Supervisor.gfxDevice->Enable(CAPS_DEPTH_TEST);
    }
    else
    {
        g_Supervisor.gfxDevice->Disable(CAPS_DEPTH_TEST);
    }

    g_Supervisor.gfxDevice->Enable(CAPS_BLEND);
    g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ALPHA);
    g_Supervisor.gfxDevice->SetDepthFunc(DEPTH_FUNC_ALWAYS);
    g_Supervisor.gfxDevice->Enable(CAPS_ALPHA_TEST);
    g_Supervisor.gfxDevice->SetAlphaTestRef(4);

    if (!g_Supervisor.cfg.disableFog)
    {
        g_Supervisor.gfxDevice->Enable(CAPS_FOG);
    }
    else
    {
        g_Supervisor.gfxDevice->Disable(CAPS_FOG);
    }

    fogColor.color = 0xffa0a0a0;
    g_Supervisor.gfxDevice->SetFogColor(fogColor);
    g_Supervisor.gfxDevice->SetFogRange(1000.0f, 5000.0f);

    g_Supervisor.gfxDevice->SetTextureFilter();
    if (g_AnmManager)
    {
        g_AnmManager->SetBlendMode(255);
        g_AnmManager->SetColorOp(255);
        g_AnmManager->SetVertexShader(255);
        g_AnmManager->SetTexture(0);
        g_AnmManager->SetCameraMode(255);
    }
    g_Stage.renderStateWasReset = 1;
}

i32 GameWindow::ChecksumExecutable()
{
    // the game uses exechecksum and exesize to write to replay and score files about the program
    // that produced that file, and in the original executable those are compared to values in the
    // verfile to check if they're "good" untampered files. obviously it's not gonna match, so we
    // just return these hardcoded values.
    g_Supervisor.exeSize = 650752;
    return g_Supervisor.exeChecksum = 0xaec5445c;
}
