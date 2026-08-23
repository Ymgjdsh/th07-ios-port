#import <UIKit/UIKit.h>

#include "../src/OnlineLauncherIOS.hpp"
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

#ifndef TH07_IOS_VERSION
#define TH07_IOS_VERSION "dev"
#endif

#ifndef TH07_IOS_BUILD
#define TH07_IOS_BUILD "0"
#endif

namespace
{
struct LauncherAction
{
    int action = TH07_ONLINE_ACTION_NONE;
    int mode = 0;
    int delay = 5;
    std::string directAddress;
    std::string relayEndpoint;
    std::string relayRoom;
};

struct LauncherSnapshot
{
    int mode = 0;
    int state = 0;
    int hostRole = 0;
    int canStart = 0;
    int delay = 5;
    u32 rtt = 0;
    std::string status = "Offline";
    std::string peer;
    std::string directAddress = "192.168.1.2";
    std::string relayEndpoint;
    std::string relayRoom;
};

std::mutex gLauncherMutex;
std::deque<LauncherAction> gLauncherActions;
LauncherSnapshot gLauncherSnapshot;
bool gRefreshScheduled = false;

NSString *String(const std::string &value)
{
    NSString *result = [NSString stringWithUTF8String:value.c_str()];
    return result ? result : @"";
}

UIViewController *TopViewController()
{
    UIWindow *window = nil;
    if (@available(iOS 13.0, *))
    {
        for (UIScene *scene in [UIApplication sharedApplication].connectedScenes)
        {
            if (![scene isKindOfClass:[UIWindowScene class]] ||
                scene.activationState == UISceneActivationStateUnattached) continue;
            for (UIWindow *candidate in ((UIWindowScene *)scene).windows)
            {
                if (candidate.isKeyWindow) { window = candidate; break; }
            }
            if (window) break;
        }
    }
    if (!window)
    {
        for (UIWindow *candidate in [UIApplication sharedApplication].windows)
        {
            if (candidate.isKeyWindow) { window = candidate; break; }
        }
    }
    UIViewController *controller = window.rootViewController;
    while (controller.presentedViewController) controller = controller.presentedViewController;
    return controller;
}

void Enqueue(const LauncherAction &action)
{
    std::lock_guard<std::mutex> lock(gLauncherMutex);
    if (gLauncherActions.size() >= 32) gLauncherActions.pop_front();
    gLauncherActions.push_back(action);
}
} // namespace

@interface TH07AboutViewController : UITableViewController
@end

@implementation TH07AboutViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = @"关于作者";
    self.tableView.scrollEnabled = NO;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    (void)tableView;
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    (void)tableView; (void)section;
    return 3;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    (void)tableView;
    UITableViewCell *cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                                        reuseIdentifier:nil] autorelease];
    static NSString *labels[] = {@"作者", @"当前版本", @"构建日期"};
    static NSString *values[] = {@"YMGSJDH", nil, nil};
    cell.textLabel.text = labels[indexPath.row];
    if (indexPath.row == 0)
    {
        cell.detailTextLabel.text = values[0];
    }
    else if (indexPath.row == 1)
    {
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%s (Build %s)",
                                     TH07_IOS_VERSION, TH07_IOS_BUILD];
    }
    else
    {
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%s %s", __DATE__, __TIME__];
    }
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    return cell;
}

@end

@interface TH07OnlineLauncherController : UITableViewController <UIGestureRecognizerDelegate> {
@private
    UISegmentedControl *_modeControl;
    UITextField *_directField;
    UITextField *_relayEndpointField;
    UITextField *_relayRoomField;
    UILabel *_statusLabel;
    UILabel *_peerLabel;
    UILabel *_delayLabel;
    UIStepper *_delayStepper;
    LauncherSnapshot _snapshot;
    BOOL _directAddressDirty;
    BOOL _relayEndpointDirty;
    BOOL _relayRoomDirty;
}
- (void)refreshFromSnapshot;
- (LauncherAction)currentAction:(int)action;
@end

static TH07OnlineLauncherController *gLauncherController = nil;

