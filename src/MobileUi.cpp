#include "MobileUi.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Controller.hpp"
#include "FileSystem.hpp"
#include "GameManager.hpp"
#include "GameWindow.hpp"
#include "Gui.hpp"
#include "MobileDiagnostics.hpp"
#include "Online.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "Touch.hpp"
#include "graphics/Gles.hpp"

namespace
{
constexpr u32 kConfigMagic = 0x37424f4d; // MOB7
constexpr u32 kConfigVersion = 6;
constexpr i32 kMaxTouches = 10;
constexpr i32 kActionButtonCount = 4;
constexpr i32 kLayoutOrientationCount = 2;
constexpr f32 kPi = 3.14159265358979323846f;
constexpr size_t kUiVboCapacity = 64 * 1024;

bool UsesOpenGlOnlinePanel()
{
#if defined(TH07_IOS)
    return false;
#else
    return Online::IsMenuOpen();
#endif
}

enum ControlMode
{
    CONTROL_JOYSTICK = 0,
    CONTROL_DRAG = 1,
    CONTROL_HYBRID = 2,
};

enum ActionId
{
    ACTION_SHOOT,
    ACTION_BOMB,
    ACTION_FOCUS,
    ACTION_PAUSE,
};

enum TouchKind
{
    TOUCH_NONE,
    TOUCH_JOYSTICK,
    TOUCH_ACTION,
    TOUCH_PANEL,
    TOUCH_MENU_TAP,
    TOUCH_OVERLAY_TAP,
    TOUCH_LAYOUT_CONTROL,
};

struct NormalizedLayout
{
    f32 joystickX;
    f32 joystickY;
    f32 actionX[kActionButtonCount];
    f32 actionY[kActionButtonCount];
};

struct MobileConfig
{
    u32 magic;
    u32 version;
    i32 controlMode;
    f32 opacity;
    f32 scale;
    f32 dragSensitivity;
    u8 showButtons;
    u8 lowEffects;
    u8 disableBackground;
    u8 showFps;
    u8 bgmEnabled;
    u8 sfxEnabled;
    u8 reserved[9];
    u8 autoBomb;
    u8 shootToggleMode;
    u8 focusToggleMode;
    u8 layoutCustomized[kLayoutOrientationCount];
    // Stored inverted so old v4 configs keep developer tools enabled.
    u8 developerDisabled;
    u8 reservedV4[3];
    NormalizedLayout layouts[kLayoutOrientationCount];
};

struct MobileConfigV5
{
    u32 magic;
    u32 version;
    i32 controlMode;
    f32 opacity;
    f32 scale;
    f32 dragSensitivity;
    u8 showButtons;
    u8 lowEffects;
    u8 disableBackground;
    u8 showFps;
    u8 bgmEnabled;
    u8 sfxEnabled;
    u8 reserved[10];
    u8 shootToggleMode;
    u8 focusToggleMode;
    u8 layoutCustomized[kLayoutOrientationCount];
    u8 developerDisabled;
    u8 reservedV4[3];
    NormalizedLayout layouts[kLayoutOrientationCount];
};

struct MobileConfigV4
{
    u32 magic;
    u32 version;
    i32 controlMode;
    f32 opacity;
    f32 scale;
    f32 dragSensitivity;
    u8 showButtons;
    u8 lowEffects;
    u8 disableBackground;
    u8 showFps;
    u8 bgmEnabled;
    u8 sfxEnabled;
    u8 reserved[10];
    u8 shootToggleMode;
    u8 bombToggleMode;
    u8 layoutCustomized[kLayoutOrientationCount];
    u8 developerDisabled;
    u8 reservedV4[3];
    NormalizedLayout layouts[kLayoutOrientationCount];
};

static_assert(sizeof(MobileConfig) == sizeof(MobileConfigV5),
              "mobile.cfg v5/v6 migration requires identical record sizes");
static_assert(sizeof(MobileConfig) == sizeof(MobileConfigV4),
              "mobile.cfg v4/v6 migration requires identical record sizes");

struct MobileConfigV3
{
    u32 magic;
    u32 version;
    i32 controlMode;
    f32 opacity;
    f32 scale;
    f32 dragSensitivity;
    u8 showButtons;
    u8 lowEffects;
    u8 disableBackground;
    u8 showFps;
    u8 bgmEnabled;
    u8 sfxEnabled;
    u8 reserved[10];
};

struct MobileConfigV2
{
    u32 magic;
    u32 version;
    i32 controlMode;
    f32 opacity;
    f32 scale;
    u8 showButtons;
    u8 lowEffects;
    u8 disableBackground;
    u8 showFps;
    u8 bgmEnabled;
    u8 sfxEnabled;
    u8 reserved[10];
};

struct CircleButton
{
    ActionId id;
    f32 x;
    f32 y;
    f32 radius;
    u16 bits;
    const char *label;
};

struct TouchOwner
{
    bool active;
    SDL_FingerID finger;
    TouchKind kind;
    i32 index;
    f32 startX;
    f32 startY;
    f32 x;
    f32 y;
    u64 startTime;
    bool toggle;
};

struct Rect
{
    f32 x;
    f32 y;
    f32 w;
    f32 h;
};

MobileConfig g_Config = {};
CircleButton g_Actions[kActionButtonCount] = {};
TouchOwner g_Touches[kMaxTouches] = {};
u16 g_HeldBits = 0;
u16 g_PulseBits = 0;
// A pulse generated while a menu is visible must not survive the menu ->
// gameplay transition.  Keep the origin mode alongside the bits so the
// consumer can reject that exact stale edge without affecting a legitimate
// gameplay pause pulse.
bool g_PulseContextSet = false;
bool g_PulseQueuedInBattle = false;
bool g_SettingsOpen = false;
bool g_SettingsPerformancePage = false;
bool g_DeveloperOpen = false;
bool g_LayoutEditMode = false;
bool g_ShootLatched = false;
bool g_FocusLatched = false;
bool g_DialogueInputActive = false;
bool g_MainMenuActive = false;
bool g_MainMenuHome = false;
MobileUi::MenuTouchAction g_MainMenuTouchAction = MobileUi::MENU_TOUCH_NONE;
f32 g_MainMenuTapX = 0.0f;
f32 g_MainMenuTapY = 0.0f;
f32 g_MainMenuTouchDelta = 0.0f;
MobileUi::OverlayTouchAction g_OverlayTouchAction = MobileUi::OVERLAY_TOUCH_NONE;
f32 g_OverlayTapX = 0.0f;
f32 g_OverlayTapY = 0.0f;
bool g_ConfigDirty = false;
u64 g_LastConfigSave = 0;
u64 g_FpsWindowStart = 0;
i32 g_FpsFrames = 0;
i32 g_Fps = 0;
i32 g_LastWidth = 640;
i32 g_LastHeight = 480;
f32 g_JoystickX = 0.0f;
f32 g_JoystickY = 0.0f;
f32 g_JoystickBaseX = 0.0f;
f32 g_JoystickBaseY = 0.0f;
f32 g_JoystickRadius = 60.0f;
Rect g_SettingsButton = {};
Rect g_PerformanceButton = {};
Rect g_MenuBackButton = {};
Rect g_MenuConfirmButton = {};
Rect g_DeveloperButton = {};
Rect g_LayoutSaveButton = {};
Rect g_LayoutResetButton = {};
Rect g_LayoutCancelButton = {};
NormalizedLayout g_LayoutEditBackup = {};
u8 g_LayoutCustomizedBackup = 0;
GLuint g_Program = 0;
GLuint g_Vao = 0;
GLuint g_Vbo = 0;
GLint g_ScreenSizeUniform = -1;
GLint g_ColorUniform = -1;
size_t g_UiVboAllocated = 0;

struct UiDrawCommand
{
    GLint first;
    GLsizei count;
    GLenum mode;
    f32 r;
    f32 g;
    f32 b;
    f32 a;
};

struct UiVertex
{
    f32 x;
    f32 y;
    f32 r;
    f32 g;
    f32 b;
    f32 a;
};

std::vector<UiVertex> g_UiVertices;
std::vector<UiDrawCommand> g_UiCommands;
std::vector<f32> g_TextScratch;

bool IsMobileBuild()
{
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    return true;
#else
    return false;
#endif
}

bool IsGameplay()
{
    return Touch::IsGameplayActive();
}

bool IsValidBattleOverlayScene()
{
    // notInMenu becomes false while the native pause/retry overlay is open.
    // The stage identity below is the stable guard; requiring notInMenu here
    // made every touch on the shared P1/P2 menu disappear after its first
    // update.
    return g_GameManager.globals &&
           g_GameManager.currentStage >= 1 && g_GameManager.currentStage <= 6 &&
           g_GameManager.framesThisStage > 0 && g_Supervisor.curState == 2 &&
           !g_GameManager.demo &&
           !g_GameManager.replay && !g_GameManager.finished;
}

bool IsGameplayOverlayScene()
{
    return g_GameManager.notInMenu || g_GameManager.isInPauseMenu || g_GameManager.isInRetryMenu;
}

void DefaultConfig()
{
    memset(&g_Config, 0, sizeof(g_Config));
    g_Config.magic = kConfigMagic;
    g_Config.version = kConfigVersion;
    g_Config.controlMode = CONTROL_HYBRID;
    g_Config.opacity = 0.58f;
    g_Config.scale = 1.0f;
    g_Config.dragSensitivity = 1.0f;
    g_Config.showButtons = 1;
    g_Config.bgmEnabled = 1;
    g_Config.sfxEnabled = 1;
}

int LayoutOrientation()
{
    return g_LastHeight > g_LastWidth ? 1 : 0;
}

void SetDefaultLayoutPositions(int orientation, i32 width, i32 height)
{
    if (orientation < 0 || orientation >= kLayoutOrientationCount) return;
    g_Config.layouts[orientation].joystickX = g_JoystickBaseX / std::max(1.0f, (f32)width);
    g_Config.layouts[orientation].joystickY = g_JoystickBaseY / std::max(1.0f, (f32)height);
    for (i32 i = 0; i < kActionButtonCount; ++i)
    {
        g_Config.layouts[orientation].actionX[i] = g_Actions[i].x / std::max(1.0f, (f32)width);
        g_Config.layouts[orientation].actionY[i] = g_Actions[i].y / std::max(1.0f, (f32)height);
    }
}

void ApplyCustomLayout(int orientation, i32 width, i32 height)
{
    if (orientation < 0 || orientation >= kLayoutOrientationCount ||
        !g_Config.layoutCustomized[orientation])
        return;
    const NormalizedLayout &layout = g_Config.layouts[orientation];
    const f32 minX = g_JoystickRadius + 8.0f;
    const f32 minY = g_JoystickRadius + 8.0f;
    if (!std::isfinite(layout.joystickX) || !std::isfinite(layout.joystickY)) return;
    g_JoystickBaseX = std::clamp(layout.joystickX * width, minX, (f32)width - minX);
    g_JoystickBaseY = std::clamp(layout.joystickY * height, minY, (f32)height - minY);
    for (i32 i = 0; i < kActionButtonCount; ++i)
    {
        if (!std::isfinite(layout.actionX[i]) || !std::isfinite(layout.actionY[i])) return;
        const f32 r = g_Actions[i].radius + 8.0f;
        g_Actions[i].x = std::clamp(layout.actionX[i] * width, r, (f32)width - r);
        g_Actions[i].y = std::clamp(layout.actionY[i] * height, r, (f32)height - r);
    }
}

void ApplyConfig()
{
    if (!std::isfinite(g_Config.opacity)) g_Config.opacity = 0.58f;
    if (!std::isfinite(g_Config.scale)) g_Config.scale = 1.0f;
    if (!std::isfinite(g_Config.dragSensitivity)) g_Config.dragSensitivity = 1.0f;
    g_Config.opacity = std::clamp(g_Config.opacity, 0.20f, 0.95f);
    g_Config.scale = std::clamp(g_Config.scale, 0.65f, 1.45f);
    g_Config.dragSensitivity = std::clamp(g_Config.dragSensitivity, 0.25f, 3.0f);
    g_Config.controlMode = std::clamp(g_Config.controlMode, 0, 2);
    g_Supervisor.cfg.effectQuality = g_Config.lowEffects ? QUALITY_WORST : QUALITY_BEAUTIFUL;
    g_Supervisor.cfg.disableFog = g_Config.lowEffects ? 1 : 0;
    g_Supervisor.cfg.playSounds = g_Config.sfxEnabled ? 1 : 0;
    if (g_Config.developerDisabled) g_DeveloperOpen = false;
}

void LoadConfig()
{
    DefaultConfig();
    const std::string path = FileSystem::GetPrefPath("mobile.cfg");
    FILE *file = fopen(path.c_str(), "rb");
    if (file)
    {
        u32 header[2] = {};
        if (fread(header, sizeof(header), 1, file) == 1 && header[0] == kConfigMagic)
        {
            rewind(file);
            if (header[1] == kConfigVersion)
            {
                MobileConfig loaded = {};
                if (fread(&loaded, sizeof(loaded), 1, file) == 1) g_Config = loaded;
            }
            else if (header[1] == 5)
            {
                MobileConfigV5 loaded = {};
                if (fread(&loaded, sizeof(loaded), 1, file) == 1)
                {
                    g_Config.controlMode = loaded.controlMode;
                    g_Config.opacity = loaded.opacity;
                    g_Config.scale = loaded.scale;
                    g_Config.dragSensitivity = loaded.dragSensitivity;
                    g_Config.showButtons = loaded.showButtons;
                    g_Config.lowEffects = loaded.lowEffects;
                    g_Config.disableBackground = loaded.disableBackground;
                    g_Config.showFps = loaded.showFps;
                    g_Config.bgmEnabled = loaded.bgmEnabled;
                    g_Config.sfxEnabled = loaded.sfxEnabled;
                    g_Config.shootToggleMode = loaded.shootToggleMode;
                    g_Config.focusToggleMode = loaded.focusToggleMode;
                    memcpy(g_Config.layoutCustomized, loaded.layoutCustomized,
                           sizeof(g_Config.layoutCustomized));
                    g_Config.developerDisabled = loaded.developerDisabled;
                    memcpy(g_Config.layouts, loaded.layouts, sizeof(g_Config.layouts));
                    g_ConfigDirty = true;
                    MobileDiagnostics::Log("mobile/config", "migrated v5 config to v6; auto bomb off");
                }
            }
            else if (header[1] == 4)
            {
                MobileConfigV4 loaded = {};
                if (fread(&loaded, sizeof(loaded), 1, file) == 1)
                {
                    g_Config.controlMode = loaded.controlMode;
                    g_Config.opacity = loaded.opacity;
                    g_Config.scale = loaded.scale;
                    g_Config.dragSensitivity = loaded.dragSensitivity;
                    g_Config.showButtons = loaded.showButtons;
                    g_Config.lowEffects = loaded.lowEffects;
                    g_Config.disableBackground = loaded.disableBackground;
                    g_Config.showFps = loaded.showFps;
                    g_Config.bgmEnabled = loaded.bgmEnabled;
                    g_Config.sfxEnabled = loaded.sfxEnabled;
                    g_Config.shootToggleMode = loaded.shootToggleMode;
                    g_Config.focusToggleMode = 0;
                    memcpy(g_Config.layoutCustomized, loaded.layoutCustomized,
                           sizeof(g_Config.layoutCustomized));
                    g_Config.developerDisabled = loaded.developerDisabled;
                    memcpy(g_Config.layouts, loaded.layouts, sizeof(g_Config.layouts));
                    g_ConfigDirty = true;
                    MobileDiagnostics::Log("mobile/config", "migrated v4 X toggle to v6 S toggle (off); auto bomb off");
                }
            }
            else if (header[1] == 2)
            {
                MobileConfigV2 loaded = {};
                if (fread(&loaded, sizeof(loaded), 1, file) == 1)
                {
                    g_Config.controlMode = loaded.controlMode;
                    g_Config.opacity = loaded.opacity;
                    g_Config.scale = loaded.scale;
                    g_Config.showButtons = loaded.showButtons;
                    g_Config.lowEffects = loaded.lowEffects;
                    g_Config.disableBackground = loaded.disableBackground;
                    g_Config.showFps = loaded.showFps;
                    g_Config.bgmEnabled = loaded.bgmEnabled;
                    g_Config.sfxEnabled = loaded.sfxEnabled;
                    g_ConfigDirty = true;
                    MobileDiagnostics::Log("mobile/config", "migrated v2 config to v6");
                }
            }
            else if (header[1] == 3)
            {
                MobileConfigV3 loaded = {};
                if (fread(&loaded, sizeof(loaded), 1, file) == 1)
                {
                    g_Config.controlMode = loaded.controlMode;
                    g_Config.opacity = loaded.opacity;
                    g_Config.scale = loaded.scale;
                    g_Config.dragSensitivity = loaded.dragSensitivity;
                    g_Config.showButtons = loaded.showButtons;
                    g_Config.lowEffects = loaded.lowEffects;
                    g_Config.disableBackground = loaded.disableBackground;
                    g_Config.showFps = loaded.showFps;
                    g_Config.bgmEnabled = loaded.bgmEnabled;
                    g_Config.sfxEnabled = loaded.sfxEnabled;
                    g_ConfigDirty = true;
                    MobileDiagnostics::Log("mobile/config", "migrated v3 config to v6");
                }
            }
        }
        fclose(file);
    }
    ApplyConfig();
    MobileDiagnostics::Log("mobile/config", "v=%u mode=%d opacity=%.2f scale=%.2f drag=%.2f low=%d bg=%d autoBomb=%d",
                           g_Config.version, g_Config.controlMode, g_Config.opacity, g_Config.scale,
                           g_Config.dragSensitivity, g_Config.lowEffects,
                           g_Config.disableBackground, g_Config.autoBomb);
}

void SaveConfig()
{
    const std::string path = FileSystem::GetPrefPath("mobile.cfg");
    FILE *file = fopen(path.c_str(), "wb");
    if (file)
    {
        fwrite(&g_Config, sizeof(g_Config), 1, file);
        fclose(file);
        g_ConfigDirty = false;
        g_LastConfigSave = SDL_GetTicks();
    }
}

void MarkConfigDirty()
{
    ApplyConfig();
    g_ConfigDirty = true;
}

void GetWindowSize(i32 &width, i32 &height)
{
    width = 640;
    height = 480;
    if (g_GameWindow.window)
    {
        SDL_GetWindowSize(g_GameWindow.window, &width, &height);
    }
}

void FingerToPixels(const SDL_TouchFingerEvent &event, f32 &x, f32 &y)
{
    i32 width, height;
    GetWindowSize(width, height);
    x = event.x * (f32)width;
    y = event.y * (f32)height;
}

TouchOwner *FindTouch(SDL_FingerID finger)
{
    for (TouchOwner &touch : g_Touches)
    {
        if (touch.active && touch.finger == finger)
        {
            return &touch;
        }
    }
    return nullptr;
}

TouchOwner *AllocateTouch(SDL_FingerID finger, TouchKind kind, f32 x, f32 y)
{
    if (TouchOwner *existing = FindTouch(finger))
    {
        return existing;
    }
    for (TouchOwner &touch : g_Touches)
    {
        if (!touch.active)
        {
            touch.active = true;
            touch.finger = finger;
            touch.kind = kind;
            touch.index = -1;
            touch.startX = touch.x = x;
            touch.startY = touch.y = y;
            touch.startTime = SDL_GetTicks();
            touch.toggle = false;
            return &touch;
        }
    }
    return nullptr;
}

i32 ActiveTouchCount()
{
    i32 count = 0;
    for (const TouchOwner &touch : g_Touches)
    {
        if (touch.active) ++count;
    }
    return count;
}

// A second finger is only a back gesture when it joins a still-fresh first
// tap.  The old code treated any additional finger (including a stale finger
// left over while a shared shell was waiting) as an immediate back action.
// That made retry/pause choices disappear or flash on the next frame.
bool IsFreshTwoFingerGesture(SDL_FingerID newFinger)
{
    if (ActiveTouchCount() != 1) return false;
    const u64 now = SDL_GetTicks();
    const f32 maxMovement = std::max(12.0f,
        (f32)std::min(g_LastWidth, g_LastHeight) * 0.04f);
    for (const TouchOwner &touch : g_Touches)
    {
        if (touch.active)
        {
            // SDL/iOS can replay a DOWN for an existing finger after a short
            // stall.  That is not a second finger and must not become a back
            // or pause gesture.
            if (touch.finger == newFinger) return false;
            const f32 dx = touch.x - touch.startX;
            const f32 dy = touch.y - touch.startY;
            return now - touch.startTime <= 500 &&
                   dx * dx + dy * dy <= maxMovement * maxMovement;
        }
    }
    return false;
}

void SuppressPendingTaps()
{
    g_MainMenuTouchAction = MobileUi::MENU_TOUCH_NONE;
    for (TouchOwner &touch : g_Touches)
    {
        if (touch.active &&
            (touch.kind == TOUCH_MENU_TAP || touch.kind == TOUCH_OVERLAY_TAP))
        {
            touch.index = 2;
        }
    }
}

bool Contains(const Rect &rect, f32 x, f32 y)
{
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

void BuildLayout(i32 width, i32 height)
{
    g_LastWidth = std::max(1, width);
    g_LastHeight = std::max(1, height);
    const bool portrait = height > width;
    const f32 shortSide = (f32)std::min(width, height);
    const f32 radius = std::clamp(shortSide * 0.064f * g_Config.scale, 30.0f, 82.0f);
    const f32 edge = std::max(12.0f, radius * 0.38f);
    const f32 bottom = portrait ? std::min((f32)height - radius - edge,
                                           (f32)height - radius * 1.12f)
                                : (f32)height - radius - edge;

    g_JoystickRadius = radius * 1.48f;
    g_JoystickBaseX = edge + g_JoystickRadius;
    g_JoystickBaseY = bottom - radius * 0.05f;
    const f32 actionX = (f32)width - edge - radius * 1.05f;
    const f32 actionY = bottom;

    g_Actions[ACTION_SHOOT] = {ACTION_SHOOT, actionX, actionY, radius * 1.06f,
                               TH_BUTTON_SHOOT, "Z"};
    g_Actions[ACTION_BOMB] = {ACTION_BOMB, actionX - radius * 1.65f,
                              actionY + radius * 0.05f, radius * 0.86f,
                              TH_BUTTON_BOMB, "X"};
    g_Actions[ACTION_FOCUS] = {ACTION_FOCUS, actionX - radius * 0.62f,
                               actionY - radius * 1.55f, radius * 0.84f,
                               TH_BUTTON_FOCUS, "S"};
    g_Actions[ACTION_PAUSE] = {ACTION_PAUSE, (f32)width - edge - radius * 0.56f,
                               edge + radius * 0.56f, radius * 0.52f,
                               TH_BUTTON_MENU, "II"};

    const f32 toolH = std::clamp(shortSide * 0.07f, 34.0f, 58.0f);
    const f32 toolW = toolH * 2.35f;
    g_SettingsButton = {edge, edge, toolW * 1.25f, toolH};
    g_PerformanceButton = {edge, edge + toolH * 1.18f, toolW * 1.25f, toolH};
    g_MenuBackButton = {edge, edge, toolH, toolH};
    g_MenuConfirmButton = {(f32)width - edge - toolH, edge, toolH, toolH};
    g_DeveloperButton = {(f32)width - edge - toolW, edge + toolH * 1.25f, toolW, toolH};
    const f32 layoutToolsWidth = toolW * 3.0f + edge * 2.0f;
    const f32 layoutToolsX = std::max(edge, ((f32)width - layoutToolsWidth) * 0.5f);
    g_LayoutSaveButton = {layoutToolsX, edge, toolW, toolH};
    g_LayoutResetButton = {layoutToolsX + toolW + edge, edge, toolW, toolH};
    g_LayoutCancelButton = {layoutToolsX + (toolW + edge) * 2.0f, edge, toolW, toolH};

    const int orientation = portrait ? 1 : 0;
    if (!g_Config.layoutCustomized[orientation])
    {
        SetDefaultLayoutPositions(orientation, width, height);
    }
    ApplyCustomLayout(orientation, width, height);

    MobileDiagnostics::Log("mobile/layout", "logical=%dx%d portrait=%d scale=%.2f joy=(%.0f,%.0f r%.0f)",
                           width, height, portrait, g_Config.scale, g_JoystickBaseX,
                           g_JoystickBaseY, g_JoystickRadius);
}

i32 HitAction(f32 x, f32 y)
{
    for (i32 i = kActionButtonCount - 1; i >= 0; --i)
    {
        const CircleButton &button = g_Actions[i];
        const f32 dx = x - button.x;
        const f32 dy = y - button.y;
        const f32 hitRadius = button.radius * 1.18f;
        if (dx * dx + dy * dy <= hitRadius * hitRadius)
        {
            return i;
        }
    }
    return -1;
}

bool IsInJoystickZone(f32 x, f32 y)
{
    const f32 dx = x - g_JoystickBaseX;
    const f32 dy = y - g_JoystickBaseY;
    const f32 hitRadius = g_JoystickRadius * 1.28f;
    return dx * dx + dy * dy <= hitRadius * hitRadius;
}

void UpdateJoystick(f32 x, f32 y)
{
    f32 dx = x - g_JoystickBaseX;
    f32 dy = y - g_JoystickBaseY;
    const f32 length = sqrtf(dx * dx + dy * dy);
    if (length > g_JoystickRadius && length > 0.0f)
    {
        dx *= g_JoystickRadius / length;
        dy *= g_JoystickRadius / length;
    }
    g_JoystickX = dx / g_JoystickRadius;
    g_JoystickY = dy / g_JoystickRadius;
}

void RebuildHeldBits()
{
    g_HeldBits = 0;
    bool joystickHeld = false;
    for (const TouchOwner &touch : g_Touches)
    {
        if (!touch.active)
        {
            continue;
        }
        if (touch.kind == TOUCH_ACTION && touch.index >= 0 && touch.index < kActionButtonCount)
        {
            if (!touch.toggle && touch.index != ACTION_PAUSE)
            {
                g_HeldBits |= g_Actions[touch.index].bits;
            }
        }
        else if (touch.kind == TOUCH_JOYSTICK)
        {
            joystickHeld = true;
        }
    }
    if (joystickHeld)
    {
        constexpr f32 deadzone = 0.20f;
        if (g_JoystickX < -deadzone) g_HeldBits |= TH_BUTTON_LEFT;
        if (g_JoystickX > deadzone) g_HeldBits |= TH_BUTTON_RIGHT;
        if (g_JoystickY < -deadzone) g_HeldBits |= TH_BUTTON_UP;
        if (g_JoystickY > deadzone) g_HeldBits |= TH_BUTTON_DOWN;
    }
    else
    {
        g_JoystickX = 0.0f;
        g_JoystickY = 0.0f;
    }
    if (g_Config.shootToggleMode && g_ShootLatched) g_HeldBits |= TH_BUTTON_SHOOT;
    if (g_Config.focusToggleMode && g_FocusLatched) g_HeldBits |= TH_BUTTON_FOCUS;
}

void ClearTouchState()
{
    memset(g_Touches, 0, sizeof(g_Touches));
    g_HeldBits = 0;
    g_PulseBits = 0;
    g_PulseContextSet = false;
    g_PulseQueuedInBattle = false;
    g_MainMenuTouchAction = MobileUi::MENU_TOUCH_NONE;
    g_OverlayTouchAction = MobileUi::OVERLAY_TOUCH_NONE;
    g_JoystickX = 0.0f;
    g_JoystickY = 0.0f;
    g_ShootLatched = false;
    g_FocusLatched = false;
}

void ClearPhysicalTouches()
{
    memset(g_Touches, 0, sizeof(g_Touches));
    g_PulseBits = 0;
    g_PulseContextSet = false;
    g_PulseQueuedInBattle = false;
    g_JoystickX = 0.0f;
    g_JoystickY = 0.0f;
    RebuildHeldBits();
}

Rect PanelRect()
{
    const f32 width = std::min((f32)g_LastWidth * 0.88f, 760.0f);
    const f32 height = std::min((f32)g_LastHeight * 0.86f, 680.0f);
    return {((f32)g_LastWidth - width) * 0.5f, ((f32)g_LastHeight - height) * 0.5f,
            width, height};
}

i32 SettingsRowAt(f32 x, f32 y)
{
    const Rect panel = PanelRect();
    const f32 top = panel.y + panel.h * 0.145f;
    const i32 count = g_SettingsPerformancePage ? 7 : 10;
    const f32 rowHeight = panel.h * (g_SettingsPerformancePage ? 0.104f : 0.084f);
    if (x < panel.x || x > panel.x + panel.w || y < top || y >= top + rowHeight * count)
    {
        return -1;
    }
    return (i32)((y - top) / rowHeight);
}

Rect SettingsRowRect(i32 row)
{
    const Rect panel = PanelRect();
    const f32 top = panel.y + panel.h * 0.145f;
    const f32 rowHeight = panel.h * (g_SettingsPerformancePage ? 0.104f : 0.084f);
    return {panel.x + panel.w * 0.06f, top + row * rowHeight,
            panel.w * 0.88f, rowHeight * 0.76f};
}

f32 SliderValueFromX(const Rect &line, f32 x, f32 minimum, f32 maximum)
{
    const f32 start = line.x + line.w * 0.56f;
    const f32 end = line.x + line.w * 0.94f;
    const f32 amount = std::clamp((x - start) / std::max(1.0f, end - start), 0.0f, 1.0f);
    return minimum + (maximum - minimum) * amount;
}

bool UpdateControlSlider(i32 row, f32 x)
{
    const Rect line = SettingsRowRect(row);
    switch (row)
    {
    case 2:
        g_Config.scale = SliderValueFromX(line, x, 0.65f, 1.45f);
        BuildLayout(g_LastWidth, g_LastHeight);
        break;
    case 3:
        g_Config.dragSensitivity = SliderValueFromX(line, x, 0.25f, 3.0f);
        break;
    case 4:
        g_Config.opacity = SliderValueFromX(line, x, 0.20f, 0.95f);
        break;
    default:
        return false;
    }
    MarkConfigDirty();
    return true;
}

i32 DeveloperRowAt(f32 x, f32 y)
{
    const Rect panel = PanelRect();
    const f32 top = panel.y + panel.h * 0.22f;
    const f32 rowHeight = panel.h * 0.13f;
    if (x < panel.x || x > panel.x + panel.w || y < top || y >= top + rowHeight * 5.0f)
    {
        return -1;
    }
    return (i32)((y - top) / rowHeight);
}

void ActivateSettingsRow(i32 row, f32 x)
{
    if (g_SettingsPerformancePage)
    {
        switch (row)
        {
        case 0: g_Config.lowEffects = !g_Config.lowEffects; break;
        case 1: g_Config.disableBackground = !g_Config.disableBackground; break;
        case 2: g_Config.showFps = !g_Config.showFps; break;
        case 3: g_Config.bgmEnabled = !g_Config.bgmEnabled; break;
        case 4: g_Config.sfxEnabled = !g_Config.sfxEnabled; break;
        case 5:
            g_Config.developerDisabled = !g_Config.developerDisabled;
            if (g_Config.developerDisabled) g_DeveloperOpen = false;
            break;
        case 6: g_SettingsOpen = false; break;
        default: return;
        }
        MarkConfigDirty();
        MobileDiagnostics::Log("mobile/performance", "row=%d low=%d bg=%d fps=%d bgm=%d sfx=%d dev=%d",
                               row, g_Config.lowEffects, g_Config.disableBackground,
                               g_Config.showFps, g_Config.bgmEnabled, g_Config.sfxEnabled,
                               !g_Config.developerDisabled);
        return;
    }
    switch (row)
    {
    case 0: g_Config.controlMode = (g_Config.controlMode + 1) % 3; break;
    case 1: g_Config.showButtons = !g_Config.showButtons; break;
    case 2:
    case 3:
    case 4: UpdateControlSlider(row, x); break;
    case 5:
        g_Config.shootToggleMode = !g_Config.shootToggleMode;
        g_ShootLatched = false;
        break;
    case 6:
        g_Config.focusToggleMode = !g_Config.focusToggleMode;
        g_FocusLatched = false;
        break;
    case 7:
        g_Config.autoBomb = !g_Config.autoBomb;
        break;
    case 8:
        g_LayoutEditMode = true;
        g_SettingsOpen = false;
        g_LayoutEditBackup = g_Config.layouts[LayoutOrientation()];
        g_LayoutCustomizedBackup = g_Config.layoutCustomized[LayoutOrientation()];
        ClearTouchState();
        break;
    case 9: g_SettingsOpen = false; break;
    default: return;
    }
    MarkConfigDirty();
    MobileDiagnostics::Log("mobile/settings", "row=%d mode=%d buttons=%d scale=%.2f drag=%.2f opacity=%.2f ztoggle=%d stoggle=%d autobomb=%d",
                           row, g_Config.controlMode, g_Config.showButtons, g_Config.scale,
                           g_Config.dragSensitivity, g_Config.opacity,
                           g_Config.shootToggleMode, g_Config.focusToggleMode, g_Config.autoBomb);
}

void ActivateDeveloperRow(i32 row)
{
    if (row == 4)
    {
        g_DeveloperOpen = false;
        return;
    }
    if (!IsGameplay() || !g_GameManager.globals)
    {
        MobileDiagnostics::Log("mobile/dev", "rejected row=%d gameplay=%d globals=%d", row,
                               IsGameplay(), g_GameManager.globals != nullptr);
        return;
    }
    if (Online::IsNetworkSession())
    {
        // DEV is a gameplay mutation. Queue it into the same delayed input
        // frame as movement so both peers apply it once at the same point.
        // A local write here would be overwritten by the host snapshot on
        // the next tick, which is the source of the visible flash/revert.
        if (row <= 3)
        {
            Online::QueueDeveloperCommand((u8)row);
            g_DeveloperOpen = false;
        }
        return;
    }
    const u8 playerId = (u8)Online::GetLocalPlayerSlot();
    switch (row)
    {
    case 0:
        SetPlayerLives(playerId, std::min(8, GetPlayerLives(playerId) + 1));
        g_Gui.lifeDisplayUpdateFrames = 2;
        break;
    case 1:
        SetPlayerBombs(playerId, std::min(8, GetPlayerBombs(playerId) + 1));
        g_Gui.bombDisplayUpdateFrames = 2;
        break;
    case 2:
        SetPlayerPower(playerId, 128);
        g_Gui.powerDisplayUpdateFrames = 2;
        break;
    case 3:
        SetPlayerLives(playerId, 8);
        SetPlayerBombs(playerId, 8);
        SetPlayerPower(playerId, 128);
        g_Gui.lifeDisplayUpdateFrames = 2;
        g_Gui.bombDisplayUpdateFrames = 2;
        g_Gui.powerDisplayUpdateFrames = 2;
        break;
    default: return;
    }
    g_GameManager.RegenerateGameIntegrityCsum();
    MobileDiagnostics::Log("mobile/dev", "applied p=%d row=%d lives=%d bombs=%d power=%d", playerId,
                           row, GetPlayerLives(playerId), GetPlayerBombs(playerId),
                           GetPlayerPower(playerId));
}

GLuint CompileShader(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[512] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        MobileDiagnostics::Log("mobile/render", "shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void EnsureRenderer()
{
    if (g_Program) return;
    const char *vs = "#version 300 es\nlayout(location=0) in vec2 aPos; layout(location=1) in vec4 aColor; uniform vec2 uScreen; out vec4 vColor;"
                      "void main(){vec2 p=vec2(aPos.x/uScreen.x*2.0-1.0,1.0-aPos.y/uScreen.y*2.0);"
                      "gl_Position=vec4(p,0.0,1.0);vColor=aColor;}";
    const char *fs = "#version 300 es\nprecision mediump float; in vec4 vColor;"
                     "out vec4 color; void main(){color=vColor;}";
    const GLuint vert = CompileShader(GL_VERTEX_SHADER, vs);
    const GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fs);
    if (!vert || !frag) return;
    g_Program = glCreateProgram();
    glAttachShader(g_Program, vert);
    glAttachShader(g_Program, frag);
    glLinkProgram(g_Program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    glGenVertexArrays(1, &g_Vao);
    glGenBuffers(1, &g_Vbo);
    glBindVertexArray(g_Vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_Vbo);
    glBufferData(GL_ARRAY_BUFFER, kUiVboCapacity, nullptr, GL_STREAM_DRAW);
    g_UiVboAllocated = kUiVboCapacity;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                          (void *)(sizeof(f32) * 2));
    glBindVertexArray(0);
    g_ScreenSizeUniform = glGetUniformLocation(g_Program, "uScreen");
    g_ColorUniform = -1;
}

void DrawVertices(const f32 *vertices, i32 count, GLenum mode, f32 r, f32 g, f32 b, f32 a)
{
    if (count <= 0 || !vertices) return;
    const size_t first = g_UiVertices.size();
    const size_t oldSize = g_UiVertices.size();
    g_UiVertices.resize(oldSize + (size_t)count);
    for (i32 i = 0; i < count; ++i)
    {
        g_UiVertices[oldSize + i] = {vertices[i * 2], vertices[i * 2 + 1], r, g, b, a};
    }
    if (!g_UiCommands.empty() && g_UiCommands.back().mode == mode &&
        g_UiCommands.back().first + g_UiCommands.back().count == (GLint)first)
    {
        g_UiCommands.back().count += count;
    }
    else
    {
        g_UiCommands.push_back({(GLint)first, (GLsizei)count, mode, r, g, b, a});
    }
}

void DrawRect(const Rect &rect, f32 r, f32 g, f32 b, f32 a)
{
    const f32 v[] = {rect.x, rect.y, rect.x + rect.w, rect.y, rect.x, rect.y + rect.h,
                     rect.x, rect.y + rect.h, rect.x + rect.w, rect.y, rect.x + rect.w,
                     rect.y + rect.h};
    DrawVertices(v, 6, GL_TRIANGLES, r, g, b, a);
}

void DrawOutline(const Rect &rect, f32 thickness, f32 r, f32 g, f32 b, f32 a)
{
    DrawRect({rect.x, rect.y, rect.w, thickness}, r, g, b, a);
    DrawRect({rect.x, rect.y + rect.h - thickness, rect.w, thickness}, r, g, b, a);
    DrawRect({rect.x, rect.y, thickness, rect.h}, r, g, b, a);
    DrawRect({rect.x + rect.w - thickness, rect.y, thickness, rect.h}, r, g, b, a);
}

const u8 *Glyph(char c)
{
    static const u8 digits[10][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,2,12}};
    static const u8 letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {15,16,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,25,21,19,19,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
    static const u8 dash[7] = {0,0,0,31,0,0,0};
    static const u8 slash[7] = {1,2,2,4,8,8,16};
    static const u8 dot[7] = {0,0,0,0,0,12,12};
    static const u8 plus[7] = {0,4,4,31,4,4,0};
    static const u8 colon[7] = {0,12,12,0,12,12,0};
    static const u8 question[7] = {14,17,1,2,4,0,4};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c == '-') return dash;
    if (c == '/') return slash;
    if (c == '.') return dot;
    if (c == '+') return plus;
    if (c == ':') return colon;
    return question;
}

void DrawText(const char *text, f32 x, f32 y, f32 scale, f32 r = 0.95f, f32 g = 0.97f,
              f32 b = 1.0f, f32 a = 0.96f)
{
    g_TextScratch.clear();
    g_TextScratch.reserve(strlen(text) * 96);
    const f32 pixel = std::max(1.0f, scale);
    for (const char *cursor = text; *cursor; ++cursor)
    {
        if (*cursor == ' ')
        {
            x += pixel * 6.0f;
            continue;
        }
        const u8 *glyph = Glyph(*cursor);
        for (i32 row = 0; row < 7; ++row)
        {
            for (i32 col = 0; col < 5; ++col)
            {
                if (!(glyph[row] & (1 << (4 - col)))) continue;
                const f32 x0 = x + col * pixel;
                const f32 y0 = y + row * pixel;
                const f32 x1 = x0 + pixel;
                const f32 y1 = y0 + pixel;
                const f32 quad[] = {x0,y0,x1,y0,x0,y1,x0,y1,x1,y0,x1,y1};
                g_TextScratch.insert(g_TextScratch.end(), quad, quad + 12);
            }
        }
        x += pixel * 6.0f;
    }
    if (!g_TextScratch.empty())
    {
        DrawVertices(g_TextScratch.data(), (i32)g_TextScratch.size() / 2,
                     GL_TRIANGLES, r, g, b, a);
    }
}

void DrawCircleAt(f32 x, f32 y, f32 radius, f32 r, f32 g, f32 b, f32 a)
{
    constexpr i32 segments = 32;
    f32 vertices[segments * 3 * 2];
    for (i32 i = 0; i < segments; ++i)
    {
        const f32 angle0 = (f32)i / (f32)segments * 2.0f * kPi;
        const f32 angle1 = (f32)(i + 1) / (f32)segments * 2.0f * kPi;
        const i32 offset = i * 6;
        vertices[offset] = x;
        vertices[offset + 1] = y;
        vertices[offset + 2] = x + cosf(angle0) * radius;
        vertices[offset + 3] = y + sinf(angle0) * radius;
        vertices[offset + 4] = x + cosf(angle1) * radius;
        vertices[offset + 5] = y + sinf(angle1) * radius;
    }
    DrawVertices(vertices, segments * 3, GL_TRIANGLES, r, g, b, a);
}

bool IsActionHeld(i32 index)
{
    if (index == ACTION_SHOOT && g_Config.shootToggleMode && g_ShootLatched) return true;
    if (index == ACTION_FOCUS && g_Config.focusToggleMode && g_FocusLatched) return true;
    for (const TouchOwner &touch : g_Touches)
    {
        if (touch.active && touch.kind == TOUCH_ACTION && touch.index == index) return true;
    }
    return false;
}

bool IsJoystickHeld()
{
    for (const TouchOwner &touch : g_Touches)
    {
        if (touch.active && touch.kind == TOUCH_JOYSTICK) return true;
    }
    return false;
}

void DrawAction(const CircleButton &button, bool held)
{
    const bool danger = button.id == ACTION_SHOOT || button.id == ACTION_BOMB;
    const f32 alpha = held ? std::min(0.98f, g_Config.opacity + 0.30f) : g_Config.opacity;
    const f32 shrink = held ? 0.88f : 1.0f;
    DrawCircleAt(button.x, button.y, button.radius * 1.05f,
                 danger ? 0.32f : 0.22f, danger ? 0.06f : 0.22f,
                 danger ? 0.16f : 0.38f, alpha * 0.72f);
    DrawCircleAt(button.x, button.y, button.radius * shrink,
                 danger ? 0.86f : 0.52f, danger ? 0.22f : 0.72f,
                 danger ? 0.42f : 0.88f, alpha);
    const f32 textScale = std::max(1.2f, button.radius * 0.078f * shrink);
    const f32 textWidth = strlen(button.label) * textScale * 6.0f;
    DrawText(button.label, button.x - textWidth * 0.5f,
             button.y - textScale * 3.5f + (held ? button.radius * 0.04f : 0.0f),
             textScale, 1.0f, 0.98f, 0.98f, 0.98f);
}

void DrawJoystick()
{
    const bool held = IsJoystickHeld();
    const f32 alpha = g_Config.opacity;
    DrawCircleAt(g_JoystickBaseX, g_JoystickBaseY, g_JoystickRadius,
                 0.18f, 0.16f, 0.32f, alpha * 0.50f);
    DrawCircleAt(g_JoystickBaseX, g_JoystickBaseY, g_JoystickRadius * 0.78f,
                 0.28f, 0.42f, 0.58f, alpha * 0.40f);
    const f32 knobX = g_JoystickBaseX + g_JoystickX * g_JoystickRadius * 0.60f;
    const f32 knobY = g_JoystickBaseY + g_JoystickY * g_JoystickRadius * 0.60f;
    DrawCircleAt(knobX, knobY, g_JoystickRadius * (held ? 0.40f : 0.36f),
                 held ? 0.82f : 0.68f, held ? 0.90f : 0.78f, 0.96f,
                 held ? std::min(0.96f, alpha + 0.25f) : alpha);
}

void DrawToolButton(const Rect &rect, const char *label, bool developer)
{
    DrawRect(rect, developer ? 0.34f : 0.12f, developer ? 0.08f : 0.22f,
             developer ? 0.18f : 0.34f, 0.78f);
    DrawOutline(rect, std::max(1.0f, rect.h * 0.04f), 0.88f, 0.72f, 0.88f, 0.72f);
    const f32 scale = std::max(1.2f, rect.h * 0.065f);
    const f32 width = strlen(label) * scale * 6.0f;
    DrawText(label, rect.x + (rect.w - width) * 0.5f, rect.y + rect.h * 0.28f,
             scale, 1.0f, 0.92f, 0.98f, 0.96f);
}

void DrawPanelBackground(const Rect &panel, const char *title)
{
    DrawRect(panel, 0.055f, 0.025f, 0.090f, 0.96f);
    DrawOutline(panel, std::max(2.0f, panel.w * 0.006f), 0.72f, 0.52f, 0.74f, 0.90f);
    DrawRect({panel.x + panel.w * 0.025f, panel.y + panel.h * 0.025f,
              panel.w * 0.95f, panel.h * 0.09f}, 0.20f, 0.05f, 0.19f, 0.92f);
    const f32 titleScale = std::max(1.5f, std::min(panel.w, panel.h) * 0.0062f);
    DrawText(title, panel.x + panel.w * 0.06f, panel.y + panel.h * 0.052f,
             titleScale, 1.0f, 0.78f, 0.92f, 1.0f);
}

void DrawSettingsPanel()
{
    static const char *controlLabels[10] = {"CONTROL", "BUTTONS", "BUTTON SIZE", "DRAG SENS",
                                             "OPACITY", "Z TOGGLE", "S TOGGLE", "AUTO BOMB",
                                             "EDIT LAYOUT", "CLOSE"};
    static const char *performanceLabels[7] = {"LOW EFFECTS", "NO BACKGROUND", "SHOW FPS",
                                               "BGM", "SFX", "DEVELOPER MODE", "CLOSE"};
    const Rect panel = PanelRect();
    DrawPanelBackground(panel, g_SettingsPerformancePage ? "PERFECT CHERRY PERFORMANCE" :
                                                          "PERFECT CHERRY CONTROLS");
    const f32 top = panel.y + panel.h * 0.145f;
    const i32 count = g_SettingsPerformancePage ? 7 : 10;
    const f32 rowHeight = panel.h * (g_SettingsPerformancePage ? 0.104f : 0.084f);
    const f32 textScale = std::max(1.2f, std::min(panel.w, panel.h) * 0.0045f);
    for (i32 row = 0; row < count; ++row)
    {
        const Rect line = {panel.x + panel.w * 0.06f, top + row * rowHeight,
                           panel.w * 0.88f, rowHeight * 0.76f};
        const bool close = row == count - 1;
        DrawRect(line, close ? 0.36f : (row % 2 ? 0.14f : 0.18f),
                 close ? 0.06f : 0.09f, close ? 0.14f : 0.20f, 0.92f);
        const char *label = g_SettingsPerformancePage ? performanceLabels[row] : controlLabels[row];
        DrawText(label, line.x + line.w * 0.04f, line.y + line.h * 0.26f, textScale);
        if (close) continue;
        char value[32] = {};
        if (g_SettingsPerformancePage)
        {
            switch (row)
            {
            case 0: SDL_strlcpy(value, g_Config.lowEffects ? "ON" : "OFF", sizeof(value)); break;
            case 1: SDL_strlcpy(value, g_Config.disableBackground ? "ON" : "OFF", sizeof(value)); break;
            case 2: SDL_strlcpy(value, g_Config.showFps ? "ON" : "OFF", sizeof(value)); break;
            case 3: SDL_strlcpy(value, g_Config.bgmEnabled ? "ON" : "OFF", sizeof(value)); break;
            case 4: SDL_strlcpy(value, g_Config.sfxEnabled ? "ON" : "OFF", sizeof(value)); break;
            case 5: SDL_strlcpy(value, g_Config.developerDisabled ? "OFF" : "ON", sizeof(value)); break;
            }
        }
        else
        {
            switch (row)
            {
            case 0:
                SDL_strlcpy(value, g_Config.controlMode == CONTROL_JOYSTICK ? "JOYSTICK" :
                                      g_Config.controlMode == CONTROL_DRAG ? "DRAG" : "HYBRID",
                            sizeof(value));
                break;
            case 1: SDL_strlcpy(value, g_Config.showButtons ? "ON" : "OFF", sizeof(value)); break;
            case 2: snprintf(value, sizeof(value), "%d", (i32)(g_Config.scale * 100)); break;
            case 3: snprintf(value, sizeof(value), "%d", (i32)(g_Config.dragSensitivity * 100)); break;
            case 4: snprintf(value, sizeof(value), "%d", (i32)(g_Config.opacity * 100)); break;
            case 5: SDL_strlcpy(value, g_Config.shootToggleMode ? "ON" : "OFF", sizeof(value)); break;
            case 6: SDL_strlcpy(value, g_Config.focusToggleMode ? "ON" : "OFF", sizeof(value)); break;
            case 7: SDL_strlcpy(value, g_Config.autoBomb ? "ON" : "OFF", sizeof(value)); break;
            }
        }
        const f32 valueWidth = strlen(value) * textScale * 6.0f;
        DrawText(value, line.x + line.w - valueWidth - line.w * 0.04f,
                 line.y + line.h * 0.26f, textScale, 0.82f, 0.94f, 1.0f, 0.98f);

        if (!g_SettingsPerformancePage && row >= 2 && row <= 4)
        {
            f32 amount = 0.0f;
            if (row == 2) amount = (g_Config.scale - 0.65f) / (1.45f - 0.65f);
            if (row == 3) amount = (g_Config.dragSensitivity - 0.25f) / (3.0f - 0.25f);
            if (row == 4) amount = (g_Config.opacity - 0.20f) / (0.95f - 0.20f);
            amount = std::clamp(amount, 0.0f, 1.0f);
            const Rect track = {line.x + line.w * 0.56f, line.y + line.h * 0.66f,
                                line.w * 0.38f, std::max(3.0f, line.h * 0.07f)};
            DrawRect(track, 0.15f, 0.10f, 0.22f, 0.95f);
            DrawRect({track.x, track.y, track.w * amount, track.h},
                     0.72f, 0.42f, 0.70f, 0.98f);
            DrawCircleAt(track.x + track.w * amount, track.y + track.h * 0.5f,
                         std::max(6.0f, line.h * 0.13f), 0.96f, 0.82f, 0.94f, 1.0f);
        }
    }
}

void DrawDeveloperPanel()
{
    static const char *labels[5] = {"ADD LIFE", "ADD BOMB", "MAX POWER", "FULL RESTOCK", "CLOSE"};
    const Rect panel = PanelRect();
    DrawPanelBackground(panel, "PERFECT CHERRY DEVELOPER");
    const f32 top = panel.y + panel.h * 0.22f;
    const f32 rowHeight = panel.h * 0.13f;
    const f32 textScale = std::max(1.5f, std::min(panel.w, panel.h) * 0.0054f);
    for (i32 row = 0; row < 5; ++row)
    {
        const Rect line = {panel.x + panel.w * 0.09f, top + row * rowHeight,
                           panel.w * 0.82f, rowHeight * 0.72f};
        DrawRect(line, row == 4 ? 0.38f : 0.24f, 0.055f, row == 4 ? 0.14f : 0.22f, 0.92f);
        DrawOutline(line, 1.5f, 0.82f, 0.48f, 0.72f, 0.72f);
        DrawText(labels[row], line.x + line.w * 0.07f, line.y + line.h * 0.27f,
                 textScale, 1.0f, 0.86f, 0.96f, 0.98f);
    }
}

#if !defined(TH07_IOS)
void DrawOnlinePanel()
{
    const Rect panel = PanelRect();
    // Keep this close to TH06 iOS' launcher: one title bar, four transport
    // choices, then the two session actions and the game controls. Runtime
    // diagnostics such as port/RTT stay out of the player-facing menu.
    DrawPanelBackground(panel, "TH07 ONLINE");
    const f32 top = panel.y + panel.h * 0.16f;
    const f32 rowHeight = panel.h * 0.073f;
    const f32 scale = std::max(1.0f, std::min(panel.w, panel.h) * 0.0033f);
    const Online::Mode mode = Online::GetMode();
    const char *labels[10] = {
        "NEARBY LAN", "DIRECT ADDRESS", "RELAY ROOM", "BLUETOOTH NEARBY",
        mode == Online::MODE_BLUETOOTH ? "CREATE ROOM" : "CREATE / CONNECT AS HOST",
        "SEARCH / CONNECT AS GUEST", "LEAVE SESSION", "START GAME",
        "START GAME (LOCAL)", "BACK"
    };
    for (i32 row = 0; row < 10; ++row)
    {
        const Rect line = {panel.x + panel.w * 0.09f, top + row * rowHeight,
                           panel.w * 0.82f, rowHeight * 0.72f};
        const bool selectedMode = row < 4 && row == (i32)mode;
        const bool sessionAction = row >= 4 && row <= 8;
        const bool selected = selectedMode ||
                              (Online::IsConnected() && (row == 7 || row == 4 || row == 5));
        const bool close = row == 9;
        DrawRect(line, close ? 0.28f : selected ? 0.42f : (sessionAction ? 0.18f : 0.10f),
                 close ? 0.06f : selected ? 0.14f : 0.07f,
                 close ? 0.12f : selected ? 0.32f : 0.16f, 0.94f);
        DrawOutline(line, selected ? 2.0f : 1.0f, close ? 0.62f : 0.82f,
                    close ? 0.46f : 0.62f, close ? 0.68f : 0.78f, 0.72f);
        DrawText(labels[row], line.x + line.w * 0.06f, line.y + line.h * 0.24f, scale,
                 selected ? 1.0f : 0.86f, selected ? 0.94f : 0.82f,
                 selected ? 0.94f : 0.90f, 0.98f);
    }
    DrawText("STATUS:", panel.x + panel.w * 0.09f,
             panel.y + panel.h * 0.91f, std::max(0.9f, scale * 0.72f),
             0.72f, 0.84f, 0.94f, 0.94f);
    DrawText(Online::GetStatusText(), panel.x + panel.w * 0.25f,
             panel.y + panel.h * 0.91f, std::max(0.9f, scale * 0.72f),
             0.86f, 0.90f, 1.0f, 0.96f);
}
#endif

void DrawFps()
{
    char value[16];
    snprintf(value, sizeof(value), "%d FPS", g_Fps);
    const f32 scale = std::max(1.2f, std::min(g_LastWidth, g_LastHeight) * 0.0045f);
    const f32 width = strlen(value) * scale * 6.0f;
    DrawRect({g_LastWidth * 0.5f - width * 0.58f, 8.0f, width * 1.16f, scale * 9.0f},
             0.03f, 0.02f, 0.08f, 0.72f);
    DrawText(value, g_LastWidth * 0.5f - width * 0.5f, 8.0f + scale,
             scale, 0.90f, 0.98f, 1.0f, 0.96f);
}
} // namespace

void MobileUi::Initialize()
{
    LoadConfig();
    i32 width, height;
    GetWindowSize(width, height);
    BuildLayout(width, height);
}

void MobileUi::Shutdown()
{
    if (g_ConfigDirty) SaveConfig();
    if (g_Program)
    {
        glDeleteProgram(g_Program);
        glDeleteBuffers(1, &g_Vbo);
        glDeleteVertexArrays(1, &g_Vao);
        g_Program = 0;
    }
}

void MobileUi::Update()
{
    const u64 now = SDL_GetTicks();
    if (!g_FpsWindowStart) g_FpsWindowStart = now;
    ++g_FpsFrames;
    if (now - g_FpsWindowStart >= 1000)
    {
        g_Fps = (i32)(g_FpsFrames * 1000 / std::max<u64>(1, now - g_FpsWindowStart));
        g_FpsFrames = 0;
        g_FpsWindowStart = now;
    }
    if (g_ConfigDirty && now - g_LastConfigSave > 500) SaveConfig();
    g_SoundPlayer.SetBgmMuted(!g_Config.bgmEnabled);

    const bool dialogueActive = IsGameplay() && g_Gui.HasCurrentMsgIdx();
    if (dialogueActive && !g_DialogueInputActive)
    {
        const bool hadPanel = g_DeveloperOpen || g_SettingsOpen || g_LayoutEditMode;
        const bool hadLatch = g_ShootLatched || g_FocusLatched;
        g_DeveloperOpen = false;
        g_SettingsOpen = false;
        g_LayoutEditMode = false;
        ClearTouchState();
        MobileDiagnostics::Log("mobile/dialogue",
                               "entered; closedPanel=%d releasedLatch=%d",
                               hadPanel, hadLatch);
    }
    g_DialogueInputActive = dialogueActive;

    if ((!IsGameplay() || g_GameManager.isInPauseMenu || g_GameManager.isInRetryMenu) &&
        (g_ShootLatched || g_FocusLatched))
    {
        g_ShootLatched = false;
        g_FocusLatched = false;
        RebuildHeldBits();
    }

    if (!g_MainMenuActive && g_SettingsOpen)
    {
        g_SettingsOpen = false;
        CancelTouches();
    }
    if (!g_MainMenuActive && Online::IsMenuOpen())
    {
        Online::CloseMenu();
        CancelTouches();
    }
    if (!IsGameplayOverlayScene() && g_DeveloperOpen)
    {
        g_DeveloperOpen = false;
        CancelTouches();
    }
}

void MobileUi::Draw(i32 drawableWidth, i32 drawableHeight)
{
    if (!IsMobileBuild()) return;
    i32 width, height;
    GetWindowSize(width, height);
    if (width != g_LastWidth || height != g_LastHeight)
    {
        BuildLayout(width, height);
        CancelTouches();
    }
    const bool drawGameplayControls = IsGameplay() && g_Config.showButtons;
    const bool drawOverlay = IsGameplay() || g_DeveloperOpen || g_SettingsOpen || g_LayoutEditMode ||
                             UsesOpenGlOnlinePanel() ||
                             g_MainMenuHome || (g_MainMenuActive && !g_MainMenuHome) ||
                             (g_Config.showFps && g_Fps > 0);
    if (!drawGameplayControls && !drawOverlay) return;
    EnsureRenderer();
    if (!g_Program) return;
    glUseProgram(g_Program);
    glUniform2f(g_ScreenSizeUniform, (f32)g_LastWidth, (f32)g_LastHeight);
    glBindVertexArray(g_Vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_Vbo);
    g_UiVertices.clear();
    g_UiCommands.clear();
    g_UiVertices.reserve(8192);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, drawableWidth, drawableHeight);

