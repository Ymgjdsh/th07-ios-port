#include "OnlineFrameHistory.hpp"
#include "OnlineStartupProtocol.hpp"

#include <cassert>
#include <cstdio>

struct TestInput
{
    u16 buttons = 0;
};

int main()
{
    assert(OnlineIsImmediatelyPreviousEpoch(0, 1));
    assert(OnlineIsImmediatelyPreviousEpoch(41, 42));
    assert(!OnlineIsImmediatelyPreviousEpoch(42, 42));
    assert(!OnlineIsImmediatelyPreviousEpoch(40, 42));
    assert(!OnlineIsImmediatelyPreviousEpoch(0xffffffffu, 0));

    // Lockstep begins only after MENU_COMMIT and only packets from the exact
    // committed epoch may touch frame history or authoritative state.
    assert(!OnlineAcceptsLockstepEpoch(false, 0, 0));
    assert(!OnlineAcceptsLockstepEpoch(false, 1, 1));
    assert(OnlineAcceptsLockstepEpoch(true, 1, 1));
    assert(!OnlineAcceptsLockstepEpoch(true, 0, 1));
    assert(!OnlineAcceptsLockstepEpoch(true, 2, 1));

    OnlineFrameHistory<TestInput, 8> history;
    history.Store(2, {22});
    history.Store(0, {10});
    history.Store(1, {11});
    assert(history.Find(0)->value.buttons == 10);
    assert(history.Find(1)->value.buttons == 11);
    assert(history.Find(2)->value.buttons == 22);

    // With no contiguous ACK yet, the bit mask acknowledges exact early
    // frames. This models receiving frames 0 and 2 before frame 1.
    history.Acknowledge(0xffffffffu, (1u << 0) | (1u << 2));
    assert(history.Find(0)->acknowledged);
    assert(!history.Find(1)->acknowledged);
    assert(history.Find(2)->acknowledged);

    history.Clear();
    for (u32 frame = 10; frame <= 14; ++frame) history.Store(frame, {(u16)frame});
    history.Acknowledge(11, (1u << 1)); // <=11 and frame 13 are acknowledged.
    assert(history.Find(10)->acknowledged);
    assert(history.Find(11)->acknowledged);
    assert(!history.Find(12)->acknowledged);
    assert(history.Find(13)->acknowledged);
    assert(!history.Find(14)->acknowledged);

    // A wrapped slot must only match its new exact frame number.
    history.Store(18, {118});
    assert(history.Find(10) == nullptr);
    assert(history.Find(18)->value.buttons == 118);

    // Simulate a first UDP pass that loses frames 1 and 4. Selective ACKs
    // leave only those exact frames pending; a retransmission pass fills both
    // holes and permits a cumulative ACK through frame 6.
    OnlineFrameHistory<TestInput, 16> sender;
    bool received[7] = {};
    for (u32 frame = 0; frame < 7; ++frame)
    {
        sender.Store(frame, {(u16)(100 + frame)});
        if (frame != 1 && frame != 4) received[frame] = true;
    }
    u32 firstMask = 0;
    for (u32 frame = 1; frame < 7; ++frame)
        if (received[frame]) firstMask |= 1u << (frame - 1);
    sender.Acknowledge(0, firstMask);
    assert(!sender.Find(1)->acknowledged);
    assert(!sender.Find(4)->acknowledged);
    for (u32 frame = 0; frame < 7; ++frame)
        if (!sender.Find(frame)->acknowledged) received[frame] = true;
    for (bool value : received) assert(value);
    sender.Acknowledge(6, 0);
    for (u32 frame = 0; frame < 7; ++frame) assert(sender.Find(frame)->acknowledged);

    std::puts("online frame history tests passed");
    return 0;
}
