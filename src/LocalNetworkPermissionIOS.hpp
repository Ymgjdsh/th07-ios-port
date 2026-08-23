#pragma once

#ifdef TH07_IOS
extern "C" void TH07_IOS_TriggerLocalNetworkPermission();
extern "C" void TH07_IOS_StopLocalNetworkPermissionProbe();
extern "C" int TH07_IOS_GetLocalNetworkPermissionState();
extern "C" void TH07_IOS_StartBonjourHost(int port);
extern "C" void TH07_IOS_StopBonjourHost();
extern "C" int TH07_IOS_PollBonjourHost(char *host, int capacity, int *port);
#else
inline void TH07_IOS_TriggerLocalNetworkPermission() {}
inline void TH07_IOS_StopLocalNetworkPermissionProbe() {}
inline int TH07_IOS_GetLocalNetworkPermissionState() { return 2; }
inline void TH07_IOS_StartBonjourHost(int) {}
inline void TH07_IOS_StopBonjourHost() {}
inline int TH07_IOS_PollBonjourHost(char *, int, int *) { return 0; }
#endif