    if (g_LayoutEditMode)
    {
        DrawJoystick();
        for (i32 i = 0; i < kActionButtonCount; ++i) DrawAction(g_Actions[i], IsActionHeld(i));
        DrawToolButton(g_LayoutSaveButton, "SAVE", false);
        DrawToolButton(g_LayoutResetButton, "RESET", false);
        DrawToolButton(g_LayoutCancelButton, "CANCEL", true);
    }
    else if (g_DeveloperOpen)
    {
        DrawDeveloperPanel();
    }
    else if (g_SettingsOpen)
    {
        DrawSettingsPanel();
    }
    else if (UsesOpenGlOnlinePanel())
    {
#if !defined(TH07_IOS)
        DrawOnlinePanel();
#endif
    }
    else
    {
        if (g_MainMenuHome)
        {
            DrawToolButton(g_SettingsButton, "CFG", false);
            DrawToolButton(g_PerformanceButton, "PERF", false);
        }
        else if (g_MainMenuActive)
        {
            DrawToolButton(g_MenuBackButton, "X", false);
            DrawToolButton(g_MenuConfirmButton, "OK", false);
        }
        if (IsGameplay())
        {
            if (g_Config.showButtons)
            {
                if (g_Config.controlMode != CONTROL_DRAG) DrawJoystick();
                for (i32 i = 0; i < kActionButtonCount; ++i)
                {
                    DrawAction(g_Actions[i], IsActionHeld(i));
                }
            }
            if (!g_Config.developerDisabled) DrawToolButton(g_DeveloperButton, "DEV", true);
        }
    }
    if (g_Config.showFps) DrawFps();

