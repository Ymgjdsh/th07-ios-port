#import <UIKit/UIKit.h>

#include <cstdio>
#include <mutex>
#include <string>

static std::mutex gTextMutex;
static int gTextField = 0;
static std::string gTextValue;
static bool gTextReady = false;
static bool gPromptVisible = false;

static UIWindow *TH07ActiveWindow()
{
    UIApplication *application = [UIApplication sharedApplication];
    for (UIScene *scene in application.connectedScenes)
    {
        if (![scene isKindOfClass:[UIWindowScene class]] ||
            scene.activationState == UISceneActivationStateUnattached)
            continue;
        UIWindowScene *windowScene = (UIWindowScene *)scene;
        for (UIWindow *window in windowScene.windows)
        {
            if (window.isKeyWindow) return window;
        }
    }
    for (UIWindow *window in application.windows)
    {
        if (!window.hidden && window.alpha > 0.0) return window;
    }
    return nil;
}

static UIViewController *TH07TopViewController()
{
    UIWindow *window = TH07ActiveWindow();
    if (!window) return nil;
    UIViewController *controller = window.rootViewController;
    while (controller)
    {
        if (controller.presentedViewController)
        {
            controller = controller.presentedViewController;
            continue;
        }
        if ([controller isKindOfClass:[UINavigationController class]])
        {
            controller = ((UINavigationController *)controller).visibleViewController;
            continue;
        }
        if ([controller isKindOfClass:[UITabBarController class]])
        {
            controller = ((UITabBarController *)controller).selectedViewController;
            continue;
        }
        break;
    }
    return controller;
}

extern "C" void TH07_IOS_RequestOnlineText(int field, const char *title, const char *value)
{
    if (field <= 0) return;
    NSString *promptTitle = [NSString stringWithUTF8String:title ? title : "Online setting"];
    NSString *initial = [NSString stringWithUTF8String:value ? value : ""];
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *controller = TH07TopViewController();
        if (!controller || gPromptVisible ||
            [controller isKindOfClass:[UIAlertController class]])
        {
            NSLog(@"[TH07] text prompt unavailable field=%d controller=%@ visible=%d",
                  field, controller, gPromptVisible);
            return;
        }
        gPromptVisible = true;
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:promptTitle
            message:nil preferredStyle:UIAlertControllerStyleAlert];
        [alert addTextFieldWithConfigurationHandler:^(UITextField *textField) {
            textField.text = initial;
            textField.clearButtonMode = UITextFieldViewModeWhileEditing;
            textField.autocapitalizationType = UITextAutocapitalizationTypeNone;
            textField.autocorrectionType = UITextAutocorrectionTypeNo;
            textField.spellCheckingType = UITextSpellCheckingTypeNo;
            // Direct accepts DNS names as well as IPv4 literals; keep the
            // period and alphabetic keys available in the fallback prompt.
            if (field == 1) textField.keyboardType = UIKeyboardTypeURL;
        }];
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
            style:UIAlertActionStyleCancel handler:^(UIAlertAction *action) {
                (void)action;
                gPromptVisible = false;
            }]];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault
            handler:^(UIAlertAction *action) {
                (void)action;
                const char *text = alert.textFields.firstObject.text.UTF8String;
                {
                    std::lock_guard<std::mutex> lock(gTextMutex);
                    gTextField = field; gTextValue = text ? text : ""; gTextReady = true;
                }
                gPromptVisible = false;
            }]];
        [controller presentViewController:alert animated:YES completion:^{
            NSLog(@"[TH07] text prompt presented field=%d", field);
        }];
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
