#include "Touch.hpp"

#include <SDL3/SDL_events.h>
#include <cmath>

#include "Controller.hpp"
#include "GameManager.hpp"
#include "GameWindow.hpp"
#include "Gui.hpp"
#include "MobileDiagnostics.hpp"
#include "MobileUi.hpp"
#include "Supervisor.hpp"

struct FingerSlot
{
    bool active;
    SDL_FingerID id;
    f32 lastPxX;
    f32 lastPxY;
    u64 start;
    u64 end;
};

struct MenuGestureTracker
{
    bool active;
    SDL_FingerID primaryId;
    f32 startX;
    f32 startY;
    f32 currentX;
    f32 currentY;
    i32 maxFingers;
    u16 pendingButton;
    u64 startTime;
    f32 maxDistanceSq;
};

FingerSlot g_MoveFinger;
FingerSlot g_FocusFinger;
FingerSlot g_DialogueHoldFinger;
MenuGestureTracker g_MenuGesture;

f32 g_AccumDx = 0.0f;
f32 g_AccumDy = 0.0f;

bool g_UsedThisRun = false;

// hopefully you only have 10 fingers
SDL_FingerID g_ActiveGameplayFingerIds[10];
i32 g_NumActiveGameplayFingers = 0;

bool g_BombPending = false;
bool g_PausePending = false;
bool g_BombedWithTouch = false;
bool g_DialogueAdvancePending = false;
bool g_DialogueReleaseSent = false;
bool g_RecordedDeltaPending = false;
f32 g_RecordedDeltaX = 0.0f;
f32 g_RecordedDeltaY = 0.0f;
bool g_ReplayDeltaPending = false;
f32 g_ReplayDeltaX = 0.0f;
f32 g_ReplayDeltaY = 0.0f;

// Touch events can outlive the scene that received them (especially on iOS,
// where a queued FINGER_UP may arrive after a chain transition).  Keep a
// compact scene key so that a menu gesture or pause edge cannot leak into the
// next scene.
u64 g_LastTouchSceneKey = 0;
bool g_TouchSceneKeyInitialized = false;

constexpr u64 DIALOGUE_SKIP_HOLD_MS = 500;

void ReleaseFinger(FingerSlot *slot);
void ClearGameplayFingers();

static u64 BuildTouchSceneKey()
{
    u64 key = 0;
    key |= (u64)(g_Supervisor.curState & 0xff);
    key |= (u64)(g_GameManager.currentStage & 0xff) << 8;
    if (g_GameManager.notInMenu) key |= 1ull << 16;
    if (g_GameManager.globals) key |= 1ull << 17;
    if (g_GameManager.isInPauseMenu) key |= 1ull << 18;
    if (g_GameManager.isInRetryMenu) key |= 1ull << 19;
    if (g_GameManager.demo) key |= 1ull << 20;
    if (g_GameManager.replay) key |= 1ull << 21;
    if (g_GameManager.finished) key |= 1ull << 22;
    // framesThisStage is intentionally reduced to a ready/not-ready bit;
    // including the exact counter would invalidate every touch each frame.
    if (g_GameManager.framesThisStage > 0) key |= 1ull << 23;
    return key;
}

static void ClearTouchStateForSceneChange()
{
    ReleaseFinger(&g_MoveFinger);
    ReleaseFinger(&g_FocusFinger);
    ReleaseFinger(&g_DialogueHoldFinger);
    g_AccumDx = 0.0f;
    g_AccumDy = 0.0f;
    ClearGameplayFingers();
    g_PausePending = false;
    g_BombPending = false;
    g_BombedWithTouch = false;
    g_DialogueAdvancePending = false;
    g_DialogueReleaseSent = false;
    g_RecordedDeltaPending = false;
    g_ReplayDeltaPending = false;
    g_MenuGesture = {};
    // MobileUi owns its own touch owners and pulse queue.  Calling its
    // non-recursive clear here is what removes a pending virtual MENU pulse.
    MobileUi::CancelTouches();
}