    if (!g_UiVertices.empty())
    {
        const size_t bytes = g_UiVertices.size() * sizeof(UiVertex);
        if (bytes > g_UiVboAllocated)
        {
            g_UiVboAllocated = std::max(bytes, g_UiVboAllocated * 2);
            glBufferData(GL_ARRAY_BUFFER, g_UiVboAllocated, nullptr, GL_STREAM_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, g_UiVertices.data());
        for (const UiDrawCommand &command : g_UiCommands)
        {
            if (command.first < 0 || command.count <= 0 ||
                (size_t)command.first + (size_t)command.count > g_UiVertices.size())
            {
                MobileDiagnostics::Log("mobile/render",
                    "rejected draw range first=%d count=%d vertices=%u",
                    command.first, command.count, (u32)g_UiVertices.size());
                continue;
            }
            glDrawArrays(command.mode, command.first, command.count);
        }
        if (g_Config.showFps)
        {
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR)
            {
                MobileDiagnostics::Log("mobile/render",
                    "GL error=0x%x vertices=%u bytes=%u commands=%u allocated=%u",
                    (u32)error, (u32)g_UiVertices.size(), (u32)bytes,
                    (u32)g_UiCommands.size(), (u32)g_UiVboAllocated);
            }
        }
    }
}

bool MobileUi::FingerDown(const SDL_TouchFingerEvent &event)
{
    if (!IsMobileBuild()) return false;
    i32 width, height;
    GetWindowSize(width, height);
    if (width != g_LastWidth || height != g_LastHeight) BuildLayout(width, height);
    f32 x, y;
    FingerToPixels(event, x, y);

    // Ignore a repeated DOWN for a finger that is already owned.  iOS can
    // replay the edge after an input-dispatch stall; re-running hit testing
    // here would toggle Z/S latches twice or emit a duplicate pause pulse.
    if (TouchOwner *existing = FindTouch(event.fingerID))
    {
        existing->x = x;
        existing->y = y;
        return true;
    }

    if (UsesOpenGlOnlinePanel())
    {
        const Rect panel = PanelRect();
        Online::HandleTouch(x, y, panel.x, panel.y, panel.w, panel.h);
        AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        return true;
    }

    // The pause/retry flags can briefly survive a scene transition.  Treat
    // them as touch overlays only for a live stage; otherwise a second finger
    // on the setup/difficulty screen would be interpreted as "back/pause".
    const bool overlayMenu = IsValidBattleOverlayScene() &&
                             (g_GameManager.isInPauseMenu || g_GameManager.isInRetryMenu);
    const bool menuContext = g_MainMenuActive || overlayMenu || g_SettingsOpen ||
                             g_DeveloperOpen || g_LayoutEditMode;
    if (menuContext && IsFreshTwoFingerGesture(event.fingerID))
    {
        SuppressPendingTaps();
        if (g_LayoutEditMode)
        {
            const int orientation = LayoutOrientation();
            g_Config.layouts[orientation] = g_LayoutEditBackup;
            g_Config.layoutCustomized[orientation] = g_LayoutCustomizedBackup;
            g_LayoutEditMode = false;
            BuildLayout(width, height);
        }
        else if (g_SettingsOpen)
        {
            g_SettingsOpen = false;
        }
        else if (g_DeveloperOpen)
        {
            g_DeveloperOpen = false;
        }
        else if (overlayMenu)
        {
            g_OverlayTouchAction = OVERLAY_TOUCH_BACK;
        }
        else if (g_MainMenuActive && !g_MainMenuHome)
        {
            MobileUi::QueueButtonPulse(TH_BUTTON_RETURNMENU);
        }
        AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        MobileDiagnostics::Log("mobile/gesture", "two-finger back main=%d home=%d pause=%d retry=%d",
                               g_MainMenuActive, g_MainMenuHome,
                               g_GameManager.isInPauseMenu, g_GameManager.isInRetryMenu);
        return true;
    }

    if (g_LayoutEditMode)
    {
        if (Contains(g_LayoutSaveButton, x, y))
        {
            g_LayoutEditMode = false;
            g_Config.layoutCustomized[LayoutOrientation()] = 1;
            MarkConfigDirty();
            ClearPhysicalTouches();
            return true;
        }
        if (Contains(g_LayoutResetButton, x, y))
        {
            g_Config.layoutCustomized[LayoutOrientation()] = 0;
            BuildLayout(width, height);
            MarkConfigDirty();
            return true;
        }
        if (Contains(g_LayoutCancelButton, x, y))
        {
            const int orientation = LayoutOrientation();
            g_Config.layouts[orientation] = g_LayoutEditBackup;
            g_Config.layoutCustomized[orientation] = g_LayoutCustomizedBackup;
            g_LayoutEditMode = false;
            BuildLayout(width, height);
            CancelTouches();
            return true;
        }
        const i32 action = HitAction(x, y);
        if (action >= 0)
        {
            TouchOwner *touch = AllocateTouch(event.fingerID, TOUCH_LAYOUT_CONTROL, x, y);
            if (touch) touch->index = action;
            return true;
        }
        if (IsInJoystickZone(x, y))
        {
            TouchOwner *touch = AllocateTouch(event.fingerID, TOUCH_LAYOUT_CONTROL, x, y);
            if (touch) touch->index = kActionButtonCount;
            return true;
        }
        return true;
    }

    if (g_DeveloperOpen)
    {
        AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        ActivateDeveloperRow(DeveloperRowAt(x, y));
        return true;
    }
    if (g_SettingsOpen)
    {
        TouchOwner *touch = AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        const i32 row = SettingsRowAt(x, y);
        if (touch) touch->index = row;
        ActivateSettingsRow(row, x);
        return true;
    }
    if (g_MainMenuHome && Contains(g_SettingsButton, x, y))
    {
        g_SettingsPerformancePage = false;
        g_SettingsOpen = true;
        CancelTouches();
        AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        MobileDiagnostics::Log("mobile/settings", "opened from main menu");
        return true;
    }
    if (g_MainMenuHome && Contains(g_PerformanceButton, x, y))
    {
        g_SettingsPerformancePage = true;
        g_SettingsOpen = true;
        CancelTouches();
        AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        MobileDiagnostics::Log("mobile/performance", "opened from main menu");
        return true;
    }
    if (g_MainMenuActive && !g_MainMenuHome && Contains(g_MenuBackButton, x, y))
    {
        MobileUi::QueueButtonPulse(TH_BUTTON_RETURNMENU);
        AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        return true;
    }
    if (g_MainMenuActive && !g_MainMenuHome && Contains(g_MenuConfirmButton, x, y))
    {
        MobileUi::QueueButtonPulse(TH_BUTTON_SELECTMENU);
        AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        return true;
    }
    if (IsGameplay() && !g_Config.developerDisabled && Contains(g_DeveloperButton, x, y))
    {
        g_DeveloperOpen = true;
        CancelTouches();
        AllocateTouch(event.fingerID, TOUCH_PANEL, x, y);
        MobileDiagnostics::Log("mobile/dev", "opened during gameplay");
        return true;
    }

    if (IsGameplay() && g_Config.showButtons)
    {
        const i32 action = HitAction(x, y);
        if (action >= 0)
        {
            TouchOwner *touch = AllocateTouch(event.fingerID, TOUCH_ACTION, x, y);
            if (touch)
            {
                touch->index = action;
                if (action == ACTION_PAUSE && IsValidBattleOverlayScene())
                    MobileUi::QueueButtonPulse(TH_BUTTON_MENU);
                if (action == ACTION_SHOOT && g_Config.shootToggleMode)
                {
                    g_ShootLatched = !g_ShootLatched;
                    touch->toggle = true;
                }
                else if (action == ACTION_FOCUS && g_Config.focusToggleMode)
                {
                    g_FocusLatched = !g_FocusLatched;
                    touch->toggle = true;
                }
                RebuildHeldBits();
            }
            return true;
        }
        if (g_Config.controlMode != CONTROL_DRAG && IsInJoystickZone(x, y))
        {
            TouchOwner *touch = AllocateTouch(event.fingerID, TOUCH_JOYSTICK, x, y);
            if (touch)
            {
                UpdateJoystick(x, y);
                RebuildHeldBits();
            }
            return true;
        }
    }

    if (g_MainMenuActive)
    {
        AllocateTouch(event.fingerID, TOUCH_MENU_TAP, x, y);
        return true;
    }
    if (overlayMenu)
    {
        AllocateTouch(event.fingerID, TOUCH_OVERLAY_TAP, x, y);
        return true;
    }
    return false;
}

