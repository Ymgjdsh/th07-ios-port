#!/usr/bin/env python3
import argparse
import hashlib
import os
import sys


def digest(path):
    value = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("--root", required=True)
args = parser.parse_args()
root = os.path.abspath(args.root)

required = {
    "CMakeLists.txt": 1,
    "ios/build_ios.sh": 1,
    "ios/package_ipa.sh": 1,
    "ios/package_source.py": 1,
    "ios/setup_mac_ssh.ps1": 1,
    "ios/build_on_mac.ps1": 1,
    "ios/mac_build.local.psd1.example": 1,
    "tools/publish_github.ps1": 1,
    "ios/OnlineLauncher.mm": 1,
    "ios/test_online_protocol.cpp": 1,
    "assets/th07.dat": 23_829_135,
    "assets/thbgm.dat": 444_516_656,
    "assets/msgothic.ttc": 1_000_000,
    "vendored/SDL/CMakeLists.txt": 1,
    "vendored/SDL_image/CMakeLists.txt": 1,
    "vendored/SDL_ttf/CMakeLists.txt": 1,
    "vendored/SDL_ttf/external/freetype/CMakeLists.txt": 1,
}

failed = False
for relative, expected in required.items():
    path = os.path.join(root, relative)
    if not os.path.isfile(path):
        print("error: missing", relative)
        failed = True
        continue
    size = os.path.getsize(path)
    if expected > 1 and relative != "assets/msgothic.ttc" and size != expected:
        print("error: unexpected size", relative, size, "expected", expected)
        failed = True
    elif relative == "assets/msgothic.ttc" and size < expected:
        print("error: font looks incomplete", relative, size)
        failed = True
    else:
        print("ok:", relative, size)

if failed:
    sys.exit(2)

cmake_path = os.path.join(root, "CMakeLists.txt")
with open(cmake_path, "r", encoding="utf-8") as stream:
    cmake_source = stream.read()

for marker in (
    'set(TH07_IOS_BUILD "34"',
    'XCODE_ATTRIBUTE_LLVM_LTO "YES_THIN"',
    'set(SDL_GPU OFF CACHE BOOL "" FORCE)',
    'set(SDL_RENDER ON CACHE BOOL "" FORCE)',
    'set(SDL_METAL OFF CACHE BOOL "" FORCE)',
    'set(SDL_VULKAN OFF CACHE BOOL "" FORCE)',
    'set(SDL_OPENGLES ON CACHE BOOL "" FORCE)',
):
    if marker not in cmake_source:
        print("error: iOS build configuration marker missing:", marker)
        failed = True

if failed:
    sys.exit(2)

print("ok: iOS OpenGL ES configuration (SDL GPU/Metal/Vulkan disabled)")

with open(os.path.join(root, "ios", "package_source.py"), "r", encoding="utf-8") as stream:
    package_source = stream.read()
with open(os.path.join(root, "ios", "build_on_mac.ps1"), "r", encoding="utf-8") as stream:
    remote_build_source = stream.read()
with open(os.path.join(root, "ios", "setup_mac_ssh.ps1"), "r", encoding="utf-8") as stream:
    ssh_setup_source = stream.read()
with open(os.path.join(root, "tools", "publish_github.ps1"), "r", encoding="utf-8") as stream:
    github_publish_source = stream.read()
with open(os.path.join(root, "ios", "build_ios.sh"), "r", encoding="utf-8") as stream:
    ios_build_source = stream.read()
for marker in (
    'parser.add_argument("--exclude-assets"',
    'excluded_roots = {".git", ".agents", "__pycache__", "dist"}',
):
    if marker not in package_source:
        print("error: remote source packaging marker missing:", marker)
        failed = True
for marker in (
    'rm -rf "`$BASE/th07-ios14-port"',
    "remoteHashOutput | Out-String",
    "First run: configuring passwordless Mac login",
):
    if marker not in remote_build_source:
        print("error: remote Mac build marker missing:", marker)
        failed = True
if "-N '\"\"'" not in ssh_setup_source:
    print("error: Windows PowerShell 5.1 ssh-keygen compatibility marker missing")
    failed = True
for marker in (
    "Potential secret material detected",
    "Refusing to publish forbidden",
    "Refusing to publish files larger than 50 MB",
    "users.noreply.github.com",
    "git push --atomic",
    "Source-only upload list",
):
    if marker not in github_publish_source:
        print("error: safe GitHub publishing marker missing:", marker)
        failed = True
