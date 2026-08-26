#include "OnlineFrameHistory.hpp"
#include "OnlineGameplayProtocol.hpp"
#include "OnlineControlProtocol.hpp"
#include "OnlineStartupProtocol.hpp"

#include <cassert>
#include <cstdio>
#include <limits>

struct TestInput
{
    u16 buttons = 0;
};

struct ControlReceiver
{
    u32 lastSequence = 0;
    u16 buttons = 0;
    u8 context = kOnlineControlInvalidContext;
    int applied = 0;
    bool consumed = true;

    bool Deliver(u32 sequence, u16 nextButtons, u8 nextContext)
    {
        const bool fresh = OnlineControlSequenceIsNewer(sequence, lastSequence);
        if (fresh)
        {
            lastSequence = sequence;
            buttons = nextButtons;
            context = nextContext;
            consumed = false;
        }
        // A duplicate regenerates an ACK only after the command was consumed
        // on its intended page.
        return !fresh && sequence == lastSequence && consumed;
    }

    bool Consume(i32 localPage, bool selectingPlayer2)
    {
        if (consumed ||
            !OnlineControlContextMatches(localPage, selectingPlayer2, context))
            return false;
        consumed = true;
        ++applied;
        return true;
    }
};

struct ReliableMenuSender
{
    u32 sequence = 0;
    u16 buttons = 0;
    i32 cursor = -1;
    u8 context = kOnlineControlInvalidContext;
    bool pending = false;
    bool locallyConsumed = false;

    void Queue(i32 page, bool selectingPlayer2, u16 nextButtons, i32 nextCursor)
    {
        assert(!pending);
        ++sequence;
        buttons = nextButtons;
        cursor = nextCursor;
        context = OnlineControlEncodeContext(page, selectingPlayer2);
        pending = true;
        locallyConsumed = false;
    }

    bool ConsumeLocal(i32 page, bool selectingPlayer2)
    {
        if (!pending || locallyConsumed ||
            !OnlineControlContextMatches(page, selectingPlayer2, context))
            return false;
        locallyConsumed = true;
        return true;
    }

