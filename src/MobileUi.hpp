#pragma once

#include <SDL3/SDL_events.h>
#include "inttypes.hpp"

namespace MobileUi
{
void Initialize();
void Shutdown();
void Update();
void Draw(i32 drawableWidth, i32 drawableHeight);

bool FingerDown(const SDL_TouchFingerEvent &event);
bool FingerUp(const SDL_TouchFingerEvent &event);
bool FingerMotion(const SDL_TouchFingerEvent &event);
void CancelTouches();
void SetMainMenuActive(bool active);
void SetMainMenuHome(bool active);
enum MenuTouchAction
{
    MENU_TOUCH_NONE,
    MENU_TOUCH_TAP,
    MENU_TOUCH_SWIPE_HORIZONTAL,
    MENU_TOUCH_SWIPE_VERTICAL,
};
MenuTouchAction ConsumeMainMenuTouch(f32 &gameX, f32 &gameY, f32 &delta);
void QueueButtonPulse(u16 buttons);
enum OverlayTouchAction
{
    OVERLAY_TOUCH_NONE,
    OVERLAY_TOUCH_TAP,
    OVERLAY_TOUCH_BACK,
};
OverlayTouchAction ConsumeOverlayTouch(f32 &gameX, f32 &gameY);

u16 GetButtonBits();
bool IsFingerCaptured(SDL_FingerID id);
bool IsPanelOpen();
bool IsStageBackgroundDisabled();
f32 GetDragSensitivity();
bool IsPerformanceTelemetryEnabled();
bool IsAutoBombEnabled();
bool IsPortraitGameplayLayout();
struct PortraitLayout
{
    i32 hudX;
    i32 hudY;
    i32 hudWidth;
    i32 hudHeight;
    i32 gameX;
    i32 gameY;
    i32 gameWidth;
    i32 gameHeight;
};
PortraitLayout GetPortraitLayout(i32 screenWidth, i32 screenHeight);
i32 GetPortraitHeaderHeight(i32 screenWidth, i32 screenHeight);
} // namespace MobileUi