@implementation TH07OnlineLauncherController

- (instancetype)init
{
    self = [super initWithStyle:UITableViewStyleInsetGrouped];
    if (self)
    {
        self.title = @"TH07 Online";
        self.navigationItem.rightBarButtonItem = [[[UIBarButtonItem alloc]
            initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self
            action:@selector(closePressed)] autorelease];
    }
    return self;
}

- (void)dealloc
{
    [_modeControl release];
    [_directField release];
    [_relayEndpointField release];
    [_relayRoomField release];
    [_statusLabel release];
    [_peerLabel release];
    [_delayLabel release];
    [_delayStepper release];
    [super dealloc];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.tableView.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive;
    _modeControl = [[UISegmentedControl alloc] initWithItems:@[@"LAN", @"Direct", @"Relay", @"Nearby"]];
    [_modeControl addTarget:self action:@selector(modeChanged:) forControlEvents:UIControlEventValueChanged];
    _directField = [[UITextField alloc] initWithFrame:CGRectZero];
    _relayEndpointField = [[UITextField alloc] initWithFrame:CGRectZero];
    _relayRoomField = [[UITextField alloc] initWithFrame:CGRectZero];
    _directField.placeholder = @"192.168.1.2";
    // Direct accepts both IPv4 literals and DNS host names.  The URL layout
    // keeps the period key handy while still exposing alphabetic characters.
    _directField.keyboardType = UIKeyboardTypeURL;
    _relayEndpointField.placeholder = @"server-ip:3478";
    _relayEndpointField.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
    _relayRoomField.placeholder = @"Room code";
    for (UITextField *field in @[_directField, _relayEndpointField, _relayRoomField])
    {
        field.autocorrectionType = UITextAutocorrectionTypeNo;
        field.autocapitalizationType = UITextAutocapitalizationTypeNone;
        field.clearButtonMode = UITextFieldViewModeWhileEditing;
        field.textAlignment = NSTextAlignmentRight;
        [field addTarget:self action:@selector(fieldChanged:) forControlEvents:UIControlEventEditingChanged];

        UIToolbar *toolbar = [[[UIToolbar alloc] init] autorelease];
        [toolbar sizeToFit];
        UIBarButtonItem *spacer = [[[UIBarButtonItem alloc]
            initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil] autorelease];
        UIBarButtonItem *done = [[[UIBarButtonItem alloc]
            initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self
            action:@selector(doneEditingPressed)] autorelease];
        toolbar.items = @[spacer, done];
        field.inputAccessoryView = toolbar;
    }
    UITapGestureRecognizer *dismissTap = [[[UITapGestureRecognizer alloc]
        initWithTarget:self action:@selector(dismissKeyboard:)] autorelease];
    dismissTap.cancelsTouchesInView = NO;
    dismissTap.delegate = self;
    [self.tableView addGestureRecognizer:dismissTap];
    _statusLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _statusLabel.numberOfLines = 0;
    _statusLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    _peerLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _peerLabel.numberOfLines = 0;
    _peerLabel.textColor = [UIColor secondaryLabelColor];
    _peerLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    _delayLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _delayStepper = [[UIStepper alloc] initWithFrame:CGRectZero];
    _delayStepper.minimumValue = 0;
    _delayStepper.maximumValue = 8;
    _delayStepper.stepValue = 1;
    [_delayStepper addTarget:self action:@selector(delayChanged:) forControlEvents:UIControlEventValueChanged];
    UIView *footer = [[[UIView alloc] initWithFrame:CGRectMake(0, 0,
                                                                  self.tableView.bounds.size.width, 44.0)] autorelease];
    UIButton *aboutButton = [UIButton buttonWithType:UIButtonTypeSystem];
    aboutButton.frame = footer.bounds;
    aboutButton.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    aboutButton.contentHorizontalAlignment = UIControlContentHorizontalAlignmentRight;
    aboutButton.contentEdgeInsets = UIEdgeInsetsMake(0.0, 16.0, 0.0, 16.0);
    aboutButton.titleLabel.font = [UIFont systemFontOfSize:13.0];
    [aboutButton setTitle:@"关于" forState:UIControlStateNormal];
    [aboutButton addTarget:self action:@selector(aboutPressed) forControlEvents:UIControlEventTouchUpInside];
    [footer addSubview:aboutButton];
    self.tableView.tableFooterView = footer;
    [self refreshFromSnapshot];
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    UIView *footer = self.tableView.tableFooterView;
    if (!footer) return;
    CGRect frame = footer.frame;
    frame.size.width = self.tableView.bounds.size.width;
    footer.frame = frame;
}

