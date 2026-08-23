#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <MultipeerConnectivity/MultipeerConnectivity.h>

#include "BluetoothPeerTransport.hpp"
#include <deque>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

@interface TH07PeerBridge : NSObject <MCSessionDelegate, MCNearbyServiceAdvertiserDelegate,
                                      MCNearbyServiceBrowserDelegate> {
@public
    MCPeerID *peerId;
    MCSession *session;
    MCNearbyServiceAdvertiser *advertiser;
    MCNearbyServiceBrowser *browser;
    std::mutex mutex;
    std::deque<std::vector<uint8_t>> packets;
    BOOL hostRole;
    BOOL connected;
    std::string status;
}
- (void)start:(BOOL)role;
- (void)stop;
- (BOOL)sendBytes:(const void *)bytes size:(int)size reliable:(BOOL)reliable;
@end

static TH07PeerBridge *gBridge = nil;
static NSString *const kServiceType = @"th07-peer";

@implementation TH07PeerBridge
- (instancetype)init {
    self = [super init];
    if (self) {
        status = "idle";
        peerId = [[MCPeerID alloc] initWithDisplayName:[[UIDevice currentDevice] name]];
        session = [[MCSession alloc] initWithPeer:peerId securityIdentity:nil
                              encryptionPreference:MCEncryptionNone];
        session.delegate = self;
    }
    return self;
}
- (void)dealloc {
    [self stop];
    session.delegate = nil;
    [session release];
    [peerId release];
    [super dealloc];
}
- (void)start:(BOOL)role {
    [self stop];
    {
        std::lock_guard<std::mutex> lock(mutex);
        hostRole = role;
        connected = NO;
        packets.clear();
        status = role ? "waiting for nearby guest..." : "searching nearby host...";
    }
    if (role) {
        advertiser = [[MCNearbyServiceAdvertiser alloc] initWithPeer:peerId
            discoveryInfo:@{@"role": @"host", @"protocol": @"11"} serviceType:kServiceType];
        advertiser.delegate = self;
        [advertiser startAdvertisingPeer];
    } else {
        browser = [[MCNearbyServiceBrowser alloc] initWithPeer:peerId serviceType:kServiceType];
        browser.delegate = self;
        [browser startBrowsingForPeers];
    }
}
- (void)stop {
    [advertiser stopAdvertisingPeer]; advertiser.delegate = nil; [advertiser release]; advertiser = nil;
    [browser stopBrowsingForPeers]; browser.delegate = nil; [browser release]; browser = nil;
    [session disconnect];
    std::lock_guard<std::mutex> lock(mutex);
    connected = NO; packets.clear(); status = "idle";
}
- (BOOL)sendBytes:(const void *)bytes size:(int)size reliable:(BOOL)reliable {
    if (!bytes || size <= 0) return NO;
    NSArray<MCPeerID *> *peers = session.connectedPeers;
    if (peers.count == 0) return NO;
    NSData *data = [NSData dataWithBytes:bytes length:(NSUInteger)size];
    NSError *error = nil;
    BOOL ok = [session sendData:data toPeers:peers
                       withMode:reliable ? MCSessionSendDataReliable : MCSessionSendDataUnreliable
                          error:&error];
    if (!ok && error.localizedDescription.UTF8String) {
        std::lock_guard<std::mutex> lock(mutex); status = error.localizedDescription.UTF8String;
    }
    return ok;
}
- (void)advertiser:(MCNearbyServiceAdvertiser *)source didReceiveInvitationFromPeer:(MCPeerID *)peer
       withContext:(NSData *)context invitationHandler:(void (^)(BOOL, MCSession *))handler {
    (void)peer; (void)context;
    handler(source == advertiser && hostRole, source == advertiser && hostRole ? session : nil);
}
- (void)browser:(MCNearbyServiceBrowser *)source foundPeer:(MCPeerID *)peer
 withDiscoveryInfo:(NSDictionary<NSString *, NSString *> *)info {
    if (source != browser || hostRole || ![info[@"role"] isEqualToString:@"host"] ||
        ![info[@"protocol"] isEqualToString:@"11"]) return;
    [source invitePeer:peer toSession:session withContext:nil timeout:10.0];
}
- (void)browser:(MCNearbyServiceBrowser *)source lostPeer:(MCPeerID *)peer { (void)source; (void)peer; }
- (void)session:(MCSession *)source peer:(MCPeerID *)peer didChangeState:(MCSessionState)state {
    (void)peer;
    if (source != session) return;
    std::lock_guard<std::mutex> lock(mutex);
    connected = state == MCSessionStateConnected;
    status = connected ? "connected" : (state == MCSessionStateConnecting ? "connecting..." : "nearby disconnected");
}
- (void)session:(MCSession *)source didReceiveData:(NSData *)data fromPeer:(MCPeerID *)peer {
    (void)peer;
    if (source != session || data.length == 0) return;
    std::vector<uint8_t> packet((const uint8_t *)data.bytes, (const uint8_t *)data.bytes + data.length);
    std::lock_guard<std::mutex> lock(mutex);
    if (packets.size() >= 256) packets.pop_front();
    packets.push_back(std::move(packet));
}
- (void)session:(MCSession *)source didReceiveStream:(NSInputStream *)stream withName:(NSString *)name fromPeer:(MCPeerID *)peer { (void)source; (void)stream; (void)name; (void)peer; }
- (void)session:(MCSession *)source didStartReceivingResourceWithName:(NSString *)name fromPeer:(MCPeerID *)peer withProgress:(NSProgress *)progress { (void)source; (void)name; (void)peer; (void)progress; }
- (void)session:(MCSession *)source didFinishReceivingResourceWithName:(NSString *)name fromPeer:(MCPeerID *)peer atURL:(NSURL *)url withError:(NSError *)error { (void)source; (void)name; (void)peer; (void)url; (void)error; }
- (void)browser:(MCNearbyServiceBrowser *)source didNotStartBrowsingForPeers:(NSError *)error {
    if (source != browser) return; std::lock_guard<std::mutex> lock(mutex);
    status = error.localizedDescription.UTF8String ?: "browser failed";
}
- (void)advertiser:(MCNearbyServiceAdvertiser *)source didNotStartAdvertisingPeer:(NSError *)error {
    if (source != advertiser) return; std::lock_guard<std::mutex> lock(mutex);
    status = error.localizedDescription.UTF8String ?: "advertiser failed";
}
@end