    void Acknowledge(u32 acknowledgedSequence)
    {
        if (pending && acknowledgedSequence == sequence) pending = false;
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
    assert(OnlineControlEncodeContext(5, false) == 0x05);
    assert(OnlineControlEncodeContext(5, true) == 0x85);
    assert(OnlineControlContextMatches(5, false, 0x05));
    assert(!OnlineControlContextMatches(5, true, 0x05));
    assert(OnlineControlContextMatches(5, true, 0x85));
    assert(OnlineControlLocalOwnsMenu(true, false));
    assert(!OnlineControlLocalOwnsMenu(false, false));
    assert(!OnlineControlLocalOwnsMenu(true, true));
    assert(OnlineControlLocalOwnsMenu(false, true));
    assert(OnlineControlPressedButtons(0x0080, 0) == 0x0080);
    assert(OnlineControlPressedButtons(0x0080, 0x0080) == 0);
    assert(OnlineControlPressedButtons(0, 0x0080) == 0); // release is not a command
    assert(!OnlineUsesGameplayLockstep(true, false, false));
    assert(OnlineUsesGameplayLockstep(true, false, true));
    assert(!OnlineUsesGameplayLockstep(true, true, true));

    // Both peers must simulate the exact value carried on the wire. Build 33
    // kept the sender's unquantized touch delta and decoded 1/16-pixel values
    // on the receiver, so player position diverged as soon as touch movement
    // began.
    assert(OnlineEncodeTouchDelta(0.1f) == 2);
    assert(OnlineCanonicalTouchDelta(0.1f) == 0.125f);
    assert(OnlineDecodeTouchDelta(OnlineEncodeTouchDelta(-1.03f)) ==
           OnlineCanonicalTouchDelta(-1.03f));
    assert(OnlineEncodeTouchDelta(100000.0f) == 32767);
    assert(OnlineEncodeTouchDelta(-100000.0f) == -32768);
    assert(OnlineEncodeTouchDelta(std::numeric_limits<f32>::quiet_NaN()) == 0);

    // Local prediction follows the same canonical, frame-ordered path that
    // lockstep will later simulate. Boundary clamps are applied after every
    // queued frame, so reversing away from an edge matches the peer trajectory.
    OnlineLocalTouchPrediction prediction;
    assert(prediction.Queue(8, 10.0f, 0.0f));
    assert(prediction.Queue(9, -3.0f, 2.0f));
    assert(prediction.Queue(9, -3.0f, 2.0f)); // repeated poll is idempotent
    assert(prediction.Count() == 2);
    f32 predictedX = 0.0f, predictedY = 0.0f;
    prediction.Predict(380.0f, 200.0f, 0.0f, 384.0f, 0.0f, 448.0f,
                       &predictedX, &predictedY);
    assert(predictedX == 381.0f && predictedY == 202.0f);
    assert(!prediction.Consume(7));
    assert(prediction.Consume(8));
    prediction.Predict(384.0f, 200.0f, 0.0f, 384.0f, 0.0f, 448.0f,
                       &predictedX, &predictedY);
    assert(predictedX == 381.0f && predictedY == 202.0f);
    assert(prediction.Consume(9));
    assert(prediction.Count() == 0);
    prediction.Reset();

    // Mobile preferences that alter collision/death behavior belong to the
    // logical input frame, not to whichever device setting is live when a
    // delayed packet is finally consumed.
    const u8 touchAutoBomb = OnlineBuildGameplayPolicy(true, true, true);
    assert(OnlineGameplayPolicyHas(touchAutoBomb, ONLINE_GAMEPLAY_AUTO_BOMB));
    assert(OnlineGameplayPolicyHas(touchAutoBomb, ONLINE_GAMEPLAY_TOUCH_USED));
    assert(OnlineGameplayPolicyHas(touchAutoBomb, ONLINE_GAMEPLAY_TOUCH_BOMB));
    const u8 touchOnly = OnlineBuildGameplayPolicy(false, true, false);
    assert(!OnlineGameplayPolicyHas(touchOnly, ONLINE_GAMEPLAY_AUTO_BOMB));
    assert(OnlineGameplayPolicyHas(touchOnly, ONLINE_GAMEPLAY_TOUCH_USED));
    assert(!OnlineGameplayPolicyHas(touchOnly, ONLINE_GAMEPLAY_TOUCH_BOMB));

    // A pause commit created while frame 241 is being consumed cannot target
    // any of the already-captured frames through 249 at input delay 8.
    assert(OnlineFirstUnsampledInputFrame(241, 8) == 250);
    assert(OnlineFirstUnsampledInputFrame(77, 3) == 81);
    // Controller.hpp values: UP, DOWN, SELECTMENU (ENTER|SHOOT), MENU and Q.
    constexpr u16 shellActions = 0x0010 | 0x0020 | 0x1001 | 0x0008 | 0x0200;
    assert(!OnlineShellInputCanArm(0x0001, shellActions, false));
    assert(!OnlineShellInputCanArm(0x0010, shellActions, false));
    assert(OnlineShellInputCanArm(0, shellActions, false));
    assert(OnlineShellInputCanArm(0x0010, shellActions, true));

    // The input-delay prefix is synthesized rather than sent. Receiving the
    // first real frame seeds that prefix into the cumulative ACK window even
    // after frames 0..7 have already been consumed from the ring.
    assert(OnlineSeedDelayedAckPrefix(0xffffffffu, 8) == 7);
    assert(OnlineSeedDelayedAckPrefix(0xffffffffu, 0) == 0xffffffffu);
    assert(OnlineSeedDelayedAckPrefix(12, 8) == 12);

    // Simulate the reliable one-shot menu channel. The first difficulty
    // tap packet and its first ACK are lost; retransmission applies the tap
    // exactly once and a duplicate only regenerates the ACK. A delayed older
    // command cannot replace a newer event.
    ControlReceiver menuPeer;
    const u32 difficultyTap = 1;
    bool acknowledged = false;
    (void)acknowledged; // first network send is dropped
    acknowledged = menuPeer.Deliver(difficultyTap, 0x0100,
                                    OnlineControlEncodeContext(4, false));
    assert(!acknowledged);
    assert(!menuPeer.Consume(0, false)); // Peer has not reached difficulty yet.
    assert(menuPeer.Consume(4, false));
    assert(menuPeer.buttons == 0x0100);
    assert(menuPeer.applied == 1);
    acknowledged = menuPeer.Deliver(difficultyTap, 0x0100,
                                    OnlineControlEncodeContext(4, false)); // ACK was lost
    assert(acknowledged && menuPeer.applied == 1);
    assert(!menuPeer.Deliver(2, 0x0080, OnlineControlEncodeContext(5, false)));
    assert(!menuPeer.Consume(4, false));
    assert(menuPeer.Consume(5, false));
    assert(menuPeer.buttons == 0x0080 && menuPeer.applied == 2);
    assert(!menuPeer.Deliver(1, 0x0100, OnlineControlEncodeContext(4, false)));
    assert(menuPeer.buttons == 0x0080 && menuPeer.applied == 2);

    // Full two-owner loadout path. The host consumes P1 character locally and
    // changes to the shot page before its first packet reaches the guest. A
    // retransmission must retain the original character/P1 context. This is
    // the Build 30 failure: retransmission used the sender's new page and left
    // the receiver stuck forever on character selection.
    constexpr u16 select = 0x1001;
    constexpr u16 right = 0x0080;
    ReliableMenuSender hostMenu;
    ControlReceiver guestMenu;
    hostMenu.Queue(5, false, select, 1);
    assert(hostMenu.ConsumeLocal(5, false));
    const u8 stableP1CharacterContext = hostMenu.context;
    assert(stableP1CharacterContext == OnlineControlEncodeContext(5, false));
    // The sender is now on page 6, but the stored wire context is still page 5.
    assert(!hostMenu.ConsumeLocal(6, false));
    assert(hostMenu.context == stableP1CharacterContext);
    assert(!guestMenu.Deliver(hostMenu.sequence, hostMenu.buttons, hostMenu.context));
    assert(guestMenu.Consume(5, false));
    assert(guestMenu.buttons == select && guestMenu.applied == 1);
    assert(guestMenu.Deliver(hostMenu.sequence, hostMenu.buttons,
                             hostMenu.context)); // duplicate regenerates ACK
    hostMenu.Acknowledge(hostMenu.sequence);
    assert(!hostMenu.pending);

    hostMenu.Queue(6, false, select, 0);
    assert(hostMenu.ConsumeLocal(6, false));
    assert(!guestMenu.Deliver(hostMenu.sequence, hostMenu.buttons, hostMenu.context));
    assert(guestMenu.Consume(6, false));
    hostMenu.Acknowledge(hostMenu.sequence);

    // Both peers now revisit character selection for P2. A delayed P1
    // character context cannot match this identically numbered page.
    assert(!OnlineControlContextMatches(5, true, stableP1CharacterContext));
    ReliableMenuSender guestOwnerMenu;
    ControlReceiver hostMenuPeer;
    guestOwnerMenu.Queue(5, true, right, -1);
    assert(guestOwnerMenu.ConsumeLocal(5, true));
    assert(!hostMenuPeer.Deliver(guestOwnerMenu.sequence, guestOwnerMenu.buttons,
                                 guestOwnerMenu.context));
    assert(!hostMenuPeer.Consume(5, false));
    assert(hostMenuPeer.Consume(5, true));
    assert(hostMenuPeer.applied == 1);
    assert(hostMenuPeer.Deliver(guestOwnerMenu.sequence, guestOwnerMenu.buttons,
                                guestOwnerMenu.context));
    assert(hostMenuPeer.applied == 1); // retransmit never moves twice
    guestOwnerMenu.Acknowledge(guestOwnerMenu.sequence);

    guestOwnerMenu.Queue(5, true, select, 1);
    assert(guestOwnerMenu.ConsumeLocal(5, true));
    assert(!hostMenuPeer.Deliver(guestOwnerMenu.sequence, guestOwnerMenu.buttons,
                                 guestOwnerMenu.context));
    assert(hostMenuPeer.Consume(5, true));
    guestOwnerMenu.Acknowledge(guestOwnerMenu.sequence);
    guestOwnerMenu.Queue(6, true, select, 0);
    assert(guestOwnerMenu.ConsumeLocal(6, true));
    assert(!hostMenuPeer.Deliver(guestOwnerMenu.sequence, guestOwnerMenu.buttons,
                                 guestOwnerMenu.context));
    assert(hostMenuPeer.Consume(6, true));
    guestOwnerMenu.Acknowledge(guestOwnerMenu.sequence);
    assert(hostMenuPeer.applied == 3); // P2 move, character confirm, shot confirm

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

    OnlineFrameHistory<TestInput, 16> delayedSender;
    for (u32 frame = 0; frame <= 8; ++frame)
        delayedSender.Store(frame, {(u16)frame});
    delayedSender.Acknowledge(OnlineSeedDelayedAckPrefix(0xffffffffu, 8), 1u);
    for (u32 frame = 0; frame <= 8; ++frame)
        assert(delayedSender.Find(frame)->acknowledged);

    std::puts("online protocol 18 local touch prediction, shell handoff and ACK recovery tests passed");
    return 0;
}