- (void)viewDidDisappear:(BOOL)animated
{
    [super viewDidDisappear:animated];
    // Pushing the About screen also hides this controller; only close the
    // session when the modal navigation container itself is being dismissed.
    UINavigationController *navigation = self.navigationController;
    BOOL leavingModal = navigation && (self.isBeingDismissed ||
                                       navigation.isBeingDismissed ||
                                       navigation.presentingViewController == nil);
    if (gLauncherController == self && navigation.topViewController == self && leavingModal)
    {
        Enqueue([self currentAction:TH07_ONLINE_ACTION_CLOSE]);
        gLauncherController = nil;
    }
}

- (void)refreshFromSnapshot
{
    {
        std::lock_guard<std::mutex> lock(gLauncherMutex);
        _snapshot = gLauncherSnapshot;
    }
    if (!self.isViewLoaded) return;
    _modeControl.selectedSegmentIndex = _snapshot.mode;
    if (!_directField.isFirstResponder)
    {
        const char *draft = _directField.text.UTF8String;
        if (_directAddressDirty && draft && _snapshot.directAddress == draft) _directAddressDirty = NO;
        if (!_directAddressDirty) _directField.text = String(_snapshot.directAddress);
    }
    if (!_relayEndpointField.isFirstResponder)
    {
        const char *draft = _relayEndpointField.text.UTF8String;
        if (_relayEndpointDirty && draft && _snapshot.relayEndpoint == draft) _relayEndpointDirty = NO;
        if (!_relayEndpointDirty) _relayEndpointField.text = String(_snapshot.relayEndpoint);
    }
    if (!_relayRoomField.isFirstResponder)
    {
        const char *draft = _relayRoomField.text.UTF8String;
        if (_relayRoomDirty && draft && _snapshot.relayRoom == draft) _relayRoomDirty = NO;
        if (!_relayRoomDirty) _relayRoomField.text = String(_snapshot.relayRoom);
    }
    _statusLabel.text = String(_snapshot.status);
    NSString *peer = _snapshot.peer.empty() ? @"No peer connected" :
        [NSString stringWithFormat:@"Peer: %@   RTT: %u ms", String(_snapshot.peer), _snapshot.rtt];
    _peerLabel.text = peer;
    _delayStepper.value = _snapshot.delay;
    _delayLabel.text = [NSString stringWithFormat:@"Input delay: %d frames", _snapshot.delay];
    [self.tableView reloadData];
}

- (LauncherAction)currentAction:(int)action
{
    LauncherAction value;
    value.action = action;
    value.mode = (int)_modeControl.selectedSegmentIndex;
    value.delay = (int)_delayStepper.value;
    const char *direct = _directField.text.UTF8String;
    const char *endpoint = _relayEndpointField.text.UTF8String;
    const char *room = _relayRoomField.text.UTF8String;
    value.directAddress = direct ? direct : "";
    value.relayEndpoint = endpoint ? endpoint : "";
    value.relayRoom = room ? room : "";
    return value;
}

- (void)closePressed
{
    Enqueue([self currentAction:TH07_ONLINE_ACTION_CLOSE]);
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)aboutPressed
{
    TH07AboutViewController *about = [[TH07AboutViewController alloc] initWithStyle:UITableViewStyleInsetGrouped];
    [self.navigationController pushViewController:about animated:YES];
    [about release];
}