extern "C" int TH07_IOS_BluetoothAvailable() { return 1; }
extern "C" int TH07_IOS_BluetoothStart(int hostRole) { @autoreleasepool { if (!gBridge) gBridge = [TH07PeerBridge new]; [gBridge start:hostRole ? YES : NO]; return 1; } }
extern "C" void TH07_IOS_BluetoothStop() { @autoreleasepool { [gBridge stop]; } }
extern "C" int TH07_IOS_BluetoothIsConnected() { if (!gBridge) return 0; std::lock_guard<std::mutex> lock(gBridge->mutex); return gBridge->connected; }
extern "C" int TH07_IOS_BluetoothSend(const void *bytes, int size, int reliable) { if (!gBridge) return 0; @autoreleasepool { return [gBridge sendBytes:bytes size:size reliable:reliable != 0]; } }
extern "C" int TH07_IOS_BluetoothPoll(void *bytes, int capacity) {
    if (!gBridge || !bytes || capacity <= 0) return 0;
    std::lock_guard<std::mutex> lock(gBridge->mutex); if (gBridge->packets.empty()) return 0;
    std::vector<uint8_t> packet = std::move(gBridge->packets.front()); gBridge->packets.pop_front();
    if ((int)packet.size() > capacity) return -1; std::memcpy(bytes, packet.data(), packet.size()); return (int)packet.size();
}
extern "C" const char *TH07_IOS_BluetoothStatus() { static std::string value; if (!gBridge) return "unavailable"; std::lock_guard<std::mutex> lock(gBridge->mutex); value = gBridge->status; return value.c_str(); }
