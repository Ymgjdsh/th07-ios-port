#pragma once

#include "inttypes.hpp"

// Platform-neutral rules for protocol 11's reliable setup/lifecycle channel.
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

inline bool OnlineUsesGameplayLockstep(bool multiplayer, bool localSession,
                                       bool gameCommitted)
{
    return multiplayer && !localSession && gameCommitted;
}