for marker in ("CMake.app/Contents/bin", "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"):
    if marker not in ios_build_source:
        print("error: iOS build script marker missing:", marker)
        failed = True
if failed:
    sys.exit(2)
print("ok: incremental SSH Mac build automation")

with open(os.path.join(root, "src", "ResultScreen.hpp"), "r", encoding="utf-8") as stream:
    score_header = stream.read()
with open(os.path.join(root, "src", "ResultScreen.cpp"), "r", encoding="utf-8") as stream:
    score_source = stream.read()

for marker in (
    "ScoreDat() : raw{}, scores(NULL), decodedData(NULL)",
    "GetScoreChunkRegion",
    "GetNextScoreChunk",
    'score ready decoded=%d fileLength=%d',
):
    if marker not in score_header and marker not in score_source:
        print("error: safe score handling marker missing:", marker)
        failed = True

if failed:
    sys.exit(2)

print("ok: safe empty/corrupt score.dat handling")

mobile_markers = {
    os.path.join("src", "MobileUi.cpp"): (
        "CONTROL_JOYSTICK",
        "DRAG SENS",
        "dragSensitivity",
        "UiVertex",
        "kUiVboCapacity",
        "PERFECT CHERRY DEVELOPER",
        "SetMainMenuHome",
        "IsPortraitGameplayLayout",
        "GetPortraitLayout",
        "Z TOGGLE",
        "S TOGGLE",
        "AUTO BOMB",
        "EDIT LAYOUT",
        "shootToggleMode",
        "focusToggleMode",
        "autoBomb",
        "layoutCustomized",
        "ClearPhysicalTouches",
        "g_DialogueInputActive",
        'MobileDiagnostics::Log("mobile/dialogue"',
        "closedPanel=%d releasedLatch=%d",
        "sizeof(UiVertex)",
        "ConsumeOverlayTouch",
        "UsesOpenGlOnlinePanel",
        "#if !defined(TH07_IOS)",
    ),
    os.path.join("src", "MainMenu.cpp"): (
        "HandleMobileTouch",
        "ConsumeMainMenuTouch",
        "MENU_TOUCH_SWIPE_HORIZONTAL",
        "onlinePos",
    ),
    os.path.join("src", "graphics", "Gles.cpp"): (
        'uniform vec4 u_SrcRect',
        "redundantStateCalls",
        "drawRegion(416, 16, 224, 224",
        "drawRegion(32, 16, 384, 448",
        "layout.gameWidth, layout.gameHeight",
    ),
    os.path.join("src", "GameWindow.cpp"): (
        '"perf/frame"',
        "HIGH_PIXEL_DENSITY_BOOLEAN, false",
        "RecordPerformance",
        "IsPerformanceTelemetryEnabled",
    ),
    os.path.join("src", "Touch.cpp"): (
        "GetDragSensitivity",
        "GetPortraitLayout",
        "RecordAppliedPlayerDelta",
        "InjectReplayPlayerDelta",
        "g_DialogueAdvancePending",
        "g_DialogueReleaseSent",
        "DIALOGUE_SKIP_HOLD_MS",
        "Dialogue input owns the full screen",
        "ApplyDialogueButtonPolicy",
        "forced Z release frame",
    ),
    os.path.join("src", "AsciiManager.cpp"): (
        "PauseMenu::HandleMobileTouch",
        "RetryMenu::HandleMobileTouch",
        "MobileHitMenuVms",
    ),
    os.path.join("src", "Supervisor.cpp"): (
        "SDL_InitSubSystem(SDL_INIT_GAMEPAD)",
        'MobileDiagnostics::Log("input/gamepad"',
    ),
    os.path.join("src", "SoundPlayer.cpp"): (
        "ma_ios_session_category_playback",
        "sessionCategoryOptions = 0",
        "retrying default context",
        "AudioDeviceNotification",
        "RequestDeviceRecovery",
        'MobileDiagnostics::Log("audio/session"',
    ),
    os.path.join("src", "GameManager.cpp"): (
        "HasUnlockedExtra",
        "noContinueClearCount > 0",
        "difficulty <= DIFF_LUNATIC",
    ),
    os.path.join("src", "Ending.cpp"): (
        'MobileDiagnostics::Log("progress/extra"',
        "g_GameManager.HasUnlockedExtra()",
    ),
    os.path.join("src", "Player.cpp"): (
        "TryActivateBomb(true)",
        "!g_GameManager.replay",
        "g_CurFrameRawInput |= TH_BUTTON_BOMB",
        'MobileDiagnostics::Log("gameplay/auto-bomb"',
        "Touch::RecordAppliedPlayerDelta",
    ),
    os.path.join("src", "ReplayManager.cpp"): (
        "TOUCH_REPLAY_MARKER_FRAME",
        "StoreReplayFloat",
        "Touch::ConsumeRecordedPlayerDelta",
        "Touch::InjectReplayPlayerDelta",
        "TOUCH_REPLAY_DATA_MAGIC",
        "touchWasUsed",
        "replayPayloadSize = sizeof(ReplayData)",
        'MobileDiagnostics::Log("replay/touch"',
    ),
    os.path.join("src", "ResultScreen.cpp"): (
        "touch run eligible for replay save",
    ),
    os.path.join("src", "Controller.cpp"): (
        "Start must always pause on mobile controllers",
        "shotSlow option turns a held controller Shoot button into Focus",
        "g_AutoFocusTimer = 0",
        "ApplyDialogueButtonPolicy",
        "TH_BUTTON_MENU",
    ),
    os.path.join("src", "main.cpp"): (
        "OpenFirstAvailableGamepad",
        'OpenFirstAvailableGamepad("active-removed")',
    ),
}

