#include "GameWindow.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "AnmManager.hpp"
#include "BulletManager.hpp"
#include "Chain.hpp"
#include "Controller.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "GameManager.hpp"
#include "EffectManager.hpp"
#include "MobileDiagnostics.hpp"
#include "MobileUi.hpp"
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
f32 g_RenderAlpha = 1.0f;
bool g_SuppressAnmAdvance;

namespace
{
struct PerformanceAccumulator
{
    u64 windowStart = 0;
    u64 calcNs = 0;
    u64 drawNs = 0;
    u64 presentNs = 0;
    u64 frameNs = 0;
    u64 uploadBytes = 0;
    u32 frames = 0;
    u32 updates = 0;
    u32 drawCalls = 0;
    u32 uploads = 0;
    u32 textureBinds = 0;
    u32 stateChanges = 0;
    u32 redundantStateCalls = 0;
    u64 anmFlushes = 0;
    u64 anmStateChanges = 0;
    u64 scriptsExecuted = 0;
    u64 scriptTicks = 0;
};

PerformanceAccumulator g_Performance;

f64 NsToAverageMs(u64 nanoseconds, u32 count)
{
    return count ? (f64)nanoseconds / 1000000.0 / (f64)count : 0.0;
}

void RecordPerformance(u64 calcNs, u64 drawNs, u64 presentNs, u64 frameNs, u32 updates,
                       u32 scriptsExecuted, u32 scriptTicks)
{
    const u64 now = SDL_GetTicksNS();
    if (!g_Performance.windowStart) g_Performance.windowStart = now;
    g_Performance.calcNs += calcNs;
    g_Performance.drawNs += drawNs;
    g_Performance.presentNs += presentNs;
    g_Performance.frameNs += frameNs;
    g_Performance.updates += updates;
    g_Performance.frames++;
    g_Performance.scriptsExecuted += scriptsExecuted;
    g_Performance.scriptTicks += scriptTicks;
    if (g_AnmManager)
    {
        g_Performance.anmFlushes += g_AnmManager->flushesThisFrame;
        g_Performance.anmStateChanges += g_AnmManager->renderStateChangesThisFrame;
    }

    GlesGraphics *gles = g_Supervisor.gfxDevice &&
                                 g_Supervisor.gfxDevice->GetType() == RENDERER_OPENGLES
                             ? static_cast<GlesGraphics *>(g_Supervisor.gfxDevice)
                             : nullptr;
    if (gles)
    {
        const GlesFrameStats &stats = gles->GetFrameStats();
        g_Performance.drawCalls += stats.drawCalls;
        g_Performance.uploads += stats.bufferUploads;
        g_Performance.uploadBytes += stats.bufferUploadBytes;
        g_Performance.textureBinds += stats.textureBinds;
        g_Performance.stateChanges += stats.blendChanges + stats.viewportChanges +
                                      stats.depthChanges;
        g_Performance.redundantStateCalls += stats.redundantStateCalls;
    }

    if (now - g_Performance.windowStart < 2000000000ull) return;
    const f64 seconds = (f64)(now - g_Performance.windowStart) / 1000000000.0;
    const f64 fps = g_Performance.frames / seconds;
    const f64 ups = g_Performance.updates / seconds;
    const GlesFrameStats empty = {};
    const GlesFrameStats &stats = gles ? gles->GetFrameStats() : empty;
    MobileDiagnostics::Log(
        "perf/frame",
        "fps=%.1f ups=%.1f cpu_ms(frame=%.2f calc=%.2f draw=%.2f present=%.2f) "
        "gl/frame(draw=%.1f upload=%.1f kb=%.1f tex=%.1f state=%.1f skipped=%.1f) "
        "anm/frame(flush=%.1f state=%.1f scripts=%.1f ticks=%.1f) "
        "world(bullets=%d effects=%d slow=%d) "
        "drawable=%dx%d",
        fps, ups, NsToAverageMs(g_Performance.frameNs, g_Performance.frames),
        NsToAverageMs(g_Performance.calcNs, g_Performance.frames),
        NsToAverageMs(g_Performance.drawNs, g_Performance.frames),
        NsToAverageMs(g_Performance.presentNs, g_Performance.frames),
        (f64)g_Performance.drawCalls / g_Performance.frames,
        (f64)g_Performance.uploads / g_Performance.frames,
        (f64)g_Performance.uploadBytes / g_Performance.frames / 1024.0,
        (f64)g_Performance.textureBinds / g_Performance.frames,
        (f64)g_Performance.stateChanges / g_Performance.frames,
        (f64)g_Performance.redundantStateCalls / g_Performance.frames,
        (f64)g_Performance.anmFlushes / g_Performance.frames,
        (f64)g_Performance.anmStateChanges / g_Performance.frames,
        (f64)g_Performance.scriptsExecuted / g_Performance.frames,
        (f64)g_Performance.scriptTicks / g_Performance.frames,
        g_BulletManager.bulletCount, g_EffectManager.activeEffectsCount,
        g_GameManager.slowModeSlowActive, stats.drawableWidth, stats.drawableHeight);
    g_Performance = {};
    g_Performance.windowStart = now;
}
} // namespace

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
}

