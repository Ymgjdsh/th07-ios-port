#pragma once

#include "inttypes.hpp"

enum TH07OnlineLauncherAction
{
    TH07_ONLINE_ACTION_NONE = 0,
    TH07_ONLINE_ACTION_SET_MODE,
    TH07_ONLINE_ACTION_HOST,
    TH07_ONLINE_ACTION_GUEST,
    TH07_ONLINE_ACTION_LEAVE,
    TH07_ONLINE_ACTION_START_GAME,
    TH07_ONLINE_ACTION_START_LOCAL,
    TH07_ONLINE_ACTION_CLOSE,
    TH07_ONLINE_ACTION_SET_DELAY,
};

#ifdef TH07_IOS
extern "C" void TH07_IOS_PresentOnlineLauncher();
extern "C" void TH07_IOS_DismissOnlineLauncher();
extern "C" void TH07_IOS_ShowOnlineError(const char *message);
extern "C" int TH07_IOS_IsOnlineLauncherVisible();
extern "C" int TH07_IOS_PollOnlineLauncherAction(int *action, int *mode, int *delay,
                                                   char *directAddress, int directCapacity,
                                                   char *relayEndpoint, int endpointCapacity,
                                                   char *relayRoom, int roomCapacity);
extern "C" void TH07_IOS_UpdateOnlineLauncher(int mode, int state, int hostRole,
                                                int canStart, int delay, u32 rtt,
                                                const char *status, const char *peer,
                                                const char *directAddress,
                                                const char *relayEndpoint,
                                                const char *relayRoom);
#else
inline void TH07_IOS_PresentOnlineLauncher() {}
inline void TH07_IOS_DismissOnlineLauncher() {}
inline void TH07_IOS_ShowOnlineError(const char *) {}
inline int TH07_IOS_IsOnlineLauncherVisible() { return 0; }
inline int TH07_IOS_PollOnlineLauncherAction(int *, int *, int *, char *, int,
                                              char *, int, char *, int) { return 0; }
inline void TH07_IOS_UpdateOnlineLauncher(int, int, int, int, int, u32,
                                           const char *, const char *, const char *,
                                           const char *, const char *) {}
#endif