for relative, markers in mobile_markers.items():
    with open(os.path.join(root, relative), "r", encoding="utf-8") as stream:
        source = stream.read()
    for marker in markers:
        if marker not in source:
            print("error: mobile feature marker missing:", relative, marker)
            failed = True

if failed:
    sys.exit(2)

print("ok: build 32 controller focus, dialogue input and touch replay markers")

online_markers = {
    os.path.join("src", "Online.cpp"): (
        "kWelcome",
        "kAck",
        "kIncompatible",
        "SendDiscovery",
        "TH07_IOS_TriggerLocalNetworkPermission",
        "TH07_IOS_PollBonjourHost",
        "THR1 REGISTER",
        "StartBluetoothHost",
        "RequestStartGame",
        "kPrepare",
        "kMenuReady",
        "kMenuCommit",
        "kGameReady",
        "kGameCommit",
        "kMenuInput",
        "kPeerBackground",
        "SynchronizeMenuInputs",
        "g_MenuCommandContext",
        "OnlineControlContextMatches",
        "drop inactive local input",
        "NotifyAppBackgrounded",
        "ackFrame",
        "ackMask",
        "OnlineFrameHistory",
        "OnlineCanonicalTouchDelta",
        "OnlineSeedDelayedAckPrefix",
        "kStateMismatchConfirmations",
        "EnterStateDivergence",
        "g_StateDiverged",
        "Online synchronization timed out",
        "g_StartupPhase != STARTUP_MENU_COMMITTED",
        "g_MenuReadyState != g_PeerMenuState",
        "CommitGameplayBarrier",
        "CommitMenuBarrier(g_BarrierEpoch + 1)",
        "packet.barrierEpoch != g_BarrierEpoch",
        "packet.barrierEpoch == g_BarrierEpoch + 1",
        "kInputStallTimeoutMs",
        "g_RemoteInputs[frame % kInputHistorySize] = zero",
        "g_InputSynchronizationActive = true",
        "StoreLocalAcknowledgement",
        "SDL_strlcpy(g_DirectAddress, address",
        "HandleIncompatiblePeer",
        "remoteBuild=%08x",
        "partially initialized",
        "UpdateShellCloseComplete",
        "g_RemoteAuthoritativeShellRevision",
        "SHARED_CONFIRM_TITLE",
        "g_ShellConfirmAction",
        "kInputSendBudgetPerTick",
        "kLanInputDelayFloor",
        "OnlineIsImmediatelyPreviousEpoch",
        "resent menu commit epoch",
        "g_LastRemoteFrame == 0xffffffffu",
        "Lockstep input owns ordinary movement",
        "g_LastAuthoritativeFrame > g_InputFrame",
        "g_AsciiManager.pauseMenu.curState = 0",
    ),
    os.path.join("src", "MainMenu.cpp"): (
        "MoveCursorVertical(9)",
        "opened from native title item",
        "original idle timer started a demo replay",
        "NotifyMenuReady",
        "NotifyGameReady",
        "ConsumeGameplayCommit",
        "QueueInputPulse",
    ),
    os.path.join("src", "Supervisor.cpp"): (
        "The public menu is one state machine",
        "IsSelectingPlayer2Loadout",
        "if (!Online::IsNetworkSession()) Online::ResetInputSynchronization();",
    ),
    os.path.join("src", "Touch.cpp"): (
        "ObserveTouchScene",
        "IsGameplayPauseGestureAllowed",
        "ClearTouchStateForSceneChange",
    ),
    os.path.join("src", "MobileUi.cpp"): (
        "g_PulseContextSet",
        "IsValidBattleOverlayScene",
        "QueueButtonPulse",
    ),
    os.path.join("ios", "BluetoothPeerTransport.mm"): (
        "MultipeerConnectivity",
        'kServiceType = @"th07-peer"',
        '@"protocol": @"15"',
    ),
    os.path.join("ios", "OnlineLauncher.mm"): (
        "UITableViewStyleInsetGrouped",
        "UIModalPresentationPageSheet",
        "UISegmentedControl",
        "TH07AboutViewController",
        "TH07_IOS_VERSION",
        "TH07_IOS_BUILD",
        "YMGSJDH",
        "aboutPressed",
        "TH07_IOS_PollOnlineLauncherAction",
        "TH07_IOS_UpdateOnlineLauncher",
        "Start Online Game",
        "fieldChanged:",
        "doneEditingPressed",
        "dismissKeyboard:",
        "_directAddressDirty",
        "inputAccessoryView",
    ),
    os.path.join("ios", "OnlineTextInput.mm"): (
        "UIKeyboardTypeURL",
        "TH07_IOS_RequestOnlineText",
    ),
    os.path.join("ios", "LocalNetworkPermission.mm"): (
        '_th07-online._udp.',
        "TH07_IOS_StartBonjourHost",
    ),
}
for relative, markers in online_markers.items():
    with open(os.path.join(root, relative), "r", encoding="utf-8") as stream:
        source = stream.read()
    for marker in markers:
        if marker not in source:
            print("error: build 34 Online marker missing:", relative, marker)
            failed = True