RenderResult GameWindow::Render()
{
    if (!this->isAppActive)
    {
#ifndef __EMSCRIPTEN__
        SDL_WaitEventTimeout(nullptr, 1000);
#endif
        return RENDER_RESULT_KEEP_RUNNING;
    }

    const f64 targetDt = 1.0 / 60.0;

    u64 currentPerfCounter = SDL_GetPerformanceCounter();
    if (g_LastPerfCounter == 0)
    {
        g_LastPerfCounter = currentPerfCounter;
    }

    f64 elapsed = (f64)(currentPerfCounter - g_LastPerfCounter) / (f64)g_GameWindow.frequency;
    g_LastPerfCounter = currentPerfCounter;

    if (elapsed < 0.0)
    {
        elapsed = 0.0;
    }
    if (elapsed > 0.1)
    {
        elapsed = 0.1;
    }

    this->accumulator += elapsed;

    const bool telemetry = MobileUi::IsPerformanceTelemetryEnabled();
    u64 calcNs = 0;
    i32 updateCount = 0;
    i32 chainRes = CHAIN_CALLBACK_RESULT_CONTINUE;
    bool updated = false;

    u64 timeToRender = SDL_GetTicksNS();

    while (this->accumulator >= targetDt)
    {
        const u64 calcStartNs = telemetry ? SDL_GetTicksNS() : 0;
        chainRes = g_Chain.RunCalcChain();
        g_SoundPlayer.ProcessQueues();
        if (telemetry) calcNs += SDL_GetTicksNS() - calcStartNs;
        updateCount++;

        if (chainRes == 0)
        {
            return RENDER_RESULT_EXIT_SUCCESS;
        }
        if (chainRes == -1)
        {
            return RENDER_RESULT_EXIT_ERROR;
        }

        this->accumulator -= targetDt;
        updated = true;
    }

    g_RenderAlpha = std::clamp((f32)(this->accumulator / targetDt), 0.0f, 1.0f);
    if (g_GameManager.isInPauseMenu || g_GameManager.isInRetryMenu)
    {
        g_RenderAlpha = 1.0f;
    }
    const u32 scriptsExecuted = telemetry && g_AnmManager
                                    ? (u32)g_AnmManager->scriptsExecutedThisFrame
                                    : 0;
    const u32 scriptTicks = telemetry && g_AnmManager ? (u32)g_AnmManager->scriptTicksThisFrame : 0;

    const u64 drawStartNs = telemetry ? SDL_GetTicksNS() : 0;
    g_Supervisor.gfxDevice->BeginFrame();
    g_AnmManager->ResetVertexBuffer();
    g_Supervisor.fogEnabled = 255;
    g_Supervisor.DisableFog();

    g_SuppressAnmAdvance = !updated;
    if (g_AnmManager)
    {
        g_AnmManager->offset =
            g_AnmManager->prevShakeOffset.Lerp(g_AnmManager->shakeOffset, g_RenderAlpha);
    }
    g_Chain.RunDrawChain();
    g_SuppressAnmAdvance = false;

    g_AnmManager->Flush();
    g_Supervisor.gfxDevice->BindTexture({0});
    g_Supervisor.gfxDevice->EndFrame();
    const u64 drawNs = telemetry ? SDL_GetTicksNS() - drawStartNs : 0;

    const u64 presentStartNs = telemetry ? SDL_GetTicksNS() : 0;
    Present();
    const u64 presentNs = telemetry ? SDL_GetTicksNS() - presentStartNs : 0;

    if (updated)
    {
        g_FrameCount++;
    }

    timeToRender = SDL_GetTicksNS() - timeToRender;

    constexpr u64 nsPerFrame = 1000000000 / 60;
    if (g_Supervisor.vsyncEnabled && timeToRender < nsPerFrame)
    {
        // should have gotten ACTUAL vsync then
        SDL_DelayNS(nsPerFrame - timeToRender);
    }

    if (telemetry)
    {
        RecordPerformance(calcNs, drawNs, presentNs, timeToRender, (u32)updateCount,
                          scriptsExecuted, scriptTicks);
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
    // The game is rendered at its native 640x480 resolution. A Retina drawable only
    // multiplies the final full-screen composition cost and can stall effect-heavy scenes.
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, false);
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