bool MobileUi::FingerMotion(const SDL_TouchFingerEvent &event)
{
    TouchOwner *touch = FindTouch(event.fingerID);
    if (!touch) return false;
    f32 x, y;
    FingerToPixels(event, x, y);
    touch->x = x;
    touch->y = y;
    if (touch->kind == TOUCH_JOYSTICK)
    {
        UpdateJoystick(x, y);
        RebuildHeldBits();
    }
    else if (touch->kind == TOUCH_LAYOUT_CONTROL && g_LayoutEditMode)
    {
        const int orientation = LayoutOrientation();
        NormalizedLayout &layout = g_Config.layouts[orientation];
        const f32 nx = std::clamp(x / std::max(1.0f, (f32)g_LastWidth), 0.0f, 1.0f);
        const f32 ny = std::clamp(y / std::max(1.0f, (f32)g_LastHeight), 0.0f, 1.0f);
        if (touch->index == kActionButtonCount)
        {
            layout.joystickX = nx;
            layout.joystickY = ny;
        }
        else if (touch->index >= 0 && touch->index < kActionButtonCount)
        {
            layout.actionX[touch->index] = nx;
            layout.actionY[touch->index] = ny;
        }
        g_Config.layoutCustomized[orientation] = 1;
        BuildLayout(g_LastWidth, g_LastHeight);
        MarkConfigDirty();
    }
    else if (touch->kind == TOUCH_ACTION)
    {
        if (!touch->toggle) touch->index = HitAction(x, y);
        RebuildHeldBits();
    }
    else if (touch->kind == TOUCH_PANEL && g_SettingsOpen && !g_SettingsPerformancePage)
    {
        UpdateControlSlider(touch->index, x);
    }
    else if (touch->kind == TOUCH_MENU_TAP)
    {
        const f32 dx = x - touch->startX;
        const f32 dy = y - touch->startY;
        const f32 threshold = std::max(12.0f,
            (f32)std::min(g_LastWidth, g_LastHeight) * 0.025f);
        if (touch->index != 1 && dx * dx + dy * dy >= threshold * threshold)
        {
            g_MainMenuTouchAction = fabsf(dx) >= fabsf(dy) ? MENU_TOUCH_SWIPE_HORIZONTAL
                                                           : MENU_TOUCH_SWIPE_VERTICAL;
            g_MainMenuTapX = x;
            g_MainMenuTapY = y;
            g_MainMenuTouchDelta = fabsf(dx) >= fabsf(dy) ? dx : dy;
            touch->index = 1;
        }
    }
    else if (touch->kind == TOUCH_OVERLAY_TAP)
    {
        const f32 dx = x - touch->startX;
        const f32 dy = y - touch->startY;
        const f32 threshold = std::max(12.0f,
            (f32)std::min(g_LastWidth, g_LastHeight) * 0.025f);
        if (dx * dx + dy * dy >= threshold * threshold) touch->index = 2;
    }
    return true;
}

