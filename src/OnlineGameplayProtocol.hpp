#pragma once

#include <algorithm>
#include <cmath>

#include "inttypes.hpp"

enum OnlineGameplayPolicyFlag : u8
{
    ONLINE_GAMEPLAY_AUTO_BOMB = 1 << 0,
    ONLINE_GAMEPLAY_TOUCH_USED = 1 << 1,
    ONLINE_GAMEPLAY_TOUCH_BOMB = 1 << 2,
};

inline u8 OnlineBuildGameplayPolicy(bool autoBomb, bool touchUsed,
                                    bool touchBomb)
{
    return (autoBomb ? ONLINE_GAMEPLAY_AUTO_BOMB : 0) |
           (touchUsed ? ONLINE_GAMEPLAY_TOUCH_USED : 0) |
           (touchBomb ? ONLINE_GAMEPLAY_TOUCH_BOMB : 0);
}

inline bool OnlineGameplayPolicyHas(u8 policy, OnlineGameplayPolicyFlag flag)
{
    return (policy & (u8)flag) != 0;
}

// Gameplay touch movement is serialized at 1/16-pixel precision. The sender
// must simulate the decoded wire value too; otherwise each local player moves
// by a slightly different amount on the two peers.
inline i16 OnlineEncodeTouchDelta(f32 value)
{
    if (!std::isfinite(value)) return 0;
    return (i16)std::clamp((i32)lroundf(value * 16.0f), -32768, 32767);
}

inline f32 OnlineDecodeTouchDelta(i16 value)
{
    return (f32)value / 16.0f;
}

inline f32 OnlineCanonicalTouchDelta(f32 value)
{
    return OnlineDecodeTouchDelta(OnlineEncodeTouchDelta(value));
}

// Frames before inputDelay are deterministic neutral input and never cross
// the network. Once the first real packet arrives, they form the cumulative
// prefix of the receiver's ACK window.
inline u32 OnlineSeedDelayedAckPrefix(u32 contiguousFrame, u32 inputDelay)
{
    if (contiguousFrame == 0xffffffffu && inputDelay > 0)
        return inputDelay - 1;
    return contiguousFrame;
}