static void ObserveTouchScene()
{
    const u64 key = BuildTouchSceneKey();
    if (!g_TouchSceneKeyInitialized)
    {
        g_LastTouchSceneKey = key;
        g_TouchSceneKeyInitialized = true;
        return;
    }
    if (key == g_LastTouchSceneKey)
    {
        return;
    }

    const u64 oldKey = g_LastTouchSceneKey;
    g_LastTouchSceneKey = key;
    ClearTouchStateForSceneChange();
    MobileDiagnostics::Log("touch/scene", "cleared stale touch state old=0x%llx new=0x%llx",
                           (unsigned long long)oldKey, (unsigned long long)key);
}

bool Touch::IsGameplayActive()
{
    // The engine can report notInMenu while a stage is being constructed. Do
    // not treat that transient viewport as a live playfield: a two-finger
    // gesture there must not become a pause edge.
    return g_GameManager.notInMenu && g_GameManager.globals &&
           g_GameManager.currentStage >= 1 && g_GameManager.currentStage <= 6 &&
           g_GameManager.framesThisStage > 0 && g_Supervisor.curState == 2 &&
           !g_GameManager.isInPauseMenu && !g_GameManager.isInRetryMenu &&
           !g_GameManager.replay && !g_GameManager.finished;
}

bool IsGameplayTouchMode()
{
    return Touch::IsGameplayActive();
}

static bool IsGameplayPauseGestureAllowed()
{
    return IsGameplayTouchMode() && !g_GameManager.demo &&
           !g_GameManager.finished && !g_Gui.HasCurrentMsgIdx();
}

void GetWindowSize(i32 *w, i32 *h)
{
    *w = 640;
    *h = 480;
    if (g_GameWindow.window)
    {
        SDL_GetWindowSize(g_GameWindow.window, w, h);
    }
}

void GetContainedRenderRect(f32 *outX, f32 *outY, f32 *outW, f32 *outH, f32 *outScale)
{
    i32 winW, winH;
    GetWindowSize(&winW, &winH);

    f32 sx = (f32)winW / 640.0f;
    f32 sy = (f32)winH / 480.0f;
    f32 scale = sx < sy ? sx : sy;

    f32 rw = 640.0f * scale;
    f32 rh = 480.0f * scale;
    f32 rx = ((f32)winW - rw) * 0.5f;
    f32 ry = ((f32)winH - rh) * 0.5f;
    if (winH > winW)
    {
        ry = 0.0f;
    }

    *outX = rx;
    *outY = ry;
    *outW = rw;
    *outH = rh;
    *outScale = scale;
}

void FingerToWindowPx(const SDL_TouchFingerEvent &f, f32 *px, f32 *py)
{
    i32 winW, winH;
    GetWindowSize(&winW, &winH);

    *px = f.x * (f32)winW;
    *py = f.y * (f32)winH;
}

f32 GetSwipeThreshold()
{
    i32 winW, winH;
    GetWindowSize(&winW, &winH);
    return (f32)winH * 0.05f;
}

bool IsBombZone(f32 px, f32 py)
{
    i32 winW, winH;
    GetWindowSize(&winW, &winH);

    f32 rx, ry, rw, rh, scale;
    GetContainedRenderRect(&rx, &ry, &rw, &rh, &scale);

    if (rx > 1.0f && (px < rx || px > (f32)winW - rx))
    {
        return true;
    }

    return px < (f32)winW * 0.15f && py > (f32)winH * 0.85f;
}

bool IsFinger(const FingerSlot &slot, SDL_FingerID id)
{
    return slot.active && slot.id == id;
}

void AssignFinger(FingerSlot *slot, SDL_FingerID id, f32 px, f32 py)
{
    slot->active = true;
    slot->id = id;
    slot->lastPxX = px;
    slot->lastPxY = py;
    slot->start = SDL_GetTicks();
    slot->end = 0;
}