- (void)fieldChanged:(UITextField *)field
{
    if (field == _directField) _directAddressDirty = YES;
    else if (field == _relayEndpointField) _relayEndpointDirty = YES;
    else if (field == _relayRoomField) _relayRoomDirty = YES;
}

- (void)doneEditingPressed
{
    [self.view endEditing:YES];
}

- (void)dismissKeyboard:(UITapGestureRecognizer *)gesture
{
    if (gesture.state == UIGestureRecognizerStateEnded) [self.view endEditing:YES];
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
       shouldReceiveTouch:(UITouch *)touch
{
    (void)gestureRecognizer;
    UIView *view = touch.view;
    if ([view isDescendantOfView:_directField] || [view isDescendantOfView:_relayEndpointField] ||
        [view isDescendantOfView:_relayRoomField] || [view isKindOfClass:[UIButton class]] ||
        [view isKindOfClass:[UIControl class]]) return NO;
    return YES;
}

- (void)modeChanged:(UISegmentedControl *)sender
{
    (void)sender;
    LauncherAction action = [self currentAction:TH07_ONLINE_ACTION_SET_MODE];
    [self.view endEditing:YES];
    Enqueue(action);
    [self.tableView reloadData];
}

- (void)delayChanged:(UIStepper *)sender
{
    (void)sender;
    _delayLabel.text = [NSString stringWithFormat:@"Input delay: %d frames", (int)_delayStepper.value];
    Enqueue([self currentAction:TH07_ONLINE_ACTION_SET_DELAY]);
}

- (void)commandPressed:(UIButton *)sender
{
    LauncherAction action = [self currentAction:(int)sender.tag];
    [self.view endEditing:YES];
    Enqueue(action);
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    (void)tableView;
    return 5;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    (void)tableView;
    if (section == 0) return 1;
    if (section == 1) return _modeControl.selectedSegmentIndex == 1 ? 1 :
                              (_modeControl.selectedSegmentIndex == 2 ? 2 : 0);
    if (section == 2) return 2;
    if (section == 3) return 3;
    return 3;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    (void)tableView;
    static NSString *titles[] = {@"Connection", @"Address", @"Session", @"Status", @"Game"};
    return titles[section];
}

- (UITableViewCell *)fieldCell:(UITextField *)field title:(NSString *)title
{
    UITableViewCell *cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                        reuseIdentifier:nil] autorelease];
    cell.textLabel.text = title;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    [cell.contentView addSubview:field];
    [NSLayoutConstraint activateConstraints:@[
        [field.leadingAnchor constraintGreaterThanOrEqualToAnchor:cell.textLabel.trailingAnchor constant:12.0],
        [field.trailingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.trailingAnchor],
        [field.centerYAnchor constraintEqualToAnchor:cell.contentView.centerYAnchor],
        [field.widthAnchor constraintGreaterThanOrEqualToConstant:150.0]
    ]];
    return cell;
}

