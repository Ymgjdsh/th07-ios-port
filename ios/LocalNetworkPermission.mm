#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <SDL3/SDL.h>
#include <atomic>
#include <arpa/inet.h>
#include <cstdio>
#include <netinet/in.h>

static std::atomic<int> gPermissionState(0);
static NSNetService *gHostService = nil;

@interface TH07BonjourProbe : NSObject <NSNetServiceBrowserDelegate, NSNetServiceDelegate> {
    NSNetServiceBrowser *_browser;
    NSMutableArray *_services;
    NSString *_resolvedHost;
    NSInteger _resolvedPort;
    BOOL _searching;
}
- (void)start;
- (void)stop;
- (BOOL)consumeHost:(char *)host capacity:(int)capacity port:(int *)port;
@end

@implementation TH07BonjourProbe
- (id)init {
    self = [super init];
    if (self) { _browser = [[NSNetServiceBrowser alloc] init]; _browser.delegate = self; _services = [[NSMutableArray alloc] init]; }
    return self;
}
- (void)dealloc { [self stop]; [_browser release]; [_services release]; [_resolvedHost release]; [super dealloc]; }
- (void)start {
    if (_searching) return;
    gPermissionState.store(1);
    [_browser searchForServicesOfType:@"_th07-online._udp." inDomain:@"local."];
    SDL_Log("[local-network] Bonjour permission probe requested");
}
- (void)stop {
    [_browser stop];
    @synchronized(self) {
        for (NSNetService *service in _services) { [service stop]; service.delegate = nil; }
        [_services removeAllObjects];
    }
    _searching = NO;
    if (gPermissionState.load() != -1) gPermissionState.store(0);
}
- (void)netServiceBrowserWillSearch:(NSNetServiceBrowser *)browser { (void)browser; _searching = YES; gPermissionState.store(2); SDL_Log("[local-network] Bonjour browser ready"); }
- (void)netServiceBrowserDidStopSearch:(NSNetServiceBrowser *)browser { (void)browser; _searching = NO; if (gPermissionState.load() != -1) gPermissionState.store(0); }
- (void)netServiceBrowser:(NSNetServiceBrowser *)browser didNotSearch:(NSDictionary *)error {
    (void)browser; _searching = NO; gPermissionState.store(-1);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[local-network] Bonjour browser failed domain=%ld code=%ld",
                 (long)[error[NSNetServicesErrorDomain] integerValue], (long)[error[NSNetServicesErrorCode] integerValue]);
}
- (void)netServiceBrowser:(NSNetServiceBrowser *)browser didFindService:(NSNetService *)service moreComing:(BOOL)moreComing {
    (void)browser; (void)moreComing; @synchronized(self) { [_services addObject:service]; }
    service.delegate = self; [service resolveWithTimeout:4.0];
}
- (void)netServiceDidResolveAddress:(NSNetService *)service {
    char address[INET_ADDRSTRLEN] = {};
    for (NSData *data in service.addresses) {
        const sockaddr *raw = (const sockaddr *)data.bytes;
        if (raw && raw->sa_family == AF_INET && inet_ntop(AF_INET, &((const sockaddr_in *)raw)->sin_addr, address, sizeof(address))) break;
    }
    if (address[0]) @synchronized(self) { [_resolvedHost release]; _resolvedHost = [[NSString alloc] initWithUTF8String:address]; _resolvedPort = service.port; }
    @synchronized(self) { [_services removeObject:service]; }
}
- (void)netService:(NSNetService *)service didNotResolve:(NSDictionary *)error { (void)error; @synchronized(self) { [_services removeObject:service]; } }
- (BOOL)consumeHost:(char *)host capacity:(int)capacity port:(int *)port {
    @synchronized(self) {
        if (!_resolvedHost || !host || capacity <= 0 || !port) return NO;
        std::snprintf(host, (size_t)capacity, "%s", _resolvedHost.UTF8String); *port = (int)_resolvedPort;
        [_resolvedHost release]; _resolvedHost = nil; _resolvedPort = 0; return YES;
    }
}
@end

static TH07BonjourProbe *gProbe = nil;
static TH07BonjourProbe *Probe() { @synchronized([TH07BonjourProbe class]) { if (!gProbe) gProbe = [[TH07BonjourProbe alloc] init]; return gProbe; } }

extern "C" void TH07_IOS_TriggerLocalNetworkPermission() { [Probe() performSelectorOnMainThread:@selector(start) withObject:nil waitUntilDone:NO]; }
extern "C" void TH07_IOS_StopLocalNetworkPermissionProbe() { [Probe() performSelectorOnMainThread:@selector(stop) withObject:nil waitUntilDone:NO]; }
extern "C" int TH07_IOS_GetLocalNetworkPermissionState() { return gPermissionState.load(); }
extern "C" void TH07_IOS_StartBonjourHost(int port) {
    if (port <= 0 || port > 65535) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [gHostService stop]; [gHostService release];
        gHostService = [[NSNetService alloc] initWithDomain:@"local." type:@"_th07-online._udp."
                                                       name:[[UIDevice currentDevice] name] port:port];
        [gHostService publish];
    });
}
extern "C" void TH07_IOS_StopBonjourHost() { dispatch_async(dispatch_get_main_queue(), ^{ [gHostService stop]; [gHostService release]; gHostService = nil; }); }
extern "C" int TH07_IOS_PollBonjourHost(char *host, int capacity, int *port) { return [Probe() consumeHost:host capacity:capacity port:port] ? 1 : 0; }