void ReleaseFinger(FingerSlot *slot)
{
    slot->active = false;
    slot->end = SDL_GetTicks();
}

bool HasGameplayFinger(SDL_FingerID id)
{
    for (i32 i = 0; i < g_NumActiveGameplayFingers; i++)
    {
        if (g_ActiveGameplayFingerIds[i] == id)
        {
            return true;
        }
    }

    return false;
}

void AddGameplayFinger(SDL_FingerID id)
{
    if (HasGameplayFinger(id))
    {
        return;
    }

    if (g_NumActiveGameplayFingers < 10)
    {
        g_ActiveGameplayFingerIds[g_NumActiveGameplayFingers++] = id;
    }
}

void RemoveGameplayFinger(SDL_FingerID id)
{
    for (i32 i = 0; i < g_NumActiveGameplayFingers; i++)
    {
        if (g_ActiveGameplayFingerIds[i] == id)
        {
            g_ActiveGameplayFingerIds[i] =
                g_ActiveGameplayFingerIds[g_NumActiveGameplayFingers - 1];

            g_NumActiveGameplayFingers--;
            return;
        }
    }
}

void ClearGameplayFingers()
{
    g_NumActiveGameplayFingers = 0;
}

void Touch::ResetRunUsage()
{
    g_UsedThisRun = false;
    g_BombedWithTouch = false;
    g_DialogueAdvancePending = false;
    g_DialogueReleaseSent = false;
    g_RecordedDeltaPending = false;
    g_ReplayDeltaPending = false;
    g_MenuGesture = {};
    g_TouchSceneKeyInitialized = false;
}

bool Touch::WasUsedThisRun()
{
    return g_UsedThisRun;
}

bool Touch::UsedTouchToBomb()
{
    return g_BombedWithTouch;
}

void Touch::CancelTouches()
{
    ReleaseFinger(&g_MoveFinger);
    ReleaseFinger(&g_FocusFinger);
    ReleaseFinger(&g_DialogueHoldFinger);

    g_AccumDx = 0.0f;
    g_AccumDy = 0.0f;

    ClearGameplayFingers();
    g_PausePending = false;
    g_BombPending = false;
    g_BombedWithTouch = false;
    g_DialogueAdvancePending = false;
    g_DialogueReleaseSent = false;
    g_RecordedDeltaPending = false;
    g_ReplayDeltaPending = false;
    // A scene transition can occur while a menu finger is still down.  Do
    // not let that gesture's eventual FINGER_UP produce a stale select/back
    // edge in the newly-created stage (which can be interpreted as pause).
    g_MenuGesture = {};
    MobileUi::CancelTouches();
    g_LastTouchSceneKey = BuildTouchSceneKey();
    g_TouchSceneKeyInitialized = true;
}

