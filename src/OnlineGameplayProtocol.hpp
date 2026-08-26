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

// The local input for currentFrame + inputDelay has already been captured
// before shell actions are processed. A synchronized transition must target
// the following frame so the packet required to consume that frame is also
// guaranteed to carry the transition.
inline u32 OnlineFirstUnsampledInputFrame(u32 currentFrame, u32 inputDelay)
{
    return currentFrame + inputDelay + 1;
}

inline bool OnlineShellInputCanArm(u16 buttons, u16 actionMask,
                                   bool explicitSelection)
{
    return explicitSelection || (buttons & actionMask) == 0;
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

inline u8 OnlineLocalPlayerSlot(bool host, bool localSession)
{
    return host || localSession ? 0 : 1;
}

// Local touch prediction is presentation-only. It keeps the exact canonical
// deltas already assigned to future lockstep frames so the local renderer can
// show where those same inputs will move the player once input delay expires.
// Nothing in this buffer is serialized or included in synchronization hashes.
class OnlineLocalTouchPrediction
{
public:
    static constexpr u32 kCapacity = 32;

    void Reset()
    {
        head = 0;
        count = 0;
    }

    bool Queue(u32 frame, f32 dx, f32 dy)
    {
        if (count != 0)
        {
            Entry &last = entries[(head + count - 1) % kCapacity];
            if (last.frame == frame)
            {
                // SynchronizeInputs may be polled repeatedly while a peer
                // frame is missing. A frame must never be predicted twice.
                return true;
            }
        }
        if (count == kCapacity) return false;
        entries[(head + count) % kCapacity] = {frame, dx, dy};
        ++count;
        return true;
    }

    bool Consume(u32 frame)
    {
        // Discard an impossible stale entry defensively. Normal lockstep
        // consumption reaches every queued frame in exact order.
        while (count != 0 && (i32)(entries[head].frame - frame) < 0)
        {
            head = (head + 1) % kCapacity;
            --count;
        }
        if (count == 0 || entries[head].frame != frame) return false;
        head = (head + 1) % kCapacity;
        --count;
        return true;
    }

    void Predict(f32 baseX, f32 baseY, f32 minX, f32 maxX,
                 f32 minY, f32 maxY, f32 *predictedX,
                 f32 *predictedY) const
    {
        f32 x = baseX;
        f32 y = baseY;
        for (u32 i = 0; i < count; ++i)
        {
            const Entry &entry = entries[(head + i) % kCapacity];
            x = std::clamp(x + entry.dx, minX, maxX);
            y = std::clamp(y + entry.dy, minY, maxY);
        }
        if (predictedX) *predictedX = x;
        if (predictedY) *predictedY = y;
    }

    u32 Count() const { return count; }

private:
    struct Entry
    {
        u32 frame = 0;
        f32 dx = 0.0f;
        f32 dy = 0.0f;
    };

    Entry entries[kCapacity] = {};
    u32 head = 0;
    u32 count = 0;
};

// Frames before inputDelay are deterministic neutral input and never cross
// the network. Once the first real packet arrives, they form the cumulative
// prefix of the receiver's ACK window.
inline u32 OnlineSeedDelayedAckPrefix(u32 contiguousFrame, u32 inputDelay)
{
    if (contiguousFrame == 0xffffffffu && inputDelay > 0)
        return inputDelay - 1;
    return contiguousFrame;
}