bool MobileUi::FingerUp(const SDL_TouchFingerEvent &event)
{
    TouchOwner *touch = FindTouch(event.fingerID);
    if (!touch) return false;
    f32 x, y;
    FingerToPixels(event, x, y);
    if (touch->kind == TOUCH_MENU_TAP)
    {
        const f32 dx = x - touch->startX;
        const f32 dy = y - touch->startY;
        const f32 threshold = (f32)std::min(g_LastWidth, g_LastHeight) * 0.035f;
        if (touch->index != 1 && SDL_GetTicks() - touch->startTime <= 600 &&
            dx * dx + dy * dy <= threshold * threshold)
        {
            g_MainMenuTouchAction = MENU_TOUCH_TAP;
            g_MainMenuTapX = x;
            g_MainMenuTapY = y;
            g_MainMenuTouchDelta = 0.0f;
        }
    }
    else if (touch->kind == TOUCH_OVERLAY_TAP)
    {
        const f32 dx = x - touch->startX;
        const f32 dy = y - touch->startY;
        const f32 threshold = std::max(12.0f,
            (f32)std::min(g_LastWidth, g_LastHeight) * 0.025f);
        if (touch->index != 2 && SDL_GetTicks() - touch->startTime <= 800 &&
            dx * dx + dy * dy <= threshold * threshold)
        {
            g_OverlayTouchAction = OVERLAY_TOUCH_TAP;
            g_OverlayTapX = x;
            g_OverlayTapY = y;
        }
    }
    touch->active = false;
    touch->kind = TOUCH_NONE;
    touch->index = -1;
    touch->toggle = false;
    RebuildHeldBits();
    return true;
}