void Touch::FingerDown(const SDL_TouchFingerEvent &f)
{
    ObserveTouchScene();
    f32 px, py;
    FingerToWindowPx(f, &px, &py);

    // Dialogue input owns the full screen. Handle it before MobileUi so taps
    // still advance when controls are hidden or land inside a virtual button.
    if (IsGameplayTouchMode() && g_Gui.HasCurrentMsgIdx())
    {
        if (!g_DialogueHoldFinger.active)
        {
            AssignFinger(&g_DialogueHoldFinger, f.fingerID, px, py);
            g_UsedThisRun = true;
        }
        return;
    }

    if (MobileUi::FingerDown(f))
    {
        g_UsedThisRun = true;
        return;
    }
    if (!IsGameplayTouchMode())
    {
        if (!g_MenuGesture.active)
        {
            g_MenuGesture.active = true;
            g_MenuGesture.primaryId = f.fingerID;
            g_MenuGesture.startX = px;
            g_MenuGesture.startY = py;
            g_MenuGesture.currentX = px;
            g_MenuGesture.currentY = py;
            g_MenuGesture.maxFingers = 1;
            g_MenuGesture.startTime = SDL_GetTicks();
            g_MenuGesture.maxDistanceSq = 0.0f;
        }
        else
        {
            // Duplicate DOWN events for the same SDL finger are seen on some
            // iOS versions after an input-dispatch stall.  They must not turn
            // a normal tap into a two-finger back gesture.
            if (f.fingerID != g_MenuGesture.primaryId)
                g_MenuGesture.maxFingers++;
        }
    }
    else
    {
        if (g_PausePending)
        {
            return;
        }

        if (IsBombZone(px, py))
        {
            g_BombPending = true;
            g_UsedThisRun = true;
            return;
        }

        AddGameplayFinger(f.fingerID);
        if (g_NumActiveGameplayFingers >= 4 && IsGameplayPauseGestureAllowed())
        {
            g_PausePending = true;
        }

        if (!g_MoveFinger.active)
        {
            if (g_FocusFinger.active && f.fingerID == g_FocusFinger.id)
            {
                return;
            }

            AssignFinger(&g_MoveFinger, f.fingerID, px, py);
            g_UsedThisRun = true;
            return;
        }

        if (!g_FocusFinger.active && f.fingerID != g_MoveFinger.id)
        {
            AssignFinger(&g_FocusFinger, f.fingerID, px, py);
            g_UsedThisRun = true;
            return;
        }
    }
}

void Touch::FingerUp(const SDL_TouchFingerEvent &f)
{
    ObserveTouchScene();
    if (IsFinger(g_DialogueHoldFinger, f.fingerID))
    {
        const u64 held = SDL_GetTicks() - g_DialogueHoldFinger.start;
        if (held < DIALOGUE_SKIP_HOLD_MS && g_Gui.HasCurrentMsgIdx())
        {
            g_DialogueAdvancePending = true;
            g_DialogueReleaseSent = false;
            MobileDiagnostics::Log("mobile/dialogue", "tap queued");
        }
        ReleaseFinger(&g_DialogueHoldFinger);
        g_UsedThisRun = true;
        return;
    }

    if (MobileUi::FingerUp(f))
    {
        return;
    }
    f32 px, py;
    FingerToWindowPx(f, &px, &py);

    if (!IsGameplayTouchMode())
    {
        if (g_MenuGesture.active && f.fingerID == g_MenuGesture.primaryId)
        {
            f32 dx = g_MenuGesture.currentX - g_MenuGesture.startX;
            f32 dy = g_MenuGesture.currentY - g_MenuGesture.startY;

            const f32 threshold = GetSwipeThreshold();
            const u64 elapsed = SDL_GetTicks() - g_MenuGesture.startTime;
            if (elapsed <= 500 && std::abs(dx) <= threshold && std::abs(dy) <= threshold &&
                g_MenuGesture.maxDistanceSq <= threshold * threshold)
            {
                if (g_MenuGesture.maxFingers >= 2)
                {
                    g_MenuGesture.pendingButton = TH_BUTTON_RETURNMENU;
                }
                else
                {
                    g_MenuGesture.pendingButton = TH_BUTTON_SELECTMENU;
                }
            }

            g_MenuGesture.active = false;
        }
    }
    else
    {
        RemoveGameplayFinger(f.fingerID);
        if (IsFinger(g_MoveFinger, f.fingerID))
        {
            ReleaseFinger(&g_MoveFinger);
            g_AccumDx = 0.0f;
            g_AccumDy = 0.0f;
        }
        if (IsFinger(g_FocusFinger, f.fingerID))
        {
            ReleaseFinger(&g_FocusFinger);
        }
    }
}

