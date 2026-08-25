#pragma once

#include <algorithm>
#include <cmath>

#include "inttypes.hpp"

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