void MobileUi::CancelTouches()
{
    ClearTouchState();
}

void MobileUi::SetMainMenuActive(bool active)
{
    if (g_MainMenuActive == active)
    {
        // A failed/aborted menu registration can call the deactivation hook
        // more than once.  Even in that case discard a pulse that originated
        // in the old menu; otherwise it can be sampled after a stage starts.
        if (!active)
        {
            g_PulseBits = 0;
            g_PulseContextSet = false;
            g_PulseQueuedInBattle = false;
            g_MainMenuTouchAction = MENU_TOUCH_NONE;
            g_OverlayTouchAction = OVERLAY_TOUCH_NONE;
        }
        return;
    }
    g_MainMenuActive = active;
    g_MainMenuTouchAction = MENU_TOUCH_NONE;
    g_OverlayTouchAction = OVERLAY_TOUCH_NONE;
    if (!active)
    {
        g_SettingsOpen = false;
        Online::CloseMenu();
    }
    CancelTouches();
    MobileDiagnostics::Log("mobile/scene", "mainMenu=%d gameplay=%d", active, IsGameplay());
}

void MobileUi::SetMainMenuHome(bool active)
{
    g_MainMenuHome = active && g_MainMenuActive;
    if (!g_MainMenuHome && Online::IsMenuOpen())
    {
        Online::CloseMenu();
        CancelTouches();
    }
    if (!g_MainMenuHome && (g_SettingsOpen || g_LayoutEditMode))
    {
        g_SettingsOpen = false;
        g_LayoutEditMode = false;
        CancelTouches();
    }
}