- (UITableViewCell *)buttonCell:(NSString *)title action:(int)action destructive:(BOOL)destructive
{
    UITableViewCell *cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                        reuseIdentifier:nil] autorelease];
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.tag = action;
    [button setTitle:title forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    if (destructive) [button setTitleColor:[UIColor systemRedColor] forState:UIControlStateNormal];
    [button addTarget:self action:@selector(commandPressed:) forControlEvents:UIControlEventTouchUpInside];
    [cell.contentView addSubview:button];
    [NSLayoutConstraint activateConstraints:@[
        [button.leadingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.leadingAnchor],
        [button.trailingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.trailingAnchor],
        [button.topAnchor constraintEqualToAnchor:cell.contentView.topAnchor constant:6.0],
        [button.bottomAnchor constraintEqualToAnchor:cell.contentView.bottomAnchor constant:-6.0],
        [button.heightAnchor constraintGreaterThanOrEqualToConstant:32.0]
    ]];
    return cell;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    (void)tableView;
    if (indexPath.section == 0)
    {
        UITableViewCell *cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil] autorelease];
        _modeControl.translatesAutoresizingMaskIntoConstraints = NO;
        [cell.contentView addSubview:_modeControl];
        [NSLayoutConstraint activateConstraints:@[
            [_modeControl.leadingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.leadingAnchor],
            [_modeControl.trailingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.trailingAnchor],
            [_modeControl.topAnchor constraintEqualToAnchor:cell.contentView.topAnchor constant:8.0],
            [_modeControl.bottomAnchor constraintEqualToAnchor:cell.contentView.bottomAnchor constant:-8.0]
        ]];
        return cell;
    }
    if (indexPath.section == 1)
    {
        if (_modeControl.selectedSegmentIndex == 1) return [self fieldCell:_directField title:@"Host address or domain"];
        return indexPath.row == 0 ? [self fieldCell:_relayEndpointField title:@"Relay"] :
                                    [self fieldCell:_relayRoomField title:@"Room"];
    }
    if (indexPath.section == 2)
        return [self buttonCell:indexPath.row == 0 ? @"Host Session" : @"Join Session"
                           action:indexPath.row == 0 ? TH07_ONLINE_ACTION_HOST : TH07_ONLINE_ACTION_GUEST
                      destructive:NO];
    if (indexPath.section == 3)
    {
        UITableViewCell *cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil] autorelease];
        UIView *content = indexPath.row == 0 ? (UIView *)_statusLabel :
                          (indexPath.row == 1 ? (UIView *)_peerLabel : (UIView *)_delayLabel);
        content.translatesAutoresizingMaskIntoConstraints = NO;
        [cell.contentView addSubview:content];
        if (indexPath.row == 2)
        {
            _delayStepper.translatesAutoresizingMaskIntoConstraints = NO;
            [cell.contentView addSubview:_delayStepper];
            [NSLayoutConstraint activateConstraints:@[
                [content.leadingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.leadingAnchor],
                [content.centerYAnchor constraintEqualToAnchor:cell.contentView.centerYAnchor],
                [_delayStepper.trailingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.trailingAnchor],
                [_delayStepper.centerYAnchor constraintEqualToAnchor:cell.contentView.centerYAnchor],
                [_delayStepper.leadingAnchor constraintGreaterThanOrEqualToAnchor:content.trailingAnchor constant:8.0],
                [cell.contentView.heightAnchor constraintGreaterThanOrEqualToConstant:50.0]
            ]];
        }
        else
        {
            [NSLayoutConstraint activateConstraints:@[
                [content.leadingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.leadingAnchor],
                [content.trailingAnchor constraintEqualToAnchor:cell.contentView.layoutMarginsGuide.trailingAnchor],
                [content.topAnchor constraintEqualToAnchor:cell.contentView.topAnchor constant:8.0],
                [content.bottomAnchor constraintEqualToAnchor:cell.contentView.bottomAnchor constant:-8.0]
            ]];
        }
        return cell;
    }
    if (indexPath.row == 0)
    {
        UITableViewCell *cell = [self buttonCell:@"Start Online Game" action:TH07_ONLINE_ACTION_START_GAME destructive:NO];
        cell.userInteractionEnabled = _snapshot.canStart != 0;
        cell.contentView.alpha = _snapshot.canStart ? 1.0 : 0.4;
        return cell;
    }
    if (indexPath.row == 1)
        return [self buttonCell:@"Start Local Two-Player" action:TH07_ONLINE_ACTION_START_LOCAL destructive:NO];
    return [self buttonCell:@"Leave Session" action:TH07_ONLINE_ACTION_LEAVE destructive:YES];
}
@end

extern "C" void TH07_IOS_PresentOnlineLauncher()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (gLauncherController || !TopViewController()) return;
        TH07OnlineLauncherController *launcher = [[TH07OnlineLauncherController alloc] init];
        UINavigationController *navigation = [[UINavigationController alloc] initWithRootViewController:launcher];
        navigation.modalPresentationStyle = UIModalPresentationPageSheet;
        gLauncherController = launcher;
        [TopViewController() presentViewController:navigation animated:YES completion:nil];
        [navigation release];
        [launcher release];
    });
}