void Touch::FingerMotion(const SDL_TouchFingerEvent &f)
{
    ObserveTouchScene();
    if (IsFinger(g_DialogueHoldFinger, f.fingerID))
    {
        f32 px, py;
        FingerToWindowPx(f, &px, &py);
        g_DialogueHoldFinger.lastPxX = px;
        g_DialogueHoldFinger.lastPxY = py;
        return;
    }

    if (MobileUi::FingerMotion(f))
    {
        return;
    }
    f32 px, py;
    FingerToWindowPx(f, &px, &py);

    if (!IsGameplayTouchMode())
    {
        if (g_MenuGesture.active && f.fingerID == g_MenuGesture.primaryId)
        {
            g_MenuGesture.currentX = px;
            g_MenuGesture.currentY = py;
            const f32 dx = px - g_MenuGesture.startX;
            const f32 dy = py - g_MenuGesture.startY;
            const f32 distanceSq = dx * dx + dy * dy;
            if (distanceSq > g_MenuGesture.maxDistanceSq)
                g_MenuGesture.maxDistanceSq = distanceSq;
        }
    }
    else
    {
        if (IsFinger(g_MoveFinger, f.fingerID))
        {
            f32 dxPx = px - g_MoveFinger.lastPxX;
            f32 dyPx = py - g_MoveFinger.lastPxY;
            const f32 sensitivity = MobileUi::GetDragSensitivity();
            i32 winW, winH;
            GetWindowSize(&winW, &winH);
            if (MobileUi::IsPortraitGameplayLayout())
            {
                const MobileUi::PortraitLayout layout =
                    MobileUi::GetPortraitLayout(winW, winH);
                const f32 scale = (f32)layout.gameWidth / 384.0f;
                if (scale > 0.0f)
                {
                    g_AccumDx += dxPx / scale * sensitivity;
                    g_AccumDy += dyPx / scale * sensitivity;
                }
            }
            else
            {
                f32 rx, ry, rw, rh, scale;
                GetContainedRenderRect(&rx, &ry, &rw, &rh, &scale);
                if (scale > 0.0f)
                {
                    g_AccumDx += dxPx / scale * sensitivity;
                    g_AccumDy += dyPx / scale * sensitivity;
                }
            }
            g_MoveFinger.lastPxX = px;
            g_MoveFinger.lastPxY = py;
        }

        if (IsFinger(g_FocusFinger, f.fingerID))
        {
            g_FocusFinger.lastPxX = px;
            g_FocusFinger.lastPxY = py;
        }
    }
}

u16 Touch::GetButtonBits()
{
    ObserveTouchScene();
    u16 buttons = 0;

    g_BombedWithTouch = false;

    if (!IsGameplayTouchMode())
    {
        if (g_MenuGesture.pendingButton != 0)
        {
            buttons |= g_MenuGesture.pendingButton;
            g_MenuGesture.pendingButton = 0;
        }

        if (g_MenuGesture.active)
        {
            f32 dx = g_MenuGesture.currentX - g_MenuGesture.startX;
            f32 dy = g_MenuGesture.currentY - g_MenuGesture.startY;
            f32 threshold = GetSwipeThreshold();

            if (std::abs(dx) > threshold || std::abs(dy) > threshold)
            {
                if (std::abs(dx) > std::abs(dy))
                {
                    buttons |= (dx > 0) ? TH_BUTTON_RIGHT : TH_BUTTON_LEFT;
                }
                else
                {
                    buttons |= (dy > 0) ? TH_BUTTON_DOWN : TH_BUTTON_UP;
                }
            }
        }
    }

    if (!g_Gui.HasCurrentMsgIdx() && g_DialogueHoldFinger.active)
    {
        if (!g_MoveFinger.active)
        {
            AssignFinger(&g_MoveFinger, g_DialogueHoldFinger.id, g_DialogueHoldFinger.lastPxX,
                         g_DialogueHoldFinger.lastPxY);
        }
        ReleaseFinger(&g_DialogueHoldFinger);
    }
    else if (g_DialogueHoldFinger.active)
    {
        u64 held = SDL_GetTicks() - g_DialogueHoldFinger.start;

        if (held >= DIALOGUE_SKIP_HOLD_MS)
        {
            buttons |= TH_BUTTON_SKIP;
        }
    }

    // keep firing for a bit after release
    if (g_MoveFinger.active || SDL_GetTicks() - g_MoveFinger.end < 200)
    {
        buttons |= TH_BUTTON_SHOOT;
    }

    if (g_FocusFinger.active)
    {
        buttons |= TH_BUTTON_FOCUS;
    }

    if (g_BombPending)
    {
        buttons |= TH_BUTTON_BOMB;
        g_BombPending = false;
        g_BombedWithTouch = true;
    }

    if (g_PausePending && IsGameplayPauseGestureAllowed())
    {
        buttons |= TH_BUTTON_MENU;
        Touch::CancelTouches();
    }
    else if (g_PausePending)
    {
        // A finger sequence can straddle a scene transition. Never carry its
        // pause edge into the difficulty/loadout menu.
        g_PausePending = false;
    }

    return buttons;
}