MobileUi::MenuTouchAction MobileUi::ConsumeMainMenuTouch(f32 &gameX, f32 &gameY, f32 &delta)
{
    if (g_MainMenuTouchAction == MENU_TOUCH_NONE || !g_MainMenuActive) return MENU_TOUCH_NONE;
    const MenuTouchAction action = g_MainMenuTouchAction;
    g_MainMenuTouchAction = MENU_TOUCH_NONE;
    const f32 sx = (f32)g_LastWidth / 640.0f;
    const f32 sy = (f32)g_LastHeight / 480.0f;
    const f32 scale = std::min(sx, sy);
    const f32 rw = 640.0f * scale;
    const f32 rh = 480.0f * scale;
    const f32 rx = ((f32)g_LastWidth - rw) * 0.5f;
    const f32 ry = ((f32)g_LastHeight - rh) * 0.5f;
    gameX = (g_MainMenuTapX - rx) / scale;
    gameY = (g_MainMenuTapY - ry) / scale;
    delta = g_MainMenuTouchDelta / scale;
    MobileDiagnostics::Log("mobile/menu", "touch action=%d game=(%.1f,%.1f) delta=%.1f",
                           action, gameX, gameY, delta);
    return action;
}

MobileUi::OverlayTouchAction MobileUi::ConsumeOverlayTouch(f32 &gameX, f32 &gameY)
{
    if (g_OverlayTouchAction == OVERLAY_TOUCH_NONE ||
        (!g_GameManager.isInPauseMenu && !g_GameManager.isInRetryMenu))
    {
        return OVERLAY_TOUCH_NONE;
    }
    const OverlayTouchAction action = g_OverlayTouchAction;
    g_OverlayTouchAction = OVERLAY_TOUCH_NONE;
    if (action == OVERLAY_TOUCH_BACK)
    {
        gameX = gameY = 0.0f;
        return action;
    }

    if (g_LastHeight > g_LastWidth)
    {
        const PortraitLayout layout = GetPortraitLayout(g_LastWidth, g_LastHeight);
        if (g_OverlayTapX < layout.gameX ||
            g_OverlayTapX >= layout.gameX + layout.gameWidth ||
            g_OverlayTapY < layout.gameY ||
            g_OverlayTapY >= layout.gameY + layout.gameHeight)
        {
            return OVERLAY_TOUCH_NONE;
        }
        gameX = 32.0f + (g_OverlayTapX - layout.gameX) /
                    std::max(1.0f, (f32)layout.gameWidth) * 384.0f;
        gameY = 16.0f + (g_OverlayTapY - layout.gameY) /
                    std::max(1.0f, (f32)layout.gameHeight) * 448.0f;
    }
    else
    {
        const f32 sx = (f32)g_LastWidth / 640.0f;
        const f32 sy = (f32)g_LastHeight / 480.0f;
        const f32 scale = std::min(sx, sy);
        const f32 rx = ((f32)g_LastWidth - 640.0f * scale) * 0.5f;
        const f32 ry = ((f32)g_LastHeight - 480.0f * scale) * 0.5f;
        gameX = (g_OverlayTapX - rx) / scale;
        gameY = (g_OverlayTapY - ry) / scale;
    }
    MobileDiagnostics::Log("mobile/overlay", "action=%d game=(%.1f,%.1f)",
                           action, gameX, gameY);
    return action;
}

