#pragma once

#include "inttypes.hpp"
#include <SDL3/SDL.h>

namespace Touch
{
constexpr i32 DEATHBOMB_TOLERANCE = 5;

// Returns true only while the stage simulation is ready to accept gameplay
// touch actions (including the pause button).  Menu/stage-construction frames
// can temporarily report notInMenu; callers must use this stricter predicate
// before turning a touch into a gameplay command.
bool IsGameplayActive();

void FingerDown(const SDL_TouchFingerEvent &f);
void FingerUp(const SDL_TouchFingerEvent &f);
void FingerMotion(const SDL_TouchFingerEvent &f);

u16 GetButtonBits();
u16 ApplyDialogueButtonPolicy(u16 buttons);

bool IsFocus();

bool GetPlayerDelta(f32 *dx, f32 *dy);
void SetPlayerDelta(f32 dx, f32 dy);
void ConsumePlayerDelta(f32 dx, f32 dy);
void RecordAppliedPlayerDelta(f32 dx, f32 dy);
bool ConsumeRecordedPlayerDelta(f32 *dx, f32 *dy);
void InjectReplayPlayerDelta(f32 dx, f32 dy);
void MarkReplayUsesTouch();

bool WasUsedThisRun();
bool UsedTouchToBomb();
void ResetRunUsage();
void CancelTouches();
} // namespace Touch