u16 Touch::ApplyDialogueButtonPolicy(u16 buttons)
{
    if (!g_Gui.HasCurrentMsgIdx())
    {
        g_DialogueAdvancePending = false;
        g_DialogueReleaseSent = false;
        return buttons;
    }

    if (!g_DialogueAdvancePending)
    {
        return buttons;
    }

    // A latched virtual Z or a held controller Shoot button can otherwise hide
    // the edge generated by a dialogue tap. Record one released frame followed
    // by one pressed frame so gameplay and Replay observe the same input stream.
    if (!g_DialogueReleaseSent)
    {
        g_DialogueReleaseSent = true;
        MobileDiagnostics::Log("mobile/dialogue", "forced Z release frame");
        return buttons & ~TH_BUTTON_SHOOT;
    }

    g_DialogueAdvancePending = false;
    g_DialogueReleaseSent = false;
    MobileDiagnostics::Log("mobile/dialogue", "sent Z press frame");
    return (buttons & ~TH_BUTTON_SHOOT) | TH_BUTTON_SHOOT;
}

bool Touch::IsFocus()
{
    return g_FocusFinger.active;
}

bool Touch::GetPlayerDelta(f32 *dx, f32 *dy)
{
    if (g_ReplayDeltaPending)
    {
        *dx = g_ReplayDeltaX;
        *dy = g_ReplayDeltaY;
        g_ReplayDeltaPending = false;
        return true;
    }

    if (!g_MoveFinger.active)
    {
        *dx = 0.0f;
        *dy = 0.0f;
        return false;
    }

    *dx = g_AccumDx;
    *dy = g_AccumDy;

    return true;
}

void Touch::SetPlayerDelta(f32 dx, f32 dy)
{
    g_AccumDx = dx;
    g_AccumDy = dy;
}

void Touch::ConsumePlayerDelta(f32 dx, f32 dy)
{
    g_AccumDx -= dx;
    g_AccumDy -= dy;
}

void Touch::RecordAppliedPlayerDelta(f32 dx, f32 dy)
{
    if (dx == 0.0f && dy == 0.0f) return;
    g_RecordedDeltaX = dx;
    g_RecordedDeltaY = dy;
    g_RecordedDeltaPending = true;
}

bool Touch::ConsumeRecordedPlayerDelta(f32 *dx, f32 *dy)
{
    if (!g_RecordedDeltaPending || !dx || !dy) return false;
    *dx = g_RecordedDeltaX;
    *dy = g_RecordedDeltaY;
    g_RecordedDeltaPending = false;
    return true;
}

void Touch::InjectReplayPlayerDelta(f32 dx, f32 dy)
{
    g_UsedThisRun = true;
    g_ReplayDeltaX = dx;
    g_ReplayDeltaY = dy;
    g_ReplayDeltaPending = true;
}

void Touch::MarkReplayUsesTouch()
{
    g_UsedThisRun = true;
}