if "demoFramesCount++" in open(os.path.join(root, "src", "MainMenu.cpp"), encoding="utf-8").read():
    print("error: title idle demo trigger is still enabled")
    failed = True
if failed:
    sys.exit(2)
print("ok: Build 34 canonical input, ACK recovery and gameplay lockstep markers")

with open(os.path.join(root, "src", "ResultScreen.cpp"), "r", encoding="utf-8") as stream:
    result_source = stream.read()
if "g_GameManager.globals->numRetries != 0 ||" in result_source or \
        "Touch::WasUsedThisRun()) // it probably" in result_source:
    print("error: touch runs are still excluded from replay saving")
    sys.exit(2)
print("ok: no-continue touch runs are eligible for replay saving")

with open(os.path.join(root, "src", "SoundPlayer.cpp"), "r", encoding="utf-8") as stream:
    sound_source = stream.read()
for forbidden in ("ma_ios_session_category_option_allow_bluetooth_a2dp",
                  "AVAudioSessionCategoryOptionAllowBluetoothA2DP",
                  "AVAudioSessionCategoryOptionAllowAirPlay"):
    if forbidden in sound_source:
        print("error: invalid Playback session route option remains:", forbidden)
        failed = True
if failed:
    sys.exit(2)
print("ok: iOS Playback session uses automatic speaker/headset/Bluetooth routing")

with open(os.path.join(root, "src", "MobileUi.cpp"), "r", encoding="utf-8") as stream:
    mobile_ui_source = stream.read()

if "g_Config.bombToggleMode" in mobile_ui_source or "g_BombLatched" in mobile_ui_source:
    print("error: obsolete X/Bomb toggle behavior remains")
    sys.exit(2)
for marker in ("migrated v4 X toggle to v6 S toggle (off); auto bomb off",
               "ACTION_FOCUS && g_Config.focusToggleMode",
               "TH_BUTTON_FOCUS",
               "migrated v5 config to v6; auto bomb off"):
    if marker not in mobile_ui_source:
        print("error: S/focus toggle marker missing:", marker)
        sys.exit(2)
