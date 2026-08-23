#pragma once

#include <cstddef>
#include "inttypes.hpp"

// Exact-frame ring storage. A wrapped slot is never mistaken for the frame
// that previously occupied it, which is essential when UDP packets arrive
// late or out of order.
template <typename T, std::size_t Capacity>
class OnlineFrameHistory
{
public:
    struct Slot
    {
        u32 frame = 0;
        T value = {};
        bool present = false;
        bool acknowledged = false;
        u32 lastSendMs = 0;
    };

    void Clear()
    {
        for (Slot &slot : slots_) slot = {};
    }

    Slot &Store(u32 frame, const T &value)
    {
        Slot &slot = slots_[frame % Capacity];
        if (!slot.present || slot.frame != frame)
        {
            slot = {};
            slot.frame = frame;
        }
        slot.value = value;
        slot.present = true;
        return slot;
    }

    Slot *Find(u32 frame)
    {
        Slot &slot = slots_[frame % Capacity];
        return slot.present && slot.frame == frame ? &slot : nullptr;
    }

    const Slot *Find(u32 frame) const
    {
        const Slot &slot = slots_[frame % Capacity];
        return slot.present && slot.frame == frame ? &slot : nullptr;
    }

    bool Has(u32 frame) const { return Find(frame) != nullptr; }

    void Erase(u32 frame)
    {
        Slot *slot = Find(frame);
        if (slot) *slot = {};
    }

    void Acknowledge(u32 contiguousFrame, u32 followingMask)
    {
        for (Slot &slot : slots_)
        {
            if (!slot.present) continue;
            if (contiguousFrame != 0xffffffffu && slot.frame <= contiguousFrame)
            {
                slot.acknowledged = true;
                continue;
            }
            if (contiguousFrame == 0xffffffffu)
            {
                if (slot.frame < 32 && (followingMask & (1u << slot.frame)))
                    slot.acknowledged = true;
                continue;
            }
            const u32 delta = slot.frame - contiguousFrame - 1;
            if (slot.frame > contiguousFrame && delta < 32 &&
                (followingMask & (1u << delta)))
                slot.acknowledged = true;
        }
    }

private:
    Slot slots_[Capacity] = {};
};
