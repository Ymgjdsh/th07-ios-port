#pragma once

#include "inttypes.hpp"

inline bool OnlineIsImmediatelyPreviousEpoch(u32 packetEpoch, u32 currentEpoch)
{
    return packetEpoch < currentEpoch && currentEpoch - packetEpoch == 1;
}