void MobileUi::QueueButtonPulse(u16 buttons)
{
    if (buttons == 0) return;
    const bool inBattle = IsValidBattleOverlayScene();
    if (!g_PulseContextSet)
    {
        g_PulseContextSet = true;
        g_PulseQueuedInBattle = inBattle;
    }
    else if (g_PulseQueuedInBattle != inBattle)
    {
        // Do not combine edges from two scenes in one input frame.  The old
        // edge is the one that can make a just-started battle pause.
        g_PulseBits = 0;
        g_PulseQueuedInBattle = inBattle;
    }
    g_PulseBits |= buttons;
}

u16 MobileUi::GetButtonBits()
{
    // A menu pulse may be read one simulation tick after the chain switched
    // to the stage.  It is never a valid gameplay command in that context.
    if (g_PulseContextSet && !g_PulseQueuedInBattle && IsValidBattleOverlayScene())
    {
        g_PulseBits = 0;
        g_PulseContextSet = false;
        g_PulseQueuedInBattle = false;
    }
    u16 bits = g_HeldBits | g_PulseBits;
    // Keep the main-menu RETURNMENU chord (MENU|BOMB) intact; only discard a
    // standalone virtual pause pulse that was queued just before a scene
    // transition.
    if (!IsValidBattleOverlayScene() && (bits & TH_BUTTON_MENU) &&
        !(bits & TH_BUTTON_BOMB))
        bits &= (u16)~TH_BUTTON_MENU;
    g_PulseBits = 0;
    g_PulseContextSet = false;
    g_PulseQueuedInBattle = false;
    return bits;
}

bool MobileUi::IsFingerCaptured(SDL_FingerID id)
{
    return FindTouch(id) != nullptr;
}

bool MobileUi::IsPanelOpen()
{
    return g_SettingsOpen || g_DeveloperOpen;
}

bool MobileUi::IsStageBackgroundDisabled()
{
    return g_Config.disableBackground;
}

bool MobileUi::IsPerformanceTelemetryEnabled()
{
    return g_Config.showFps;
}

bool MobileUi::IsAutoBombEnabled()
{
    return g_Config.autoBomb != 0;
}

bool MobileUi::IsAutoBombEnabledForPlayer(u8 playerId)
{
    if (!Online::IsNetworkSession()) return IsAutoBombEnabled();
    return Online::IsAutoBombEnabledForPlayer(playerId);
}

f32 MobileUi::GetDragSensitivity()
{
    return g_Config.dragSensitivity;
}

bool MobileUi::IsPortraitGameplayLayout()
{
    i32 width, height;
    GetWindowSize(width, height);
    return IsGameplayOverlayScene() && height > width;
}

i32 MobileUi::GetPortraitHeaderHeight(i32 screenWidth, i32 screenHeight)
{
    return GetPortraitLayout(screenWidth, screenHeight).gameY;
}

MobileUi::PortraitLayout MobileUi::GetPortraitLayout(i32 screenWidth, i32 screenHeight)
{
    PortraitLayout layout = {};
    if (screenWidth <= 0 || screenHeight <= 0) return layout;

    // Match the TH06 mobile composition: a compact full-width HUD occupies the
    // top 22%, then the playfield stretches to every remaining screen edge.
    // The source regions and gameplay coordinates are unchanged, so only the
    // final presentation is stretched and touch mapping remains exact.
    layout.hudX = 0;
    layout.hudY = 0;
    layout.hudWidth = screenWidth;
    layout.hudHeight = std::clamp((i32)std::lround((f32)screenHeight * 0.22f),
                                  1, screenHeight - 1);
    layout.gameX = 0;
    layout.gameY = layout.hudHeight;
    layout.gameWidth = screenWidth;
    layout.gameHeight = screenHeight - layout.hudHeight;
    return layout;
}
