#pragma once

#include "inttypes.hpp"

// Platform-neutral rules for protocol 12's reliable setup/lifecycle channel.
// The wire transport may duplicate or reorder snapshots; only the newest
// sequence changes the replicated state.
inline bool OnlineControlSequenceIsNewer(u32 candidate, u32 current)
{
    return candidate != current && (i32)(candidate - current) > 0;
}

inline bool OnlineControlShouldFreeze(bool localBackgrounded,
                                      bool peerBackgrounded,
                                      bool awaitingResumeAck)
{
    return localBackgrounded || peerBackgrounded || awaitingResumeAck;
}

inline bool OnlineControlPageMatches(i32 localPage, i32 remotePage)
{
    return localPage >= 0 && localPage == remotePage;
}

constexpr u8 kOnlineControlInvalidContext = 0xff;

// Character and shot pages are visited once for P1 and again for P2. The
// player phase is part of the wire context so an old P1 packet can never be
// consumed on the identically numbered P2 page.
inline u8 OnlineControlEncodeContext(i32 page, bool selectingPlayer2)
{
    if (page < 0 || page >= 0x7f) return kOnlineControlInvalidContext;
    return (u8)page | (selectingPlayer2 ? 0x80u : 0u);
}

inline bool OnlineControlContextMatches(i32 localPage, bool selectingPlayer2,
                                        u8 remoteContext)
{
    const u8 localContext = OnlineControlEncodeContext(localPage, selectingPlayer2);
    return localContext != kOnlineControlInvalidContext && localContext == remoteContext;
}

inline bool OnlineControlLocalOwnsMenu(bool host, bool selectingPlayer2)
{
    return host != selectingPlayer2;
}

inline u16 OnlineControlPressedButtons(u16 current, u16 previous)
{
    return current & (u16)~previous;
}

inline bool OnlineUsesGameplayLockstep(bool multiplayer, bool localSession,
                                       bool gameCommitted)
{
    return multiplayer && !localSession && gameCommitted;
}
