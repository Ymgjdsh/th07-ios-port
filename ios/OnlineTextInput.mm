#import <UIKit/UIKit.h>

#include <cstdio>
#include <mutex>
#include <string>

static std::mutex gTextMutex;
static int gTextField = 0;
static std::string gTextValue;
static bool gTextReady = false;

static UIViewController *TH07TopViewController()
{
    UIWindow *window = [UIApplication sharedApplication].keyWindow;
    UIViewController *controller = window.rootViewController;
    while (controller.presentedViewController) controller = controller.presentedViewController;
    return controller;
}

extern "C" void TH07_IOS_RequestOnlineText(int field, const char *title, const char *value)
{
    if (field <= 0) return;
    NSString *promptTitle = [NSString stringWithUTF8String:title ? title : "Online setting"];
    NSString *initial = [NSString stringWithUTF8String:value ? value : ""];
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *controller = TH07TopViewController();
        if (!controller || [controller isKindOfClass:[UIAlertController class]]) return;
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:promptTitle
            message:nil preferredStyle:UIAlertControllerStyleAlert];
        [alert addTextFieldWithConfigurationHandler:^(UITextField *textField) {
            textField.text = initial;
            textField.clearButtonMode = UITextFieldViewModeWhileEditing;
            // Direct accepts DNS names as well as IPv4 literals; keep the
            // period and alphabetic keys available in the fallback prompt.
            if (field == 1) textField.keyboardType = UIKeyboardTypeURL;
        }];
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault
            handler:^(UIAlertAction *action) {
                (void)action;
                const char *text = alert.textFields.firstObject.text.UTF8String;
                std::lock_guard<std::mutex> lock(gTextMutex);
                gTextField = field; gTextValue = text ? text : ""; gTextReady = true;
            }]];
        [controller presentViewController:alert animated:YES completion:nil];
    });
}

extern "C" int TH07_IOS_PollOnlineText(int *field, char *value, int capacity)
{
    if (!field || !value || capacity <= 0) return 0;
    std::lock_guard<std::mutex> lock(gTextMutex);
    if (!gTextReady) return 0;
    *field = gTextField;
    std::snprintf(value, (size_t)capacity, "%s", gTextValue.c_str());
    gTextReady = false; return 1;
}