extern "C" void TH07_IOS_DismissOnlineLauncher()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        TH07OnlineLauncherController *launcher = gLauncherController;
        if (!launcher) return;
        [launcher dismissViewControllerAnimated:YES completion:nil];
        gLauncherController = nil;
    });
}

extern "C" void TH07_IOS_ShowOnlineError(const char *message)
{
    std::string copied = message ? message : "Online session failed";
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *controller = TopViewController();
        if (!controller || [controller isKindOfClass:[UIAlertController class]]) return;
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Online Error"
            message:String(copied) preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [controller presentViewController:alert animated:YES completion:nil];
    });
}

extern "C" int TH07_IOS_IsOnlineLauncherVisible()
{
    return gLauncherController != nil;
}

extern "C" int TH07_IOS_PollOnlineLauncherAction(int *action, int *mode, int *delay,
                                                   char *directAddress, int directCapacity,
                                                   char *relayEndpoint, int endpointCapacity,
                                                   char *relayRoom, int roomCapacity)
{
    if (!action || !mode || !delay) return 0;
    std::lock_guard<std::mutex> lock(gLauncherMutex);
    if (gLauncherActions.empty()) return 0;
    LauncherAction value = std::move(gLauncherActions.front());
    gLauncherActions.pop_front();
    *action = value.action;
    *mode = value.mode;
    *delay = value.delay;
    if (directAddress && directCapacity > 0)
        std::snprintf(directAddress, (size_t)directCapacity, "%s", value.directAddress.c_str());
    if (relayEndpoint && endpointCapacity > 0)
        std::snprintf(relayEndpoint, (size_t)endpointCapacity, "%s", value.relayEndpoint.c_str());
    if (relayRoom && roomCapacity > 0)
        std::snprintf(relayRoom, (size_t)roomCapacity, "%s", value.relayRoom.c_str());
    return 1;
}

extern "C" void TH07_IOS_UpdateOnlineLauncher(int mode, int state, int hostRole,
                                                 int canStart, int delay, u32 rtt,
                                                const char *status, const char *peer,
                                                const char *directAddress,
                                                const char *relayEndpoint,
                                                const char *relayRoom)
{
    {
        std::lock_guard<std::mutex> lock(gLauncherMutex);
        const std::string nextStatus = status ? status : "Offline";
        const std::string nextPeer = peer ? peer : "";
        const std::string nextDirectAddress = directAddress ? directAddress : "";
        const std::string nextRelayEndpoint = relayEndpoint ? relayEndpoint : "";
        const std::string nextRelayRoom = relayRoom ? relayRoom : "";
        if (gLauncherSnapshot.mode == mode && gLauncherSnapshot.state == state &&
            gLauncherSnapshot.hostRole == hostRole && gLauncherSnapshot.canStart == canStart &&
            gLauncherSnapshot.delay == delay && gLauncherSnapshot.rtt == rtt &&
            gLauncherSnapshot.status == nextStatus && gLauncherSnapshot.peer == nextPeer &&
            gLauncherSnapshot.directAddress == nextDirectAddress &&
            gLauncherSnapshot.relayEndpoint == nextRelayEndpoint &&
            gLauncherSnapshot.relayRoom == nextRelayRoom) return;
        gLauncherSnapshot.mode = mode;
        gLauncherSnapshot.state = state;
        gLauncherSnapshot.hostRole = hostRole;
        gLauncherSnapshot.canStart = canStart;
        gLauncherSnapshot.delay = delay;
        gLauncherSnapshot.rtt = rtt;
        gLauncherSnapshot.status = nextStatus;
        gLauncherSnapshot.peer = nextPeer;
        gLauncherSnapshot.directAddress = nextDirectAddress;
        gLauncherSnapshot.relayEndpoint = nextRelayEndpoint;
        gLauncherSnapshot.relayRoom = nextRelayRoom;
        if (gRefreshScheduled) return;
        gRefreshScheduled = true;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        {
            std::lock_guard<std::mutex> lock(gLauncherMutex);
            gRefreshScheduled = false;
        }
        [gLauncherController refreshFromSnapshot];
    });
}
