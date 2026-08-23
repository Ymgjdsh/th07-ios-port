#pragma once

#ifdef __cplusplus
extern "C" {
#endif
int TH07_IOS_BluetoothAvailable();
int TH07_IOS_BluetoothStart(int hostRole);
void TH07_IOS_BluetoothStop();
int TH07_IOS_BluetoothIsConnected();
int TH07_IOS_BluetoothSend(const void *bytes, int size, int reliable);
int TH07_IOS_BluetoothPoll(void *bytes, int capacity);
const char *TH07_IOS_BluetoothStatus();
#ifdef __cplusplus
}
#endif
