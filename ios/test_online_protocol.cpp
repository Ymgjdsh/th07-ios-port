#include "OnlineFrameHistory.hpp"
#include "OnlineControlProtocol.hpp"
#include "OnlineStartupProtocol.hpp"

#include <cassert>
#include <cstdio>

struct TestInput
{
    u16 buttons = 0;
};

struct ControlReceiver
{
    u32 lastSequence = 0;
    u16 buttons = 0;
    i32 page = -1;
    int applied = 0;
    bool consumed = true;

    bool Deliver(u32 sequence, u16 nextButtons, i32 nextPage)
    {
        const bool fresh = OnlineControlSequenceIsNewer(sequence, lastSequence);
        if (fresh)
        {
            lastSequence = sequence;
            buttons = nextButtons;
            page = nextPage;
            consumed = false;
        }
        // A duplicate regenerates an ACK only after the command was consumed
        // on its intended page.
        return !fresh && sequence == lastSequence && consumed;
    }

    bool Consume(i32 localPage)
    {
        if (consumed || !OnlineControlPageMatches(localPage, page)) return false;
        consumed = true;
        ++applied;
        return true;
    }
};

int main()
{
    // Setup/lifecycle packets are latest-value control snapshots, independent
    // of gameplay frame history. Sequence comparison remains valid at wrap.
    assert(OnlineControlSequenceIsNewer(11, 10));
    assert(!OnlineControlSequenceIsNewer(10, 10));
    assert(!OnlineControlSequenceIsNewer(9, 10));
    assert(OnlineControlSequenceIsNewer(0, 0xffffffffu));
    assert(!OnlineControlShouldFreeze(false, false, false));
    assert(OnlineControlShouldFreeze(true, false, false));
    assert(OnlineControlShouldFreeze(false, true, false));
    assert(OnlineControlShouldFreeze(false, false, true));
    assert(OnlineControlPageMatches(4, 4));
    assert(!OnlineControlPageMatches(3, 4));
    assert(!OnlineControlPageMatches(-1, -1));
    assert(!OnlineUsesGameplayLockstep(true, false, false));
    assert(OnlineUsesGameplayLockstep(true, false, true));
    assert(!OnlineUsesGameplayLockstep(true, true, true));

    // Simulate the reliable latest-value menu channel. The first difficulty
    // tap packet and its first ACK are lost; retransmission applies the tap
    // exactly once and a duplicate only regenerates the ACK. A delayed older
    // cursor snapshot cannot replace the newer release snapshot.
    ControlReceiver menuPeer;
    const u32 difficultyTap = 1;
    bool acknowledged = false;
    (void)acknowledged; // first network send is dropped
    acknowledged = menuPeer.Deliver(difficultyTap, 0x0100, 4);
    assert(!acknowledged);
    assert(!menuPeer.Consume(0)); // Peer has not reached difficulty yet.
    assert(menuPeer.Consume(4));
    assert(menuPeer.buttons == 0x0100);
    assert(menuPeer.applied == 1);
    acknowledged = menuPeer.Deliver(difficultyTap, 0x0100, 4); // ACK was lost
    assert(acknowledged && menuPeer.applied == 1);
    assert(!menuPeer.Deliver(2, 0, 5));
    assert(!menuPeer.Consume(4));
    assert(menuPeer.Consume(5));
    assert(menuPeer.buttons == 0 && menuPeer.applied == 2);
    assert(!menuPeer.Deliver(1, 0x0100, 4));
    assert(menuPeer.buttons == 0 && menuPeer.applied == 2);

    // Lifecycle handshake: both sides freeze as soon as A backgrounds. On
    // foreground A remains frozen until B's response arrives; B cannot run a
    // gameplay frame ahead because lockstep still lacks A's input.
    bool aLocalBackground = true;
    bool aPeerBackground = false;
    bool aAwaitingResume = false;
    bool bPeerBackground = true;
    assert(OnlineControlShouldFreeze(aLocalBackground, aPeerBackground,
                                     aAwaitingResume));
    assert(OnlineControlShouldFreeze(false, bPeerBackground, false));
    aLocalBackground = false;
    aAwaitingResume = true;
    assert(OnlineControlShouldFreeze(aLocalBackground, aPeerBackground,
                                     aAwaitingResume));
    bPeerBackground = false; // B received FOREGROUND and replies.
    aAwaitingResume = false; // A received B's reply.
    assert(!OnlineControlShouldFreeze(aLocalBackground, aPeerBackground,
                                      aAwaitingResume));
    assert(!OnlineControlShouldFreeze(false, bPeerBackground, false));

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

    std::puts("online protocol 11 control, reconnect and frame history tests passed");
    return 0;
}