print("ok: S toggle controls focus while X remains a momentary bomb button")

interaction_markers = {
    os.path.join("src", "MainMenu.cpp"): (
        "MobileNearestVerticalItem",
        "a tap confirms the character currently",
        "g_GameManager.character == CHAR_SAKUYA",
    ),
    os.path.join("src", "MobileUi.cpp"): (
        "DEVELOPER MODE",
        "developerDisabled",
        "screenHeight * 0.22f",
        "layout.gameWidth = screenWidth",
        "layout.gameHeight = screenHeight - layout.hudHeight",
    ),
}
for relative, markers in interaction_markers.items():
    with open(os.path.join(root, relative), "r", encoding="utf-8") as stream:
        source = stream.read()
    for marker in markers:
        if marker not in source:
            print("error: build 11 interaction marker missing:", relative, marker)
            failed = True

if failed:
    sys.exit(2)

print("ok: direct difficulty/shot taps, safe character confirm, developer toggle and full portrait layout")

low_effect_markers = {
    os.path.join("src", "EffectManager.cpp"): (
        "avoid all ordinary particle rendering",
    ),
    os.path.join("src", "BulletManager.cpp"): (
        "Hide its donut effect",
        "bullet->state == BULLET_DESPAWN",
    ),
    os.path.join("src", "EnemyManager.cpp"): (
        "effectQuality != QUALITY_WORST && vm->anmFileIdx",
        "effectQuality != QUALITY_WORST && enemy->trailFlags",
    ),
    os.path.join("src", "Stage.cpp"): (
        "arg->spellCardState >= 1 &&",
        "effectQuality != QUALITY_WORST",
    ),
}
for relative, markers in low_effect_markers.items():
    with open(os.path.join(root, relative), "r", encoding="utf-8") as stream:
        source = stream.read()
    for marker in markers:
        if marker not in source:
            print("error: complete low-effects marker missing:", relative, marker)
            failed = True

if failed:
    sys.exit(2)

print("ok: low effects suppresses particles, spawn/despawn VMs, enemy sub-VMs, trails and spell VMs")

if "g_UiVertices.size() * sizeof(f32)" in mobile_ui_source:
    print("error: unsafe UI vertex upload size regression")
    sys.exit(2)
if "g_UiVertices.size() * sizeof(UiVertex)" not in mobile_ui_source:
    print("error: complete UI vertex upload is missing")
    sys.exit(2)
print("ok: complete interleaved UI vertex buffer upload")


def portrait_layout(width, height):
    hud_height = max(1, min(round(height * 0.22), height - 1))
    return 0, 0, width, hud_height, 0, hud_height, width, height - hud_height


for width, height in ((320, 568), (375, 812), (390, 844), (414, 896),
                      (768, 1024), (1024, 1366)):
    layout = portrait_layout(width, height)
    hud_x, hud_y, hud_w, hud_h, game_x, game_y, game_w, game_h = layout
    if (hud_x < 0 or game_x < 0 or hud_y < 0 or
            hud_x + hud_w > width or game_x + game_w > width or
            game_y + game_h > height):
        print("error: portrait layout exceeds screen", width, height, layout)
        sys.exit(2)
    if (hud_x != 0 or hud_y != 0 or game_x != 0 or
            hud_w != width or game_w != width or
            game_y != hud_h or game_y + game_h != height):
        print("error: portrait layout does not cover the full screen", width, height, layout)
        sys.exit(2)
    if not (0.20 <= hud_h / height <= 0.24):
        print("error: portrait HUD is not compact", width, height, layout)
        sys.exit(2)
print("ok: gap-free full-width portrait layouts for compact, 19.5:9 and iPad screens")

resource_identity = 0
for relative in ("assets/th07.dat", "assets/thbgm.dat", "assets/msgothic.ttc"):
    asset_digest = digest(os.path.join(root, relative))
    print("sha256:", relative, asset_digest)
    resource_identity ^= int(asset_digest[:8], 16)
online_source = open(os.path.join(root, "src", "Online.cpp"), encoding="utf-8").read()
identity_marker = f"kResourceIdentity = 0x{resource_identity:08x}u"
if identity_marker not in online_source:
    print("error: Online resource identity is stale; expected", identity_marker)
    sys.exit(2)
print("ok: Online resource identity", f"{resource_identity:08x}")
