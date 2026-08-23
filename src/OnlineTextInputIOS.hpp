#pragma once

#ifdef TH07_IOS
extern "C" void TH07_IOS_RequestOnlineText(int field, const char *title, const char *value);
extern "C" int TH07_IOS_PollOnlineText(int *field, char *value, int capacity);
#else
inline void TH07_IOS_RequestOnlineText(int, const char *, const char *) {}
inline int TH07_IOS_PollOnlineText(int *, char *, int) { return 0; }
#endif
