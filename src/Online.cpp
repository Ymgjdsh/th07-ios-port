#include "Online.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netdb.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

#include "LocalNetworkPermissionIOS.hpp"
#include "AsciiManager.hpp"
#include "GameManager.hpp"
#include "Controller.hpp"
#include "Gui.hpp"
#include "MobileDiagnostics.hpp"
#include "OnlineFrameHistory.hpp"
#include "OnlineStartupProtocol.hpp"
#include "OnlineLauncherIOS.hpp"
#include "OnlineTextInputIOS.hpp"
#include "Player.hpp"
#include "Rng.hpp"
#include "Supervisor.hpp"
#if defined(TH07_IOS)
#include "../ios/BluetoothPeerTransport.hpp"
#endif

namespace
{
constexpr u32 kMagic = 0x374f4e54; // TNO7
// Build 28 changes startup barrier recovery and authoritative correction
// semantics. Keep mixed builds out of the same session.
constexpr u16 kVersion = 9;
constexpr u32 kBuildIdentity = 0x07060006u;
// XOR of the first SHA-256 words for th07.dat, thbgm.dat and msgothic.ttc.
constexpr u32 kResourceIdentity = 0xf09a7ea3u;
constexpr u16 kPort = 37707;
constexpr u8 kHello = 1, kWelcome = 2, kInput = 3, kBye = 4, kHeartbeat = 5, kAck = 6,
             kAuthoritativeState = 7, kIncompatible = 8;
constexpr u8 kPrepare = 9, kPrepareAck = 10, kMenuReady = 11, kMenuCommit = 12;
constexpr u8 kGameReady = 13, kGameCommit = 14;
constexpr u32 kDiscoveryIntervalMs = 450;
constexpr u32 kHandshakeIntervalMs = 300;
constexpr u32 kHeartbeatIntervalMs = 500;
constexpr u32 kPeerTimeoutMs = 4000;
constexpr u32 kInputStallTimeoutMs = 12000;
constexpr u32 kInputRetransmitIntervalMs = 24;
// Bound recovery traffic after a delayed UDP packet. Without a budget, every
// pending frame is resent in one tick and the resulting burst makes the
// lockstep renderer appear to stutter on iOS Wi-Fi.
constexpr u32 kInputSendBudgetPerTick = 8;
constexpr int kLanInputDelayFloor = 5;
constexpr int kDefaultInputDelay = 3;
constexpr int kUdpSocketBufferBytes = 256 * 1024;
constexpr u32 kBluetoothReconnectGraceMs = 10000;
// Shared menus are handed off on a logical input frame.  The close animation
// itself is local, but the commit must not be exposed before both peers have
// consumed the same logical frame.  Keeping a small lead also gives a lost
// UDP packet a chance to be retransmitted before the shell is closed.
constexpr u32 kSharedShellLeadFrames = 6;
constexpr u8 kShellAckDelivered = 0x01;
constexpr u8 kShellConfirmShift = 4;
constexpr u8 kShellConfirmMask = 0x30;

#pragma pack(push, 1)
struct PlayerStatePacket
{
    i16 positionX16;
    i16 positionY16;
    u16 respawnTimer;
    u16 bombTimer;
    u8 playerState;
    u8 active;
    u8 bombActive;
    u8 focus;
    u8 lives;
    u8 bombs;
    u8 power;
    u8 reserved;
};

struct Packet
{
    u32 magic; u16 version; u8 kind; u8 role; u32 sequence; u32 timestamp;
    u32 frame; u16 buttons; i16 touchDx16; i16 touchDy16;
    u16 port; u32 session; u32 barrierEpoch;
    u8 lifeCount; u8 bombCount; u8 slowMode; u8 reserved2;
    u32 ackFrame; u32 ackMask; u32 buildIdentity; u32 resourceIdentity;
    u16 difficulty; u8 stage; u8 menuState;
    u8 p1Character; u8 p1Shot; u8 p2Character; u8 p2Shot; u32 stateHash;
    u32 authoritativeFrame;
    PlayerStatePacket p1State;
    PlayerStatePacket p2State;
    u8 shellKind;
    u8 shellSelectionP1;
    u8 shellSelectionP2;
    u8 shellVoteP1;
    u8 shellVoteP2;
    u8 shellCommit;
    u8 shellRequest;
    u8 shellReserved;
    u32 shellRevision;
    u32 shellHandoffFrame;
    i8 shellSelectionRequest;
    u8 shellSelectionValid;
    i16 menuCursor;
    u8 menuCursorValid;
    u8 menuReserved;
    char name[24];
};
#pragma pack(pop)

constexpr u32 kInputHistorySize = 256;
struct InputFrame
{
    u32 frame = 0;
    u16 buttons = 0;
    f32 touchDx = 0.0f;
    f32 touchDy = 0.0f;
    u32 stateHash = 0;
    i8 shellSelectionRequest = -1;
    bool shellSelectionValid = false;
    i16 menuCursor = -1;
    bool present = false;
};

enum StartupPhase
{
    STARTUP_NONE = 0,
    STARTUP_PREPARING,
    STARTUP_MENU_WAIT,
    STARTUP_MENU_COMMITTED,
    STARTUP_GAME_WAIT,
    STARTUP_GAME_COMMITTED,
    STARTUP_FAILED,
};

socket_t g_Socket = kInvalidSocket;
Online::State g_State = Online::STATE_IDLE;
Online::Mode g_Mode = Online::MODE_NEARBY_LAN;
bool g_MenuOpen = false, g_Host = false;
bool g_StartGameRequested = false, g_LocalGameRequested = false;
bool g_MultiplayerSession = false, g_LocalSession = false;
bool g_InputSynchronizationActive = false;
u16 g_StartSeed = 0;
StartupPhase g_StartupPhase = STARTUP_NONE;
u32 g_LaunchNonce = 0;
bool g_MenuReadySent = false, g_PeerMenuReady = false, g_GameReadySent = false;
bool g_PeerGameReady = false, g_GameplayCommit = false;
i32 g_MenuReadyState = -1, g_PeerMenuState = -1;
i32 g_MenuReadyCursor = -1, g_PeerMenuCursor = -1;
i32 g_GameDifficulty = -1, g_GameStage = -1;
i32 g_PeerGameDifficulty = -1, g_PeerGameStage = -1;
i32 g_PeerCharacters[2] = {0, 0}, g_PeerShots[2] = {0, 0};
u32 g_StartupStartedAt = 0, g_LastStartupSend = 0;
u32 g_BarrierEpoch = 0;
u32 g_BluetoothDisconnectedAt = 0;
int g_InputDelay = kDefaultInputDelay;
bool g_InputDelayUserSet = false;
u32 g_Sequence = 0, g_Session = 0, g_LastDiscovery = 0, g_LastHandshake = 0;
u32 g_LastHeartbeat = 0, g_LastPeer = 0, g_LastRtt = 0;
u32 g_InputFrame = 0;
bool g_LocalInputSent = false;
InputFrame g_RemoteInputs[kInputHistorySize];
OnlineFrameHistory<InputFrame, kInputHistorySize> g_LocalInputHistory;
u32 g_RemoteAckFrame = 0xffffffffu, g_RemoteAckMask = 0;
u32 g_LastRemoteFrame = 0xffffffffu, g_RemoteFrameMask = 0;
u32 g_LastSyncProgress = 0;
u32 g_InputRetransmits = 0, g_InputDuplicates = 0, g_InputOutOfOrder = 0;
bool g_InputStalled = false;
u32 g_LastAuthoritativeFrame = 0;
PlayerStatePacket g_RemoteP1State = {};
PlayerStatePacket g_RemoteP2State = {};
bool g_RemoteAuthoritativeStatePresent = false;
// State packets received while a shared shell is open belong to the old
// gameplay epoch.  Retain their revision so they cannot resurrect a dead
// player after a retry commit.
u32 g_RemoteAuthoritativeShellRevision = 0;
u32 g_LastAuthoritativeSendFrame = 0xffffffffu;
sockaddr_in g_Peer = {};
char g_Status[128] = "Offline", g_PeerAddress[64] = {};
char g_RelayEndpoint[128] = {}, g_RelayRoom[64] = {};
char g_DirectAddress[64] = "192.168.1.2";
u32 g_LastRelayRegister = 0;
i32 g_PlayerCharacters[2] = {0, 0};
i32 g_PlayerShots[2] = {0, 0};
bool g_Player2LoadoutSelected = false;
bool g_SelectingPlayer2Loadout = false;
Online::SharedShellKind g_ShellKind = Online::SHARED_SHELL_NONE;
u8 g_ShellSelection[2] = {0, 0};
u8 g_ShellVote[2] = {0, 0};
u32 g_ShellRevision = 0;
Online::SharedShellCommit g_ShellCommit = Online::SHARED_COMMIT_NONE;
Online::SharedShellConfirmAction g_ShellConfirmAction = Online::SHARED_CONFIRM_NONE;
u32 g_ShellHandoffFrame = 0;
u32 g_ShellCommitRevision = 0;
u8 g_ShellRequest = 0;
u32 g_ShellOpenedFrame = 0xffffffffu;
u16 g_QueuedShellButtons = 0;
u16 g_QueuedInputButtons = 0;
u16 g_LastShellButtons[2] = {0, 0};
bool g_ShellCommitDelivered = false;
bool g_ShellCommitAcked = false;
bool g_ShellCloseComplete = false;
bool g_ShellHandoffReady = false;
i32 g_LocalMenuCursor = -1;
i32 g_RemoteMenuCursor = -1;
i32 g_MenuCursorTarget = -1;
i32 g_LocalShellSelectionRequest = -1;
i32 g_RemoteShellSelectionRequest = -1;
u32 g_LastRemoteShellSelectionFrame = 0xffffffffu;

static void ApplyTransportInputDelay(Online::Mode mode)
{
    if (g_InputDelayUserSet) return;
    // Reliable Bluetooth already provides an ordered queue. UDP needs a few
    // frames of jitter room so a short Wi-Fi scheduling pause does not stop
    // the deterministic simulation. The host advertises this value in the
    // prepare packet and the guest adopts the same delay.
    g_InputDelay = mode == Online::MODE_BLUETOOTH ? kDefaultInputDelay
                                                   : kLanInputDelayFloor;
}

static void PresentSharedShell(Online::SharedShellKind kind)
{
    // Do not let retransmitted state packets reopen a shell that is already
    // closing.  If the opening packet was lost, the first commit packet can
    // still create the presentation while the menu flags are clear.
    if (g_ShellCommit != Online::SHARED_COMMIT_NONE &&
        (g_GameManager.isInPauseMenu || g_GameManager.isInRetryMenu))
        return;
    if (kind == Online::SHARED_SHELL_PAUSE)
    {
        if (!g_GameManager.isInPauseMenu)
        {
            // The native PauseMenu object survives between pauses. Reset a
            // previous close/transition state before exposing the new shared
            // shell, or state 4/9/10 can immediately close it again.
            g_AsciiManager.pauseMenu.curState = 0;
            g_AsciiManager.pauseMenu.numFrames = 0;
            g_GameManager.isInPauseMenu = 1;
            g_GameManager.isPaused = 1;
            g_GameManager.arcadeRegionTopLeftPos.x = 32.0f;
            g_GameManager.arcadeRegionTopLeftPos.y = 16.0f;
            g_GameManager.arcadeRegionSize.x = 384.0f;
            g_GameManager.arcadeRegionSize.y = 448.0f;
        }
    }
    else if (kind == Online::SHARED_SHELL_RETRY)
    {
        if (!g_GameManager.isInRetryMenu)
        {
            g_AsciiManager.retryMenu.curState = 0;
            g_AsciiManager.retryMenu.numFrames = 0;
            g_GameManager.isInRetryMenu = 1;
        }
        g_GameManager.isPaused = 1;
    }
}

static bool IsBattlePauseInputAllowed()
{
    // A menu gesture can arrive while the title/setup scene is still using the
    // gameplay-sized viewport. Only a live stage may turn it into a shared
    // pause shell; dialogue and loading remain controlled by their own lanes.
    return g_MultiplayerSession && g_GameManager.notInMenu &&
           g_GameManager.globals && g_GameManager.currentStage >= 1 &&
           g_GameManager.currentStage <= 6 && g_GameManager.framesThisStage > 0 &&
           g_Supervisor.curState == 2 &&
           !g_GameManager.demo && !g_GameManager.replay &&
           !g_GameManager.finished && !g_Gui.HasCurrentMsgIdx();
}

static void UpdateShellCloseComplete()
{
    if (!g_ShellCommitDelivered || g_ShellCommit == Online::SHARED_COMMIT_NONE)
        return;
    // A commit is consumed on a logical input frame, while the native menu
    // still needs to finish its close animation.  A state-based check keeps
    // both devices on the same handoff even when their render rates differ.
    if (!g_GameManager.isInPauseMenu && !g_GameManager.isInRetryMenu)
    {
        if (!g_ShellCloseComplete)
            MobileDiagnostics::Log("online/shell", "close complete commit=%d frame=%u",
                                   (int)g_ShellCommit, g_InputFrame);
        g_ShellCloseComplete = true;
    }
}

static void OpenSharedShellOnHost(Online::SharedShellKind kind)
{
    if (!g_Host || !g_MultiplayerSession || kind == Online::SHARED_SHELL_NONE ||
        g_ShellKind != Online::SHARED_SHELL_NONE)
        return;
    g_ShellKind = kind;
    g_ShellSelection[0] = g_ShellSelection[1] = 0;
    g_ShellVote[0] = g_ShellVote[1] = 0;
    g_ShellCommit = Online::SHARED_COMMIT_NONE;
    g_ShellConfirmAction = Online::SHARED_CONFIRM_NONE;
    g_ShellCommitDelivered = false;
    g_ShellCommitAcked = false;
    g_ShellCloseComplete = false;
    g_ShellHandoffReady = false;
    g_ShellHandoffFrame = 0;
    g_ShellOpenedFrame = g_InputFrame;
    g_ShellCommitRevision = 0;
    g_RemoteShellSelectionRequest = -1;
    g_ShellRequest = 0;
    // A gameplay pulse can still be queued when the shell opens. It belongs
    // to the old scene and must not become an immediate shell vote.
    g_QueuedShellButtons = 0;
    g_QueuedInputButtons &= (u16)~TH_BUTTON_MENU;
    // A gameplay SHOOT/FIRE bit may still be held when the shell opens.  Do
    // not let that old bit suppress the first shell confirmation edge.
    g_LastShellButtons[0] = g_LastShellButtons[1] = 0;
    ++g_ShellRevision;
    PresentSharedShell(kind);
    MobileDiagnostics::Log("online/shell", "open kind=%d revision=%u", (int)kind,
                           g_ShellRevision);
}

static bool ShellButtonEdge(u16 buttons, u16 button, u8 playerId)
{
    // TH_BUTTON_SELECTMENU is a combination of ENTER|SHOOT.  Requiring both
    // bits to be released made a tap disappear whenever the virtual shoot
    // button was still held from the previous frame.  Match WAS_PRESSED_RAW:
    // any change in the masked bits is a single edge.
    return (buttons & button) != 0 &&
           (buttons & button) != (g_LastShellButtons[playerId] & button);
}

static void CommitSharedShell(Online::SharedShellCommit commit)
{
    if (g_ShellCommit != Online::SHARED_COMMIT_NONE) return;
    g_ShellCommit = commit;
    g_ShellCommitDelivered = false;
    g_ShellCommitAcked = false;
    g_ShellCloseComplete = false;
    g_ShellHandoffReady = false;
    g_RemoteAuthoritativeStatePresent = false;
    g_LastAuthoritativeSendFrame = 0xffffffffu;
    ++g_ShellRevision;
    g_ShellCommitRevision = g_ShellRevision;
    g_ShellHandoffFrame = g_InputFrame + kSharedShellLeadFrames;
    MobileDiagnostics::Log("online/shell", "commit=%d revision=%u", (int)commit,
                           g_ShellRevision);
}

static void ProcessSharedShellFrame(u16 p1Buttons, u16 p2Buttons,
                                    const InputFrame &p1Input,
                                    const InputFrame &p2Input)
{
    if (!g_Host || !g_MultiplayerSession || g_ShellKind == Online::SHARED_SHELL_NONE ||
        g_ShellCommit != Online::SHARED_COMMIT_NONE)
        return;
    const u16 buttons[2] = {p1Buttons, p2Buttons};
    if (g_ShellOpenedFrame == g_InputFrame)
    {
        // The opening MENU edge belongs to gameplay, not to the first menu
        // page. Record it as the previous state and wait for a fresh tap.
        g_LastShellButtons[0] = buttons[0];
        g_LastShellButtons[1] = buttons[1];
        return;
    }
    const bool confirmingPause = g_ShellKind == Online::SHARED_SHELL_PAUSE &&
                                 g_ShellConfirmAction != Online::SHARED_CONFIRM_NONE;
    const u8 shellMaxSelection = confirmingPause ? 1 :
        (g_ShellKind == Online::SHARED_SHELL_PAUSE ? 2 : 1);
    const u8 oldSelection0 = g_ShellSelection[0];
    const u8 oldSelection1 = g_ShellSelection[1];
    const u8 oldVote0 = g_ShellVote[0];
    const u8 oldVote1 = g_ShellVote[1];
    // A touch selection belongs to the input frame that carried it.  Looking
    // at g_LocalShellSelectionRequest here is racy: SendStoredInput clears the
    // live queue as soon as the UDP packet is accepted, which used to make a
    // local tap disappear before the host consumed the frame.  Consume the
    // immutable frame values instead, so retransmission and lockstep stalls
    // cannot lose or repeat a vote.
    const i32 localSelectionRequest = p1Input.shellSelectionValid
                                          ? p1Input.shellSelectionRequest
                                          : -1;
    const i32 remoteSelectionRequest = p2Input.shellSelectionValid
                                           ? p2Input.shellSelectionRequest
                                           : -1;
    if (localSelectionRequest >= 0)
    {
        g_ShellSelection[0] = (u8)std::clamp(localSelectionRequest, 0, (int)shellMaxSelection);
        g_ShellVote[0] = 0;
    }
    if (remoteSelectionRequest >= 0)
    {
        g_ShellSelection[1] = (u8)std::clamp(remoteSelectionRequest, 0, (int)shellMaxSelection);
        g_ShellVote[1] = 0;
    }
    for (u8 playerId = 0; playerId < 2; ++playerId)
    {
        if (ShellButtonEdge(buttons[playerId], TH_BUTTON_UP, playerId))
        {
            const u8 maxSelection = confirmingPause ? 1 :
                (g_ShellKind == Online::SHARED_SHELL_PAUSE ? 2 : 1);
            g_ShellSelection[playerId] = g_ShellSelection[playerId] == 0
                                              ? maxSelection
                                              : g_ShellSelection[playerId] - 1;
            g_ShellVote[playerId] = 0;
        }
        if (ShellButtonEdge(buttons[playerId], TH_BUTTON_DOWN, playerId))
        {
            const u8 maxSelection = confirmingPause ? 1 :
                (g_ShellKind == Online::SHARED_SHELL_PAUSE ? 2 : 1);
            g_ShellSelection[playerId] = g_ShellSelection[playerId] >= maxSelection
                                              ? 0
                                              : g_ShellSelection[playerId] + 1;
            g_ShellVote[playerId] = 0;
        }
        if (ShellButtonEdge(buttons[playerId], TH_BUTTON_SELECTMENU, playerId))
            g_ShellVote[playerId] = 1;
        if (ShellButtonEdge(buttons[playerId], TH_BUTTON_MENU, playerId) ||
            ShellButtonEdge(buttons[playerId], TH_BUTTON_Q, playerId))
            g_ShellVote[playerId] = 0;
    }
    // Selection and vote changes are state changes, not transient input. A
    // monotonic revision lets the guest reject delayed UDP packets from before
    // the latest choice, which otherwise made the marker visibly flicker.
    if (oldSelection0 != g_ShellSelection[0] || oldSelection1 != g_ShellSelection[1] ||
        oldVote0 != g_ShellVote[0] || oldVote1 != g_ShellVote[1])
    {
        ++g_ShellRevision;
    }
    if (g_ShellVote[0] != 1 || g_ShellVote[1] != 1 ||
        g_ShellSelection[0] != g_ShellSelection[1])
        return;
    if (g_ShellKind == Online::SHARED_SHELL_PAUSE)
    {
        if (g_ShellConfirmAction != Online::SHARED_CONFIRM_NONE)
        {
            if (g_ShellSelection[0] == 0)
            {
                CommitSharedShell(g_ShellConfirmAction == Online::SHARED_CONFIRM_TITLE
                                      ? Online::SHARED_COMMIT_TITLE
                                      : Online::SHARED_COMMIT_RESET);
            }
            else
            {
                const u8 primarySelection =
                    g_ShellConfirmAction == Online::SHARED_CONFIRM_TITLE ? 1 : 2;
                g_ShellConfirmAction = Online::SHARED_CONFIRM_NONE;
                g_ShellSelection[0] = g_ShellSelection[1] = primarySelection;
                g_ShellVote[0] = g_ShellVote[1] = 0;
                ++g_ShellRevision;
            }
            return;
        }
        switch (g_ShellSelection[0])
        {
        case 0: CommitSharedShell(Online::SHARED_COMMIT_RESUME); break;
        case 1:
            g_ShellConfirmAction = Online::SHARED_CONFIRM_TITLE;
            g_ShellSelection[0] = g_ShellSelection[1] = 0;
            g_ShellVote[0] = g_ShellVote[1] = 0;
            ++g_ShellRevision;
            break;
        default:
            g_ShellConfirmAction = Online::SHARED_CONFIRM_RESET;
            g_ShellSelection[0] = g_ShellSelection[1] = 0;
            g_ShellVote[0] = g_ShellVote[1] = 0;
            ++g_ShellRevision;
            break;
        }
    }
    else
    {
        // Retry is a shared continuation. If either player chooses the title,
        // the game exits together once both players confirm their choice.
        CommitSharedShell(g_ShellSelection[0] == 1
                              ? Online::SHARED_COMMIT_TITLE
                              : Online::SHARED_COMMIT_RETRY);
    }
}

static void ApplyRemoteSharedShellState(const Packet &packet)
{
    if (g_Host || !g_MultiplayerSession || packet.role != 1 ||
        packet.session != g_LaunchNonce || packet.shellRevision <= g_ShellRevision)
        return;

    const Online::SharedShellKind previousKind = g_ShellKind;
    const Online::SharedShellCommit previousCommit = g_ShellCommit;
    const bool hadPause = g_GameManager.isInPauseMenu != 0;
    const bool hadRetry = g_GameManager.isInRetryMenu != 0;
    const Online::SharedShellKind incomingKind =
        (Online::SharedShellKind)std::clamp((int)packet.shellKind, 0, 2);
    const Online::SharedShellCommit incomingCommit =
        (Online::SharedShellCommit)std::clamp((int)packet.shellCommit, 0,
                                              (int)Online::SHARED_COMMIT_RESET);
    const Online::SharedShellConfirmAction incomingConfirmAction =
        (Online::SharedShellConfirmAction)std::clamp(
            (int)((packet.shellReserved & kShellConfirmMask) >> kShellConfirmShift),
            (int)Online::SHARED_CONFIRM_NONE, (int)Online::SHARED_CONFIRM_RESET);

    // The host only publishes shellKind=NONE after the guest has finished its
    // close animation.  If an older terminal packet is reordered ahead of the
    // local animation, keep the shell state intact instead of reopening and
    // closing it on alternating frames.
    UpdateShellCloseComplete();
    if (incomingKind == Online::SHARED_SHELL_NONE &&
        incomingCommit == Online::SHARED_COMMIT_NONE &&
        g_ShellCommit != Online::SHARED_COMMIT_NONE &&
        (!g_ShellCommitDelivered || !g_ShellCloseComplete))
        return;

    g_ShellRevision = packet.shellRevision;
    g_ShellKind = incomingKind;
    if (previousKind == Online::SHARED_SHELL_NONE &&
        incomingKind != Online::SHARED_SHELL_NONE)
    {
        // The packet that announces a shell may arrive in the same update
        // tick as the local input frame. Do not let that frame's old MENU edge
        // be interpreted as a shell action on the guest.
        g_ShellOpenedFrame = g_InputFrame;
    }
    g_ShellConfirmAction = incomingConfirmAction;
    const u8 maxSelection = g_ShellConfirmAction != Online::SHARED_CONFIRM_NONE ? 1 :
        (g_ShellKind == Online::SHARED_SHELL_PAUSE ? 2 : 1);
    g_ShellSelection[0] = std::min(packet.shellSelectionP1, maxSelection);
    g_ShellSelection[1] = std::min(packet.shellSelectionP2, maxSelection);
    g_ShellVote[0] = packet.shellVoteP1 ? 1 : 0;
    g_ShellVote[1] = packet.shellVoteP2 ? 1 : 0;

    if (incomingCommit != Online::SHARED_COMMIT_NONE &&
        previousCommit != incomingCommit)
    {
        g_ShellCommit = incomingCommit;
        g_ShellCommitDelivered = false;
        g_ShellCommitAcked = false;
        g_ShellCloseComplete = false;
        g_ShellCommitRevision = packet.shellRevision;
        g_ShellHandoffFrame = packet.shellHandoffFrame;
        // Older packets from this same source did not carry a handoff frame.
        // Derive a conservative one from the first input frame that carried
        // the commit instead of falling back to a wall-clock timeout.
        if (g_ShellHandoffFrame == 0)
            g_ShellHandoffFrame = packet.frame + kSharedShellLeadFrames;
        g_ShellHandoffReady = g_InputFrame >= g_ShellHandoffFrame;
        // Any queued authoritative state is from before the retry/reset.
        g_RemoteAuthoritativeStatePresent = false;
        g_RemoteAuthoritativeShellRevision = 0;
    }
    else if (incomingKind == Online::SHARED_SHELL_NONE &&
             incomingCommit == Online::SHARED_COMMIT_NONE)
    {
        // Terminal shell state is accepted only after the local close has
        // completed. Clear every one-shot field even when the local commit
        // was already consumed; otherwise a delayed terminal packet can leave
        // an old ACK, selection, or handoff latched into the next shell.
        g_ShellCommit = Online::SHARED_COMMIT_NONE;
        g_ShellCommitRevision = 0;
        g_ShellHandoffFrame = 0;
        g_ShellHandoffReady = false;
        g_ShellCommitDelivered = false;
        g_ShellCommitAcked = false;
        g_ShellCloseComplete = false;
        g_RemoteAuthoritativeStatePresent = false;
        g_RemoteAuthoritativeShellRevision = 0;
    }

    if (g_ShellCommit != Online::SHARED_COMMIT_NONE && g_ShellHandoffFrame != 0 &&
        g_InputFrame >= g_ShellHandoffFrame)
        g_ShellHandoffReady = true;

    // A guest request is retransmitted until the host echoes the opened shell.
    if (g_ShellRequest != 0 && g_ShellKind == (Online::SharedShellKind)g_ShellRequest)
        g_ShellRequest = 0;

    const bool shellChanged = previousKind != g_ShellKind || previousCommit != g_ShellCommit;
    if (g_ShellKind != Online::SHARED_SHELL_NONE &&
        (shellChanged || (!hadPause && !hadRetry)))
        PresentSharedShell(g_ShellKind);
    if (g_ShellKind == Online::SHARED_SHELL_NONE &&
        g_ShellCommit == Online::SHARED_COMMIT_NONE)
    {
        g_ShellSelection[0] = g_ShellSelection[1] = 0;
        g_ShellVote[0] = g_ShellVote[1] = 0;
        g_ShellConfirmAction = Online::SHARED_CONFIRM_NONE;
        g_LastShellButtons[0] = g_LastShellButtons[1] = 0;
        g_LocalShellSelectionRequest = g_RemoteShellSelectionRequest = -1;
        // A terminal shell packet is the boundary between gameplay epochs.
        // Clear every one-shot lifecycle flag even when the guest had already
        // observed the commit in an earlier packet.  Leaving one of these bits
        // latched can make a later heartbeat look like a stale acknowledgement.
        g_ShellCommitDelivered = false;
        g_ShellCommitAcked = false;
        g_ShellCloseComplete = false;
        g_ShellHandoffReady = false;
        g_ShellHandoffFrame = 0;
        g_ShellCommitRevision = 0;
        g_RemoteAuthoritativeStatePresent = false;
        g_RemoteAuthoritativeShellRevision = 0;
        g_GameManager.isInPauseMenu = 0;
        g_GameManager.isInRetryMenu = 0;
        g_GameManager.isPaused = 0;
        g_ShellOpenedFrame = 0xffffffffu;
    }
    g_ShellRequest = 0;
}

void ResetSyncState()
{
    g_InputFrame = 0;
    g_LocalInputSent = false;
    for (InputFrame &input : g_RemoteInputs) input = {};
    g_LocalInputHistory.Clear();
    g_RemoteAckFrame = 0xffffffffu;
    g_RemoteAckMask = 0;
    g_LastRemoteFrame = 0xffffffffu;
    g_RemoteFrameMask = 0;
    g_LastSyncProgress = SDL_GetTicks();
    g_InputRetransmits = g_InputDuplicates = g_InputOutOfOrder = 0;
    g_InputStalled = false;
    g_LastAuthoritativeFrame = 0;
    g_RemoteP1State = {};
    g_RemoteP2State = {};
    g_RemoteAuthoritativeStatePresent = false;
    g_RemoteAuthoritativeShellRevision = 0;
    g_LastAuthoritativeSendFrame = 0xffffffffu;
    g_ShellCloseComplete = false;
    if (g_ShellKind == Online::SHARED_SHELL_NONE)
        g_ShellOpenedFrame = 0xffffffffu;
    for (int frame = 0; frame < g_InputDelay; ++frame)
    {
        InputFrame zero = {};
        zero.frame = (u32)frame;
        zero.present = true;
        g_LocalInputHistory.Store((u32)frame, zero);
        // The delayed prefix is deterministic neutral input on both lanes.
        // Without the remote prefix, frame zero can never be consumed.
        g_RemoteInputs[frame % kInputHistorySize] = zero;
    }
}

void ActivateMultiplayerSession(bool local)
{
    g_MultiplayerSession = true;
    g_LocalSession = local;
    g_BarrierEpoch = 0;
    // Menu navigation is already carried by the same authoritative P1/P2
    // lanes as gameplay. Enabling it before the menu barrier lets the guest
    // follow the host into the difficulty page instead of remaining behind.
    g_InputSynchronizationActive = true;
    g_StartupPhase = local ? STARTUP_GAME_COMMITTED : STARTUP_PREPARING;
    g_StartupStartedAt = SDL_GetTicks();
    g_LastStartupSend = 0;
    g_MenuReadySent = g_PeerMenuReady = false;
    g_MenuReadyState = g_PeerMenuState = -1;
    g_MenuReadyCursor = g_PeerMenuCursor = -1;
    g_GameReadySent = g_PeerGameReady = false;
    g_GameDifficulty = g_GameStage = -1;
    g_PeerGameDifficulty = g_PeerGameStage = -1;
    g_GameplayCommit = local;
    g_BluetoothDisconnectedAt = 0;
    g_ShellKind = Online::SHARED_SHELL_NONE;
    g_ShellSelection[0] = g_ShellSelection[1] = 0;
    g_ShellVote[0] = g_ShellVote[1] = 0;
    g_ShellRevision = 0;
    g_ShellCommit = Online::SHARED_COMMIT_NONE;
    g_ShellConfirmAction = Online::SHARED_CONFIRM_NONE;
    g_ShellCommitDelivered = false;
    g_ShellCommitAcked = false;
    g_ShellHandoffReady = false;
    g_ShellHandoffFrame = 0;
    g_ShellCommitRevision = 0;
    g_ShellRequest = 0;
    g_QueuedShellButtons = 0;
    g_QueuedInputButtons = 0;
    g_LastShellButtons[0] = g_LastShellButtons[1] = 0;
    g_LocalMenuCursor = g_RemoteMenuCursor = g_MenuCursorTarget = -1;
    g_LocalShellSelectionRequest = g_RemoteShellSelectionRequest = -1;
    ResetSyncState();
    g_PlayerCharacters[0] = g_PlayerCharacters[1] = 0;
    g_PlayerShots[0] = g_PlayerShots[1] = 0;
    g_Player2LoadoutSelected = false;
    g_SelectingPlayer2Loadout = false;
}

void StoreLocalAcknowledgement(const Packet &packet)
{
    if (!g_MultiplayerSession || packet.session != g_LaunchNonce ||
        packet.barrierEpoch != g_BarrierEpoch) return;
    if (packet.ackFrame == 0xffffffffu && packet.ackMask == 0) return;
    g_LocalInputHistory.Acknowledge(packet.ackFrame, packet.ackMask);
    g_RemoteAckFrame = packet.ackFrame;
    g_RemoteAckMask = packet.ackMask;
}

void StoreRemoteInput(const Packet &packet)
{
    if (!g_MultiplayerSession || packet.session != g_LaunchNonce ||
        packet.barrierEpoch != g_BarrierEpoch) return;
    if (g_Host && g_ShellCommit != Online::SHARED_COMMIT_NONE && packet.role == 2 &&
        packet.shellCommit == (u8)g_ShellCommit &&
         packet.shellRevision == g_ShellCommitRevision &&
        (packet.shellReserved & kShellAckDelivered) != 0)
    {
        g_ShellCommitAcked = true;
    }
    ApplyRemoteSharedShellState(packet);
    if (g_Host && packet.shellRequest != 0 && g_ShellKind == Online::SHARED_SHELL_NONE)
    {
        if (packet.shellRequest == (u8)Online::SHARED_SHELL_RETRY)
        {
            // The guest's last-life transition is local to the guest's
            // player chain.  Carry the transition with the shell request so
            // the host cannot continue drawing or simulating a dead P2 while
            // the retry vote is open.
            g_PlayerActive[1] = false;
            g_Players[1].playerState = PLAYER_STATE_DEAD;
            g_Players[1].bombInfo.isInUse = 0;
            g_Players[1].isBombing = 0;
        }
        OpenSharedShellOnHost((Online::SharedShellKind)std::clamp((int)packet.shellRequest, 1, 2));
        // The request is edge-triggered.  Leaving it set would cause the host
        // to reopen a stale retry/pause shell as soon as the current shell
        // closes (the guest retransmits ordinary heartbeat packets too).
        g_ShellRequest = 0;
    }
    StoreLocalAcknowledgement(packet);
    if (packet.frame < g_InputFrame || packet.frame >= g_InputFrame + kInputHistorySize) return;
    InputFrame &slot = g_RemoteInputs[packet.frame % kInputHistorySize];
    if (slot.present && slot.frame == packet.frame)
    {
        ++g_InputDuplicates;
        return;
    }
    const u32 expected = g_LastRemoteFrame == 0xffffffffu ? 0 : g_LastRemoteFrame + 1;
    if (packet.frame != expected) ++g_InputOutOfOrder;
    slot.frame = packet.frame;
    slot.buttons = packet.buttons;
    slot.touchDx = (f32)packet.touchDx16 / 16.0f;
    slot.touchDy = (f32)packet.touchDy16 / 16.0f;
    slot.stateHash = packet.stateHash;
    slot.shellSelectionRequest = packet.shellSelectionValid
                                     ? (i8)std::clamp((int)packet.shellSelectionRequest, 0, 2)
                                     : -1;
    slot.shellSelectionValid = packet.shellSelectionValid != 0;
    slot.menuCursor = packet.menuCursorValid ? packet.menuCursor : -1;
    if (packet.menuCursorValid) g_RemoteMenuCursor = packet.menuCursor;
    slot.present = true;
    // Selection requests are kept in the input frame itself.  Do not mirror
    // them into a live global here: a retransmitted packet would otherwise
    // clear an already-confirmed vote after the frame had been consumed.
    while (true)
    {
        const u32 next = g_LastRemoteFrame == 0xffffffffu ? 0 : g_LastRemoteFrame + 1;
        const InputFrame &candidate = g_RemoteInputs[next % kInputHistorySize];
        if (!candidate.present || candidate.frame != next) break;
        g_LastRemoteFrame = next;
    }
    g_RemoteFrameMask = 0;
    const u32 base = g_LastRemoteFrame == 0xffffffffu ? 0 : g_LastRemoteFrame + 1;
    for (u32 bit = 0; bit < 32; ++bit)
    {
        const u32 frame = base + bit;
        const InputFrame &candidate = g_RemoteInputs[frame % kInputHistorySize];
        if (candidate.present && candidate.frame == frame) g_RemoteFrameMask |= 1u << bit;
    }
}

void CloseSocket()
{
    if (g_Socket == kInvalidSocket) return;
#if defined(_WIN32)
    closesocket(g_Socket);
#else
    close(g_Socket);
#endif
    g_Socket = kInvalidSocket;
}
bool WouldBlock()
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}
void SetNonBlocking(socket_t socket)
{
#if defined(_WIN32)
    u_long value = 1; ioctlsocket(socket, FIONBIO, &value);
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags >= 0) fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
}
bool OpenSocket()
{
    CloseSocket();
#if defined(_WIN32)
    static bool winsockReady = false;
    if (!winsockReady) { WSADATA data = {}; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false; winsockReady = true; }
#endif
    g_Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_Socket == kInvalidSocket) return false;
    int reuse = 1, broadcast = 1;
    setsockopt(g_Socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#if defined(SO_REUSEPORT)
    setsockopt(g_Socket, SOL_SOCKET, SO_REUSEPORT, (const char *)&reuse, sizeof(reuse));
#endif
    setsockopt(g_Socket, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));
    // A short Wi-Fi scheduling pause can deliver several input frames at once.
    // Keep them in the kernel queue until Online::Update drains the socket.
    int socketBuffer = kUdpSocketBufferBytes;
    setsockopt(g_Socket, SOL_SOCKET, SO_RCVBUF, (const char *)&socketBuffer,
               sizeof(socketBuffer));
    setsockopt(g_Socket, SOL_SOCKET, SO_SNDBUF, (const char *)&socketBuffer,
               sizeof(socketBuffer));
    sockaddr_in local = {}; local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_ANY); local.sin_port = htons(kPort);
    if (bind(g_Socket, (sockaddr *)&local, sizeof(local)) != 0) { CloseSocket(); return false; }
    SetNonBlocking(g_Socket); return true;
}
void SetStatus(const char *status) { SDL_strlcpy(g_Status, status ? status : "Offline", sizeof(g_Status)); }
void FillPacket(Packet &packet, u8 kind, u16 buttons, u32 frame = 0,
                f32 touchDx = 0.0f, f32 touchDy = 0.0f)
{
    memset(&packet, 0, sizeof(packet)); packet.magic = kMagic; packet.version = kVersion;
    packet.kind = kind; packet.role = g_Host ? 1 : 2; packet.sequence = ++g_Sequence;
    packet.timestamp = SDL_GetTicks(); packet.frame = frame; packet.buttons = buttons;
    packet.touchDx16 = (i16)std::clamp((i32)lroundf(touchDx * 16.0f), -32768, 32767);
    packet.touchDy16 = (i16)std::clamp((i32)lroundf(touchDy * 16.0f), -32768, 32767);
    packet.port = kPort; packet.session = g_MultiplayerSession ? g_LaunchNonce : g_Session;
    packet.barrierEpoch = g_BarrierEpoch;
    packet.lifeCount = g_Supervisor.cfg.lifeCount;
    packet.bombCount = g_Supervisor.cfg.bombCount;
    packet.slowMode = g_Supervisor.cfg.slowMode;
    packet.reserved2 = (u8)g_InputDelay;
    packet.ackFrame = g_LastRemoteFrame;
    packet.ackMask = g_RemoteFrameMask;
    packet.buildIdentity = kBuildIdentity;
    packet.resourceIdentity = kResourceIdentity;
    packet.difficulty = (u16)std::max(0, g_GameDifficulty);
    packet.stage = (u8)std::max(0, g_GameStage);
    packet.menuState = (u8)std::max(0, g_MenuReadyState);
    packet.p1Character = (u8)std::clamp(g_PlayerCharacters[0], 0, 2);
    packet.p1Shot = (u8)std::clamp(g_PlayerShots[0], 0, 1);
    packet.p2Character = (u8)std::clamp(g_PlayerCharacters[1], 0, 2);
    packet.p2Shot = (u8)std::clamp(g_PlayerShots[1], 0, 1);
    packet.shellKind = (u8)g_ShellKind;
    packet.shellSelectionP1 = g_ShellSelection[0];
    packet.shellSelectionP2 = g_ShellSelection[1];
    packet.shellVoteP1 = g_ShellVote[0];
    packet.shellVoteP2 = g_ShellVote[1];
    packet.shellCommit = (u8)g_ShellCommit;
    packet.shellRequest = g_ShellRequest;
    // Bit 0 is a guest acknowledgement that its one-shot closing commit was
    // consumed.  The host waits for this before publishing shellKind=NONE.
    packet.shellReserved = (u8)((u8)g_ShellConfirmAction << kShellConfirmShift);
    if (!g_Host && g_ShellCloseComplete) packet.shellReserved |= kShellAckDelivered;
    packet.shellRevision = g_ShellRevision;
    packet.shellHandoffFrame = g_ShellHandoffFrame;
    packet.shellSelectionRequest = (i8)std::clamp(g_LocalShellSelectionRequest, -1, 2);
    packet.shellSelectionValid = g_LocalShellSelectionRequest >= 0 ? 1 : 0;
    packet.menuCursor = (i16)std::clamp(g_LocalMenuCursor, -1, 32767);
    packet.menuCursorValid = g_LocalMenuCursor >= 0 ? 1 : 0;
    packet.menuReserved = 0;
    packet.stateHash = g_InputFrame ^ ((u32)g_GameDifficulty << 16) ^ (u32)g_GameStage;
    SDL_strlcpy(packet.name, g_Host ? "TH07 Host" : "TH07 Guest", sizeof(packet.name));
}
bool SendPacket(const sockaddr_in &destination, u8 kind, u16 buttons = 0, u32 frame = 0,
                f32 touchDx = 0.0f, f32 touchDy = 0.0f)
{
    if (g_Socket == kInvalidSocket) return false; Packet packet;
    FillPacket(packet, kind, buttons, frame, touchDx, touchDy);
    return sendto(g_Socket, (const char *)&packet, sizeof(packet), 0,
                  (const sockaddr *)&destination, sizeof(destination)) == (int)sizeof(packet);
}

static i16 QuantizePlayerPosition(f32 value)
{
    return (i16)std::clamp((i32)lroundf(value * 16.0f), -32768, 32767);
}

static void CapturePlayerStatePacket(const Player &player, PlayerStatePacket &state)
{
    state = {};
    state.positionX16 = QuantizePlayerPosition(player.positionCenter.x);
    state.positionY16 = QuantizePlayerPosition(player.positionCenter.y);
    state.respawnTimer = (u16)std::clamp(player.respawnTimer, 0, 65535);
    state.bombTimer = (u16)std::clamp(player.bombInfo.bombTimer.current, 0, 65535);
    state.playerState = (u8)std::clamp((i32)player.playerState, 0, 4);
    state.active = g_PlayerActive[player.initParam] ? 1 : 0;
    state.bombActive = player.bombInfo.isInUse ? 1 : 0;
    state.focus = player.isFocus ? 1 : 0;
    if (g_GameManager.globals)
    {
        state.lives = (u8)std::clamp(GetPlayerLives(player.initParam), 0, 255);
        state.bombs = (u8)std::clamp(GetPlayerBombs(player.initParam), 0, 255);
        state.power = (u8)std::clamp(GetPlayerPower(player.initParam), 0, 128);
    }
}

static void ApplyPlayerStatePacket(const PlayerStatePacket &state, u8 playerId)
{
    if (!g_GameManager.globals || playerId >= 2) return;
    Player &player = g_Players[playerId];
    const bool active = state.active != 0;
    const i8 authoritativeState =
        (i8)std::clamp((i32)state.playerState, 0, 4);
    const bool lifecycleChanged = g_PlayerActive[playerId] != active ||
                                  player.playerState != authoritativeState;
    const bool bombChanged = (player.bombInfo.isInUse != 0) !=
                             (state.bombActive != 0);
    g_PlayerActive[playerId] = active;
    SetPlayerLives(playerId, state.lives);
    SetPlayerBombs(playerId, state.bombs);
    SetPlayerPower(playerId, state.power);

    // Lockstep input owns ordinary movement. Replacing the guest position
    // with a 1/16-pixel snapshot every frame introduces a new rounding error
    // before the next simulation tick and also resets render interpolation,
    // producing the visible split/jitter that these packets were meant to
    // repair. Position and lifecycle timers are corrected only on an actual
    // death/respawn/activation transition.
    if (lifecycleChanged || !active)
    {
        player.positionCenter.x = (f32)state.positionX16 / 16.0f;
        player.positionCenter.y = (f32)state.positionY16 / 16.0f;
        player.positionCenter.z = 0.49f;
        player.prevPositionCenter = player.positionCenter;
        player.respawnTimer = state.respawnTimer;
        player.playerState = authoritativeState;
    }
    if (bombChanged || lifecycleChanged)
    {
        player.isBombing = state.bombActive ? 1 : 0;
        player.bombInfo.isInUse = state.bombActive ? 1 : 0;
        player.bombInfo.bombTimer = state.bombTimer;
    }
    if (!active)
    {
        player.playerState = PLAYER_STATE_DEAD;
        player.bombInfo.isInUse = 0;
        player.isBombing = 0;
    }
}

static void StoreRemoteAuthoritativeState(const Packet &packet)
{
    ApplyRemoteSharedShellState(packet);
    // Authoritative state is only meaningful after the shared shell has
    // reached its terminal NONE state.  A retry/title commit is carried on
    // the same packet as the host's last gameplay snapshot; retaining that
    // snapshot would let an old dead-player state be applied after both peers
    // have already confirmed the new epoch.
    if (packet.shellKind != (u8)Online::SHARED_SHELL_NONE ||
        packet.shellCommit != (u8)Online::SHARED_COMMIT_NONE ||
        g_ShellKind != Online::SHARED_SHELL_NONE ||
        g_ShellCommit != Online::SHARED_COMMIT_NONE)
    {
        g_RemoteAuthoritativeStatePresent = false;
        g_RemoteAuthoritativeShellRevision = 0;
        return;
    }
    if (g_Host || !g_MultiplayerSession || packet.role != 1 ||
        packet.session != g_LaunchNonce || packet.barrierEpoch != g_BarrierEpoch ||
        packet.authoritativeFrame < g_LastAuthoritativeFrame ||
        packet.shellRevision < g_ShellRevision)
    {
        return;
    }
    // A retry/title commit advances the shared shell epoch before either
    // device closes its native menu. Authoritative packets from that closing
    // window can still contain the pre-retry dead-player state, so do not
    // queue them until the agreed handoff frame has been reached.
    if (g_ShellCommit != Online::SHARED_COMMIT_NONE &&
        g_ShellHandoffFrame != 0 &&
        packet.authoritativeFrame < g_ShellHandoffFrame)
    {
        return;
    }
    g_LastAuthoritativeFrame = packet.authoritativeFrame;
    g_RemoteP1State = packet.p1State;
    g_RemoteP2State = packet.p2State;
    g_RemoteAuthoritativeShellRevision = packet.shellRevision;
    g_RemoteAuthoritativeStatePresent = true;
}

static void ApplyPendingAuthoritativeState()
{
    if (g_Host || !g_RemoteAuthoritativeStatePresent || !g_GameManager.notInMenu ||
        g_Supervisor.curState != 2 || g_ShellKind != Online::SHARED_SHELL_NONE ||
        g_ShellCommit != Online::SHARED_COMMIT_NONE)
    {
        return;
    }
    // A future snapshot stays queued until its logical boundary. A snapshot
    // that arrived a few frames late is still useful for discrete resources,
    // death/respawn and bomb transitions. ApplyPlayerStatePacket deliberately
    // leaves ordinary movement untouched, so applying that late state cannot
    // pull a live player backwards.
    if (g_LastAuthoritativeFrame > g_InputFrame) return;
    // A state packet from the previous shell epoch must never overwrite a
    // player that has just been restored by a retry commit.
    if (g_RemoteAuthoritativeShellRevision < g_ShellRevision)
    {
        g_RemoteAuthoritativeStatePresent = false;
        return;
    }
    const bool wasP2Active = g_PlayerActive[1];
    ApplyPlayerStatePacket(g_RemoteP1State, 0);
    ApplyPlayerStatePacket(g_RemoteP2State, 1);
    if (wasP2Active != g_PlayerActive[1])
    {
        MobileDiagnostics::Log("online/state", "authoritative frame=%u p2 active=%d state=%d",
                               g_LastAuthoritativeFrame, g_PlayerActive[1] ? 1 : 0,
                               (int)g_Players[1].playerState);
    }
    g_RemoteAuthoritativeStatePresent = false;
}

static bool SendAuthoritativeStatePacket()
{
    if (!g_Host || !g_MultiplayerSession || g_State != Online::STATE_CONNECTED ||
        !g_GameManager.globals)
    {
        return false;
    }
    Packet packet;
    FillPacket(packet, kAuthoritativeState, 0, g_InputFrame);
    packet.authoritativeFrame = g_InputFrame;
    CapturePlayerStatePacket(g_Players[0], packet.p1State);
    CapturePlayerStatePacket(g_Players[1], packet.p2State);
    if (g_Mode == Online::MODE_BLUETOOTH)
    {
#if defined(TH07_IOS)
        return TH07_IOS_BluetoothSend(&packet, (int)sizeof(packet), 0) != 0;
#else
        return false;
#endif
    }
    if (g_Socket == kInvalidSocket || g_Peer.sin_addr.s_addr == 0) return false;
    return sendto(g_Socket, (const char *)&packet, sizeof(packet), 0,
                  (const sockaddr *)&g_Peer, sizeof(g_Peer)) == (int)sizeof(packet);
}

u32 ComputeSynchronizationHash(u32 frame)
{
    // A device can still be loading the stage while the lockstep input
    // history is already advancing. Do not compare a partially initialized
    // menu/loading state against a live gameplay state.
    if (!g_GameManager.globals || !g_GameManager.notInMenu || g_Supervisor.curState != 2)
        return 0;

    u32 hash = 2166136261u;
    const auto mix = [&hash](u32 value)
    {
        hash ^= value;
        hash *= 16777619u;
    };
    const auto mixGameplayFloat = [&mix](f32 value)
    {
        // Keep the check useful across iOS releases where a few simulation
        // operations can differ by a fraction of a pixel.
        if (!std::isfinite(value)) { mix(0xffffffffu); return; }
        mix((u32)(i32)lroundf(value * 16.0f));
    };
    mix(frame);
    mix(g_Rng.seed);
    mix(g_Rng.generationCount);
    mix((u32)(g_GameReadySent ? g_GameDifficulty : g_Supervisor.cfg.defaultDifficulty));
    mix((u32)(g_GameReadySent ? g_GameStage : 0));
    if (g_GameManager.globals && g_GameManager.notInMenu && g_Supervisor.curState == 2)
    {
        mix((u32)g_GameManager.framesThisStage);
        mix((u32)g_GameManager.cherry);
        mix((u32)g_GameManager.cherryPlus);
        mix((u32)g_GameManager.rank.rank);
        mix(g_GameManager.globals->score);
        mix((u32)g_GameManager.globals->grazeInTotal);
        for (u32 playerId = 0; playerId < 2; ++playerId)
        {
            mix((u32)g_PlayerCharacters[playerId]);
            mix((u32)g_PlayerShots[playerId]);
            mix((u32)GetPlayerLives((u8)playerId));
            mix((u32)GetPlayerBombs((u8)playerId));
            mix((u32)GetPlayerPower((u8)playerId));
            mix(g_PlayerActive[playerId] ? 1u : 0u);
            if (!g_PlayerActive[playerId]) continue;
            const Player &player = g_Players[playerId];
            mixGameplayFloat(player.positionCenter.x);
            mixGameplayFloat(player.positionCenter.y);
            mix((u32)(u8)player.playerState);
            mix((u32)player.respawnTimer);
            mix((u32)player.bombInfo.isInUse);
            mix((u32)player.bombInfo.bombTimer.current);
            mix((u32)(u8)player.isFocus);
            for (const PlayerBullet &bullet : player.bullets)
            {
                if (!bullet.bulletState) continue;
                mix((u32)(u16)bullet.bulletState);
                mixGameplayFloat(bullet.pos.x);
                mixGameplayFloat(bullet.pos.y);
                mix((u32)(u16)bullet.damage);
            }
        }
    }
    return hash;
}

bool SendStoredInput(u32 frame, InputFrame &input)
{
    Packet packet;
    FillPacket(packet, kInput, input.buttons, frame, input.touchDx, input.touchDy);
    packet.stateHash = input.stateHash;
    // Serialize the cursor captured with this frame. FillPacket normally
    // reads the current pending cursor, which may belong to a later frame or
    // may already have been cleared after the first send. Retransmits must
    // preserve the original one-shot menu operation.
    packet.menuCursor = input.menuCursor;
    packet.menuCursorValid = input.menuCursor >= 0 ? 1 : 0;
    // Shared pause/retry selections are also frame-scoped.  Reconstructing
    // them from the live queue here would lose the choice on retransmit.
    packet.shellSelectionRequest = input.shellSelectionRequest;
    packet.shellSelectionValid = input.shellSelectionValid ? 1 : 0;
    bool sent = false;
    if (g_Mode == Online::MODE_BLUETOOTH)
    {
#if defined(TH07_IOS)
        sent = TH07_IOS_BluetoothSend(&packet, (int)sizeof(packet), 0) != 0;
#endif
    }
    else if (g_Socket != kInvalidSocket)
    {
        sent = sendto(g_Socket, (const char *)&packet, sizeof(packet), 0,
                      (const sockaddr *)&g_Peer, sizeof(g_Peer)) == (int)sizeof(packet);
    }
    if (sent && input.menuCursor >= 0 && input.frame == g_InputFrame + (u32)g_InputDelay)
        g_LocalMenuCursor = -1;
    if (sent && input.frame == g_InputFrame + (u32)g_InputDelay)
        g_LocalShellSelectionRequest = -1;
    return sent;
}
bool SendText(const sockaddr_in &destination, const char *text)
{
    if (g_Socket == kInvalidSocket || !text) return false;
    const int length = (int)strlen(text);
    return sendto(g_Socket, text, length, 0, (const sockaddr *)&destination,
                  sizeof(destination)) == length;
}
void SendRelayRegister()
{
    if (g_Mode != Online::MODE_RELAY || !g_RelayRoom[0] || g_Peer.sin_addr.s_addr == 0) return;
    char text[256] = {};
    SDL_snprintf(text, sizeof(text), "THR1 REGISTER %s %s %u th07-%08x",
                 g_RelayRoom, g_Host ? "host" : "guest", (u32)kVersion, g_Session);
    SendText(g_Peer, text); g_LastRelayRegister = SDL_GetTicks();
    SetStatus(g_Host ? "Waiting relay guest..." : "Waiting relay host...");
}
#if defined(TH07_IOS)
bool SendBluetooth(u8 kind, u16 buttons = 0, u32 frame = 0,
                   f32 touchDx = 0.0f, f32 touchDy = 0.0f)
{
    Packet packet; FillPacket(packet, kind, buttons, frame, touchDx, touchDy);
    return TH07_IOS_BluetoothSend(&packet, (int)sizeof(packet), kind != kInput) != 0;
}
#endif
bool Valid(const Packet &packet)
{
    return packet.magic == kMagic && packet.version == kVersion &&
           packet.buildIdentity == kBuildIdentity && packet.resourceIdentity == kResourceIdentity;
}
bool IsExpectedPeer(const sockaddr_in &from);
void FailStartup(const char *reason);
void LogIncompatiblePeer(const sockaddr_in &from, const Packet &packet)
{
    char address[64] = {};
    inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address));
    MobileDiagnostics::Log(
        "online/handshake",
        "reject peer=%s:%u remoteVersion=%u remoteBuild=%08x remoteResource=%08x "
        "localVersion=%u localBuild=%08x localResource=%08x",
        address, (unsigned)ntohs(from.sin_port), (unsigned)packet.version,
        (unsigned)packet.buildIdentity, (unsigned)packet.resourceIdentity,
        (unsigned)kVersion, (unsigned)kBuildIdentity, (unsigned)kResourceIdentity);
}
void HandleIncompatiblePeer(const sockaddr_in &from, const Packet &packet)
{
    LogIncompatiblePeer(from, packet);
    if (g_Host)
    {
        // Reply with a packet in the current format. The guest can then reset
        // its stale peer and resume discovery without accepting mixed builds.
        if (g_State == Online::STATE_HOSTING || IsExpectedPeer(from))
        {
            SendPacket(from, kIncompatible);
            SetStatus("Peer incompatible; install the same IPA");
        }
        return;
    }

    const bool expected = g_Peer.sin_addr.s_addr == 0 || IsExpectedPeer(from) ||
                          from.sin_addr.s_addr == g_Peer.sin_addr.s_addr;
    if (!expected) return;
    if (g_MultiplayerSession) FailStartup("Peer incompatible; install the same IPA");
    g_MultiplayerSession = false;
    g_LocalSession = false;
    g_InputSynchronizationActive = false;
    g_StartupPhase = STARTUP_NONE;
    g_Peer = {};
    g_PeerAddress[0] = '\0';
    // Let the actionable status remain visible until the next discovery
    // interval, then resume the normal LAN search loop.
    const u32 retryAt = SDL_GetTicks();
    g_LastDiscovery = retryAt;
    g_LastHandshake = retryAt;
    g_LastPeer = retryAt;
    g_State = Online::STATE_SEARCHING;
    SetStatus(g_Mode == Online::MODE_NEARBY_LAN
                  ? "Incompatible peer; searching compatible room..."
                  : "Peer incompatible; install the same IPA");
}
void ApplyStartPacket(const Packet &packet)
{
    g_StartSeed = (u16)packet.frame;
    g_Rng.SetSeed(g_StartSeed);
    g_Rng.seedBackup = g_StartSeed;
    g_Rng.generationCount = 0;
    g_Supervisor.cfg.lifeCount = packet.lifeCount;
    g_Supervisor.cfg.bombCount = packet.bombCount;
    g_Supervisor.cfg.slowMode = packet.slowMode;
    g_Supervisor.cfg.defaultDifficulty = (u8)std::clamp((i32)packet.difficulty, 0, 5);
    g_InputDelay = std::clamp((i32)packet.reserved2, 0, 8);
}

void ResetSessionRng()
{
    g_Rng.SetSeed(g_StartSeed);
    g_Rng.seedBackup = g_StartSeed;
    g_Rng.generationCount = 0;
}

bool CommitMenuBarrier(u32 epoch)
{
    if (g_StartupPhase == STARTUP_MENU_COMMITTED ||
        g_StartupPhase == STARTUP_GAME_WAIT || g_StartupPhase == STARTUP_GAME_COMMITTED)
        return false;
    if (epoch <= g_BarrierEpoch) return false;
    g_BarrierEpoch = epoch;
    g_StartupPhase = STARTUP_MENU_COMMITTED;
    g_InputSynchronizationActive = true;
    g_StartupStartedAt = SDL_GetTicks();
    ResetSessionRng();
    ResetSyncState();
    // A menu click is attached to one lockstep frame. Never carry the
    // one-shot cursor over a barrier reset: replaying it on the next page can
    // consume a stale confirm on the peer.
    g_LocalMenuCursor = g_RemoteMenuCursor = g_MenuCursorTarget = -1;
    return true;
}

bool CommitGameplayBarrier(u32 epoch)
{
    if (g_StartupPhase == STARTUP_GAME_COMMITTED) return false;
    if (epoch <= g_BarrierEpoch) return false;
    g_BarrierEpoch = epoch;
    g_StartupPhase = STARTUP_GAME_COMMITTED;
    g_GameplayCommit = true;
    g_StartupStartedAt = SDL_GetTicks();
    ResetSyncState();
    g_LocalMenuCursor = g_RemoteMenuCursor = g_MenuCursorTarget = -1;
    return true;
}

bool SendStartupPacket(u8 kind)
{
    Packet packet;
    FillPacket(packet, kind, 0, g_StartSeed);
    packet.session = g_LaunchNonce;
    packet.difficulty = (u16)std::max(0, g_GameDifficulty);
    packet.stage = (u8)std::max(0, g_GameStage);
    packet.menuState = (u8)std::max(0, g_MenuReadyState);
    packet.p1Character = (u8)std::clamp(g_PlayerCharacters[0], 0, 2);
    packet.p1Shot = (u8)std::clamp(g_PlayerShots[0], 0, 1);
    packet.p2Character = (u8)std::clamp(g_PlayerCharacters[1], 0, 2);
    packet.p2Shot = (u8)std::clamp(g_PlayerShots[1], 0, 1);
    if (kind == kPrepare) packet.difficulty = (u16)std::clamp((i32)g_Supervisor.cfg.defaultDifficulty, 0, 5);
    if (kind == kMenuReady || kind == kMenuCommit) packet.buttons = (u16)std::max(0, g_MenuReadyCursor);
    if (g_Mode == Online::MODE_BLUETOOTH)
    {
#if defined(TH07_IOS)
        return TH07_IOS_BluetoothSend(&packet, (int)sizeof(packet), 1) != 0;
#else
        return false;
#endif
    }
    if (g_Socket == kInvalidSocket || g_Peer.sin_addr.s_addr == 0) return false;
    return sendto(g_Socket, (const char *)&packet, sizeof(packet), 0,
                  (const sockaddr *)&g_Peer, sizeof(g_Peer)) == (int)sizeof(packet);
}

void FailStartup(const char *reason)
{
    const StartupPhase failedPhase = g_StartupPhase;
    g_StartupPhase = STARTUP_FAILED;
    g_InputSynchronizationActive = false;
    g_MultiplayerSession = false;
    g_GameplayCommit = false;
    g_ShellKind = Online::SHARED_SHELL_NONE;
    g_ShellCommit = Online::SHARED_COMMIT_NONE;
    g_ShellConfirmAction = Online::SHARED_CONFIRM_NONE;
    g_ShellHandoffFrame = 0;
    g_ShellCommitRevision = 0;
    g_ShellCommitAcked = false;
    g_ShellHandoffReady = false;
    g_ShellCommitDelivered = false;
    g_ShellCloseComplete = false;
    g_RemoteAuthoritativeStatePresent = false;
    g_RemoteAuthoritativeShellRevision = 0;
    g_LastAuthoritativeSendFrame = 0xffffffffu;
    g_GameManager.isInPauseMenu = 0;
    g_GameManager.isInRetryMenu = 0;
    g_GameManager.isPaused = 0;
    SetStatus(reason ? reason : "Online startup failed");
    TH07_IOS_ShowOnlineError(reason ? reason : "Online startup failed");
    MobileDiagnostics::Log("online/startup", "failed phase=%d reason=%s", (int)failedPhase,
                           reason ? reason : "unknown");
}

void HandleStartupPacket(const Packet &packet)
{
    if (packet.buildIdentity != kBuildIdentity || packet.resourceIdentity != kResourceIdentity)
    {
        FailStartup("Peer build or protocol mismatch");
        return;
    }
    if (packet.kind != kPrepare && packet.session != g_LaunchNonce) return;
    // Once the host commits a barrier, the guest may still be retransmitting
    // its READY packet with the immediately previous epoch. Accept that one
    // recovery case so the committed host can resend the matching COMMIT.
    // Older epochs and all ordinary startup packets remain rejected.
    const bool committedReadyRecovery = g_Host &&
        OnlineIsImmediatelyPreviousEpoch(packet.barrierEpoch, g_BarrierEpoch) &&
        ((packet.kind == kMenuReady && g_StartupPhase == STARTUP_MENU_COMMITTED) ||
         (packet.kind == kGameReady && g_StartupPhase == STARTUP_GAME_COMMITTED));
    if (packet.kind != kPrepare && packet.kind != kMenuCommit &&
        packet.kind != kGameCommit && packet.barrierEpoch != g_BarrierEpoch &&
        !committedReadyRecovery)
        return;
    if (packet.kind == kPrepare && !g_Host)
    {
        if (g_MultiplayerSession && packet.session == g_LaunchNonce &&
            packet.barrierEpoch == g_BarrierEpoch &&
            g_StartupPhase != STARTUP_FAILED)
        {
            SendStartupPacket(kPrepareAck);
            return;
        }
        g_LaunchNonce = packet.session;
        g_StartSeed = (u16)packet.frame;
        ApplyStartPacket(packet);
        ActivateMultiplayerSession(false);
        g_BarrierEpoch = packet.barrierEpoch;
        g_StartupPhase = STARTUP_MENU_WAIT;
        g_StartGameRequested = true;
        g_MenuOpen = true;
        SendStartupPacket(kPrepareAck);
        SetStatus("Peer ready; opening game menu...");
        MobileDiagnostics::Log("online/startup", "prepare accepted nonce=%u seed=%u", g_LaunchNonce,
                               (u32)g_StartSeed);
    }
    else if (packet.kind == kPrepareAck && g_Host &&
             packet.session == g_LaunchNonce && g_StartupPhase == STARTUP_PREPARING)
    {
        g_StartupPhase = STARTUP_MENU_WAIT;
        g_StartupStartedAt = SDL_GetTicks();
        SetStatus("Both devices prepared; choose difficulty");
    }
    else if (packet.kind == kMenuReady && packet.session == g_LaunchNonce)
    {
        if (g_Host && g_StartupPhase == STARTUP_MENU_COMMITTED &&
            OnlineIsImmediatelyPreviousEpoch(packet.barrierEpoch, g_BarrierEpoch))
        {
            SendStartupPacket(kMenuCommit);
            MobileDiagnostics::Log("online/startup",
                "resent menu commit epoch=%u for peer epoch=%u",
                g_BarrierEpoch, packet.barrierEpoch);
            return;
        }
        g_PeerMenuReady = true;
        g_PeerMenuState = packet.menuState;
        g_PeerMenuCursor = packet.buttons;
        if (g_Host && g_MenuReadySent && g_MenuReadyState == g_PeerMenuState &&
            g_StartupPhase != STARTUP_MENU_COMMITTED &&
            g_StartupPhase != STARTUP_GAME_WAIT && g_StartupPhase != STARTUP_GAME_COMMITTED)
        {
            if (CommitMenuBarrier(g_BarrierEpoch + 1))
            {
                SendStartupPacket(kMenuCommit);
                SetStatus("Menu synchronized");
            }
        }
        else if (g_Host && g_MenuReadySent && g_MenuReadyState != g_PeerMenuState)
        {
            FailStartup("Devices reached different menu state");
        }
    }
    else if (packet.kind == kMenuCommit && !g_Host && packet.session == g_LaunchNonce &&
             packet.menuState == (u8)std::max(0, g_MenuReadyState))
    {
        if (packet.barrierEpoch == g_BarrierEpoch)
        {
            SetStatus("Menu synchronized");
        }
        else if (packet.barrierEpoch == g_BarrierEpoch + 1 &&
                 g_StartupPhase != STARTUP_GAME_WAIT &&
                 g_StartupPhase != STARTUP_GAME_COMMITTED)
        {
            if (CommitMenuBarrier(packet.barrierEpoch))
            {
                SetStatus("Menu synchronized");
            }
        }
    }
    else if (packet.kind == kGameReady && g_Host && packet.session == g_LaunchNonce)
    {
        if (g_StartupPhase == STARTUP_GAME_COMMITTED)
        {
            SendStartupPacket(kGameCommit);
            return;
        }
        g_PeerGameReady = true;
        g_PeerGameDifficulty = packet.difficulty;
        g_PeerGameStage = packet.stage;
        g_PeerCharacters[0] = packet.p1Character;
        g_PeerShots[0] = packet.p1Shot;
        g_PeerCharacters[1] = packet.p2Character;
        g_PeerShots[1] = packet.p2Shot;
        if (g_GameReadySent && packet.difficulty == (u16)g_GameDifficulty &&
            packet.stage == (u8)g_GameStage && packet.p1Character == (u8)g_PlayerCharacters[0] &&
            packet.p1Shot == (u8)g_PlayerShots[0] && packet.p2Character == (u8)g_PlayerCharacters[1] &&
            packet.p2Shot == (u8)g_PlayerShots[1])
        {
            CommitGameplayBarrier(g_BarrierEpoch + 1);
            SendStartupPacket(kGameCommit);
            SetStatus("Game configuration synchronized");
        }
        else if (g_GameReadySent)
        {
            FailStartup("Game settings differ between devices");
        }
    }
    else if (packet.kind == kGameCommit && !g_Host && packet.session == g_LaunchNonce)
    {
        if (packet.difficulty != (u16)g_GameDifficulty || packet.stage != (u8)g_GameStage ||
            packet.p1Character != (u8)g_PlayerCharacters[0] ||
            packet.p1Shot != (u8)g_PlayerShots[0] ||
            packet.p2Character != (u8)g_PlayerCharacters[1] ||
            packet.p2Shot != (u8)g_PlayerShots[1])
        {
            FailStartup("Game settings differ between devices");
            return;
        }
        if (packet.barrierEpoch == g_BarrierEpoch)
        {
            SetStatus("Starting synchronized game...");
        }
        else if (packet.barrierEpoch == g_BarrierEpoch + 1 &&
                 CommitGameplayBarrier(packet.barrierEpoch))
        {
            SetStatus("Starting synchronized game...");
        }
    }
}

void SetPeer(const sockaddr_in &from)
{
    g_Peer = from; g_LastPeer = SDL_GetTicks(); inet_ntop(AF_INET, &from.sin_addr, g_PeerAddress, sizeof(g_PeerAddress));
}
bool IsExpectedPeer(const sockaddr_in &from)
{
    return g_Peer.sin_addr.s_addr != 0 && from.sin_addr.s_addr == g_Peer.sin_addr.s_addr &&
           from.sin_port == g_Peer.sin_port;
}

void ProcessNativeLauncherActions()
{
#if defined(TH07_IOS)
    int action = 0, mode = 0, delay = g_InputDelay;
    char direct[64] = {}, endpoint[128] = {}, room[64] = {};
    while (TH07_IOS_PollOnlineLauncherAction(&action, &mode, &delay, direct, sizeof(direct),
                                              endpoint, sizeof(endpoint), room, sizeof(room)))
    {
        if (action == TH07_ONLINE_ACTION_SET_MODE)
        {
            Online::SetMode((Online::Mode)std::clamp(mode, 0, 3));
        }
        else if (action == TH07_ONLINE_ACTION_SET_DELAY)
        {
            Online::SetInputDelay(delay);
            SetStatus("Input delay updated");
        }
        else if (action == TH07_ONLINE_ACTION_HOST || action == TH07_ONLINE_ACTION_GUEST)
        {
            const bool hostRole = action == TH07_ONLINE_ACTION_HOST;
            if (mode == Online::MODE_NEARBY_LAN)
                hostRole ? Online::StartHost() : Online::StartSearch();
            else if (mode == Online::MODE_DIRECT)
            {
                if (hostRole)
                {
                    ApplyTransportInputDelay(Online::MODE_DIRECT);
                    if (!OpenSocket()) SetStatus("Unable to open UDP port 37707");
                    else
                    {
                        g_Mode = Online::MODE_DIRECT;
                        g_Host = true;
                        g_Session = SDL_GetTicks() ^ 0x07a7c0deu;
                        g_State = Online::STATE_HOSTING;
                        g_LastPeer = SDL_GetTicks();
                        SetStatus("Waiting for direct guest on port 37707");
                    }
                }
                else Online::StartDirect(direct, kPort);
            }
            else if (mode == Online::MODE_RELAY)
                Online::StartRelay(endpoint, room, hostRole);
            else
                hostRole ? Online::StartBluetoothHost() : Online::StartBluetoothGuest();
        }
        else if (action == TH07_ONLINE_ACTION_LEAVE)
        {
            Online::Leave();
        }
        else if (action == TH07_ONLINE_ACTION_START_GAME)
        {
            Online::RequestStartGame();
        }
        else if (action == TH07_ONLINE_ACTION_START_LOCAL)
        {
            Online::StartLocalGame();
        }
        else if (action == TH07_ONLINE_ACTION_CLOSE)
        {
            Online::CloseMenu();
        }
    }
#endif
}

void RefreshNativeLauncher()
{
    if (!g_MenuOpen) return;
    TH07_IOS_UpdateOnlineLauncher((int)g_Mode, (int)g_State, g_Host ? 1 : 0,
                                  g_State == Online::STATE_CONNECTED && g_Host &&
                                          !g_MultiplayerSession ? 1 : 0,
                                  g_InputDelay, g_LastRtt, g_Status, g_PeerAddress,
                                  g_DirectAddress, g_RelayEndpoint, g_RelayRoom);
}

void UpdateStartupReliability(u32 now)
{
    if (!g_MultiplayerSession || g_LocalSession || g_StartupPhase == STARTUP_FAILED) return;
    if (now - g_LastStartupSend >= kHandshakeIntervalMs)
    {
        if (g_StartupPhase == STARTUP_PREPARING && g_Host)
            SendStartupPacket(kPrepare);
        else if ((g_StartupPhase == STARTUP_PREPARING || g_StartupPhase == STARTUP_MENU_WAIT) &&
                 g_MenuReadySent)
            SendStartupPacket(kMenuReady);
        else if (g_StartupPhase == STARTUP_MENU_COMMITTED && g_Host &&
                 g_LastRemoteFrame == 0xffffffffu)
            SendStartupPacket(kMenuCommit);
        else if (g_StartupPhase == STARTUP_GAME_WAIT && g_GameReadySent)
            SendStartupPacket(kGameReady);
        else if (g_StartupPhase == STARTUP_GAME_COMMITTED && g_Host &&
                 g_LastRemoteFrame == 0xffffffffu)
            SendStartupPacket(kGameCommit);
        g_LastStartupSend = now;
    }
    const bool waitingForProtocol = g_StartupPhase == STARTUP_PREPARING ||
        (g_StartupPhase == STARTUP_MENU_WAIT && g_MenuReadySent) ||
        (g_StartupPhase == STARTUP_GAME_WAIT && g_GameReadySent);
    if (waitingForProtocol && now - g_StartupStartedAt > 15000)
        FailStartup("Online synchronization timed out");
}
void AcceptPeer(const sockaddr_in &from)
{
    SetPeer(from);
    if (g_State != Online::STATE_CONNECTED) { g_State = Online::STATE_CONNECTED; SetStatus(g_Host ? "Connected (host)" : "Connected (guest)"); MobileDiagnostics::Log("online", "connected peer=%s", g_PeerAddress); }
}
void SendDiscovery()
{
    sockaddr_in target = {}; target.sin_family = AF_INET; target.sin_port = htons(kPort); target.sin_addr.s_addr = htonl(INADDR_BROADCAST); SendPacket(target, kHello);
#if !defined(_WIN32)
    ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0)
    {
        for (ifaddrs *entry = interfaces; entry; entry = entry->ifa_next)
        {
            if (!entry->ifa_addr || !entry->ifa_broadaddr || entry->ifa_addr->sa_family != AF_INET ||
                !(entry->ifa_flags & IFF_UP) || !(entry->ifa_flags & IFF_BROADCAST) || (entry->ifa_flags & IFF_LOOPBACK)) continue;
            target.sin_addr = ((const sockaddr_in *)entry->ifa_broadaddr)->sin_addr; SendPacket(target, kHello);
        }
        freeifaddrs(interfaces);
    }
#endif
}
#if defined(TH07_IOS)
void UpdateBluetooth()
{
    if (g_State == Online::STATE_HOSTING || g_State == Online::STATE_SEARCHING)
    {
        if (TH07_IOS_BluetoothIsConnected())
        {
            if (g_State != Online::STATE_CONNECTED) g_State = Online::STATE_CONNECTED;
            g_BluetoothDisconnectedAt = 0;
            g_LastPeer = SDL_GetTicks();
            SetStatus(g_Host ? "Connected (Bluetooth host)" : "Connected (Bluetooth guest)");
        }
        else if (const char *status = TH07_IOS_BluetoothStatus()) if (*status) SetStatus(status);
    }
    else if (g_State == Online::STATE_CONNECTED && !TH07_IOS_BluetoothIsConnected())
    {
        const u32 now = SDL_GetTicks();
        if (!g_BluetoothDisconnectedAt) g_BluetoothDisconnectedAt = now;
        if (now - g_BluetoothDisconnectedAt >= kBluetoothReconnectGraceMs)
        {
            if (g_MultiplayerSession) FailStartup("Nearby peer disconnected");
            g_State = Online::STATE_IDLE;
        }
        else
        {
            SetStatus("Nearby transport reconnecting...");
        }
        return;
    }
    Packet packet = {};
    while (TH07_IOS_BluetoothPoll(&packet, sizeof(packet)) == (int)sizeof(packet))
    {
        if (!Valid(packet))
        {
            if (packet.magic == kMagic) SetStatus("Incompatible nearby build or resources");
            continue;
        }
        const u32 now = SDL_GetTicks();
        if (packet.kind == kHello && g_Host) { g_Session = packet.session; SendBluetooth(kWelcome); }
        else if (packet.kind == kWelcome && !g_Host) { g_State = Online::STATE_CONNECTED; SetStatus("Connected (Bluetooth guest)"); SendBluetooth(kAck); }
        else if (packet.kind == kAuthoritativeState && g_State == Online::STATE_CONNECTED)
        {
            StoreRemoteAuthoritativeState(packet);
            g_LastPeer = now;
        }
        else if (packet.kind == kInput && g_State == Online::STATE_CONNECTED)
        {
            StoreRemoteInput(packet); g_LastPeer = now;
        }
        else if (packet.kind == kHeartbeat)
        {
            StoreLocalAcknowledgement(packet);
            SendBluetooth(kAck, 0, packet.timestamp);
        }
        else if (packet.kind == kAck)
        {
            StoreLocalAcknowledgement(packet);
            g_LastPeer = now;
            if (packet.frame && now >= packet.frame) g_LastRtt = std::min(now - packet.frame, 999u);
        }
        else if (packet.kind >= kPrepare && packet.kind <= kGameCommit &&
                 g_State == Online::STATE_CONNECTED)
        {
            g_LastPeer = now;
            HandleStartupPacket(packet);
            if (packet.kind == kPrepareAck || packet.kind == kMenuReady ||
                packet.kind == kGameReady)
                SendBluetooth(kAck);
        }
    }
    const u32 now = SDL_GetTicks();
    if (g_State == Online::STATE_CONNECTED && now - g_LastHeartbeat >= kHeartbeatIntervalMs)
    {
        SendBluetooth(kHeartbeat);
        g_LastHeartbeat = now;
    }
    UpdateStartupReliability(now);
}
#endif
} // namespace

void Online::Initialize()
{
    CloseSocket(); g_State = STATE_IDLE; g_Mode = MODE_NEARBY_LAN;
    g_MenuOpen = false; g_MultiplayerSession = false; g_LocalSession = false;
    g_InputSynchronizationActive = false; g_StartupPhase = STARTUP_NONE;
    g_LaunchNonce = 0; g_BarrierEpoch = 0; g_GameplayCommit = false;
    g_InputDelay = kDefaultInputDelay; g_InputDelayUserSet = false;
    ApplyTransportInputDelay(MODE_NEARBY_LAN);
    g_Session = SDL_GetTicks() ^ 0x07a7c0deu; ResetSyncState(); SetStatus("Offline");
}
void Online::Shutdown() { Leave(); TH07_IOS_StopLocalNetworkPermissionProbe();
#if defined(_WIN32)
    WSACleanup();
#endif
}
void Online::Update()
{
    ProcessNativeLauncherActions();
    RefreshNativeLauncher();
    int textField = 0; char textValue[128] = {};
    if (TH07_IOS_PollOnlineText(&textField, textValue, sizeof(textValue)))
    {
        if (textField == 1) SDL_strlcpy(g_DirectAddress, textValue, sizeof(g_DirectAddress));
        else if (textField == 2) SDL_strlcpy(g_RelayEndpoint, textValue, sizeof(g_RelayEndpoint));
        else if (textField == 3) SDL_strlcpy(g_RelayRoom, textValue, sizeof(g_RelayRoom));
        SetStatus("Setting updated");
    }
    if (g_Mode == MODE_BLUETOOTH)
    {
#if defined(TH07_IOS)
        UpdateBluetooth();
#endif
        RefreshNativeLauncher();
        return;
    }
    if (g_Socket == kInvalidSocket) return; Packet packet = {}; sockaddr_in from = {};
    for (;;)
    {
#if defined(_WIN32)
        int fromLength = sizeof(from);
#else
        socklen_t fromLength = sizeof(from);
#endif
        const int received = (int)recvfrom(g_Socket, (char *)&packet, sizeof(packet), 0, (sockaddr *)&from, &fromLength);
        if (received < 0) { if (!WouldBlock()) SetStatus("Network receive error"); break; }
        if (received >= 5 && memcmp(&packet, "THR1", 4) == 0)
        {
            char control[sizeof(Packet) + 1] = {};
            memcpy(control, &packet, (size_t)std::min(received, (int)sizeof(Packet)));
            if (strstr(control, " READY") || strstr(control, " REGISTERED"))
            {
                SetStatus(strstr(control, " READY") ? "Relay peer ready" :
                          (g_Host ? "Waiting relay guest..." : "Waiting relay host..."));
                if (!g_Host) SendPacket(g_Peer, kHello);
            }
            else if (strstr(control, " VERSION_MISMATCH")) SetStatus("Relay version mismatch");
            else if (strstr(control, " REGISTER_FAILED")) SetStatus("Relay registration failed");
            continue;
        }
        if (received != (int)sizeof(Packet)) continue;
        if (packet.magic == kMagic && packet.kind == kIncompatible)
        {
            HandleIncompatiblePeer(from, packet);
            continue;
        }
        if (!Valid(packet))
        {
            if (packet.magic == kMagic) HandleIncompatiblePeer(from, packet);
            continue;
        }
        const u32 now = SDL_GetTicks();
        if (packet.kind == kHello && g_Host &&
            (g_State == STATE_HOSTING || IsExpectedPeer(from)))
        {
            g_Session = packet.session; AcceptPeer(from); SendPacket(from, kWelcome);
        }
        else if (packet.kind == kWelcome && !g_Host &&
                 (g_State == STATE_SEARCHING || (g_State == STATE_CONNECTED && IsExpectedPeer(from))))
        {
            AcceptPeer(from); SendPacket(from, kAck);
        }
        else if (packet.kind == kAck && g_State == STATE_CONNECTED && IsExpectedPeer(from))
        {
            StoreLocalAcknowledgement(packet);
            g_LastPeer = now;
            if (packet.frame && now >= packet.frame) g_LastRtt = std::min(now - packet.frame, 999u);
            if (g_Host) AcceptPeer(from);
        }
        else if (packet.kind == kAuthoritativeState && g_State == STATE_CONNECTED &&
                 IsExpectedPeer(from))
        {
            StoreRemoteAuthoritativeState(packet);
            g_LastPeer = now;
        }
        else if (packet.kind == kInput && g_State == STATE_CONNECTED && IsExpectedPeer(from))
        {
            StoreRemoteInput(packet); g_LastPeer = now;
        }
        else if (packet.kind == kHeartbeat && g_State == STATE_CONNECTED && IsExpectedPeer(from))
        {
            StoreLocalAcknowledgement(packet);
            g_LastPeer = now;
            SendPacket(from, kAck, 0, packet.timestamp);
        }
        else if (packet.kind >= kPrepare && packet.kind <= kGameCommit &&
                 g_State == STATE_CONNECTED && IsExpectedPeer(from))
        {
            g_LastPeer = now;
            HandleStartupPacket(packet);
            if (packet.kind == kPrepareAck || packet.kind == kMenuReady ||
                packet.kind == kGameReady)
                SendPacket(from, kAck);
        }
        else if (packet.kind == kBye && IsExpectedPeer(from))
        {
            if (g_MultiplayerSession) FailStartup("Peer left synchronized play");
            g_State = STATE_SEARCHING;
            SetStatus("Peer disconnected; searching...");
        }
    }
    const u32 now = SDL_GetTicks();
    if (g_State == STATE_SEARCHING && !g_Host && g_Mode == MODE_NEARBY_LAN)
    {
        char bonjourHost[64] = {}; int bonjourPort = 0;
        if (TH07_IOS_PollBonjourHost(bonjourHost, sizeof(bonjourHost), &bonjourPort))
        {
            MobileDiagnostics::Log("online", "Bonjour found host=%s port=%d", bonjourHost, bonjourPort);
            StartDirect(bonjourHost, (u16)bonjourPort);
            g_Mode = MODE_NEARBY_LAN;
            return;
        }
        const int permission = TH07_IOS_GetLocalNetworkPermissionState();
        if (permission == -1) { SetStatus("Local network permission denied"); return; }
        if (permission != 2) { SetStatus("Waiting for local network permission..."); return; }
    }
    if (g_State == STATE_SEARCHING && !g_Host && g_Mode == MODE_NEARBY_LAN &&
        now - g_LastDiscovery >= kDiscoveryIntervalMs)
    {
        SendDiscovery(); g_LastDiscovery = now; g_LastHandshake = now;
        SetStatus("Searching nearby rooms...");
    }
    if (g_State == STATE_SEARCHING && !g_Host && g_Peer.sin_addr.s_addr != 0 && now - g_LastHandshake >= kHandshakeIntervalMs) { SendPacket(g_Peer, kHello); g_LastHandshake = now; }
    if (g_State == STATE_CONNECTED && now - g_LastHeartbeat >= kHeartbeatIntervalMs) { SendPacket(g_Peer, kHeartbeat); g_LastHeartbeat = now; }
    UpdateStartupReliability(now);
    if (g_Mode == MODE_RELAY && g_State != STATE_CONNECTED && now - g_LastRelayRegister >= 1000)
        SendRelayRegister();
    if (g_State == STATE_CONNECTED && now - g_LastPeer > kPeerTimeoutMs)
    {
        if (g_MultiplayerSession)
        {
            FailStartup("Connection lost; synchronized play stopped");
            g_State = STATE_IDLE;
        }
        else
        {
            g_State = STATE_SEARCHING;
            SetStatus("Connection lost; searching...");
        }
    }
    RefreshNativeLauncher();
}
void Online::OpenMenu()
{
    g_MenuOpen = true;
    TH07_IOS_TriggerLocalNetworkPermission();
    if (g_Mode != MODE_BLUETOOTH && g_Socket == kInvalidSocket && !OpenSocket())
        SetStatus("Network unavailable");
    TH07_IOS_PresentOnlineLauncher();
}
void Online::CloseMenu()
{
    g_MenuOpen = false;
    TH07_IOS_DismissOnlineLauncher();
}
bool Online::IsMenuOpen() { return g_MenuOpen; }
void Online::SetMode(Mode mode)
{
    if (g_Mode == mode) return;
    Leave();
    g_Mode = mode;
    g_InputDelayUserSet = false;
    ApplyTransportInputDelay(mode);
    SetStatus(mode == MODE_BLUETOOTH ? "Bluetooth nearby" : "Offline");
}
Online::Mode Online::GetMode() { return g_Mode; }
void Online::SetInputDelay(int frames)
{
    g_InputDelay = std::clamp(frames, 0, 8);
    g_InputDelayUserSet = true;
}
int Online::GetInputDelay() { return g_InputDelay; }
u16 Online::GetPort() { return kPort; }
const char *Online::GetRelayEndpoint() { return g_RelayEndpoint; }
const char *Online::GetRelayRoom() { return g_RelayRoom; }
bool Online::IsBluetooth() { return g_Mode == MODE_BLUETOOTH; }
void Online::RequestStartGame()
{
    if (g_State != STATE_CONNECTED) { SetStatus("Connect a peer before starting"); return; }
    if (!g_Host) { SetStatus("Waiting for host to start..."); return; }
    if (g_MultiplayerSession) { SetStatus("Online game is already starting"); return; }
    g_StartSeed = g_Rng.seed;
    g_Rng.seedBackup = g_StartSeed;
    g_Rng.generationCount = 0;
    ActivateMultiplayerSession(false);
    g_LaunchNonce = ++g_Session ^ SDL_GetTicks() ^ 0x13579bdu;
    g_StartupPhase = STARTUP_PREPARING;
    g_StartupStartedAt = SDL_GetTicks();
    g_StartGameRequested = true;
    SetStatus("Preparing synchronized game...");
    SendStartupPacket(kPrepare);
    g_LastStartupSend = SDL_GetTicks();
}
void Online::StartLocalGame()
{
    // Drop any previous socket/peer while keeping the new two-player session
    // alive for the local split-input path.
    Leave();
    ActivateMultiplayerSession(true);
    g_LocalGameRequested = true;
    SetStatus("Starting local two-player game...");
}
bool Online::ConsumeStartGameRequested() { const bool value = g_StartGameRequested; g_StartGameRequested = false; return value; }
bool Online::ConsumeLocalGameRequested() { const bool value = g_LocalGameRequested; g_LocalGameRequested = false; return value; }
bool Online::NotifyMenuReady(i32 gameState, i32 cursor)
{
    if (!g_MultiplayerSession || g_LocalSession || g_StartupPhase == STARTUP_FAILED) return false;
    if (gameState < 0 || gameState > 255) { FailStartup("Invalid online menu state"); return false; }
    const bool firstReady = !g_MenuReadySent;
    if (!firstReady && g_MenuReadyState != gameState)
    {
        FailStartup("Devices reached different game menus");
        return false;
    }
    g_MenuReadyState = gameState;
    // Cursor movement is normal authoritative menu input. The barrier only
    // validates that both peers reached the same menu page; difficulty is
    // driven by the host's synchronized P1 lane after that point.
    g_MenuReadyCursor = cursor;
    g_MenuReadySent = true;
    if (firstReady) g_StartupStartedAt = SDL_GetTicks();
    if (firstReady && (g_StartupPhase == STARTUP_MENU_WAIT || g_StartupPhase == STARTUP_PREPARING))
    {
        SendStartupPacket(kMenuReady);
        g_LastStartupSend = SDL_GetTicks();
        SetStatus("Waiting for peer menu...");
    }
    if (g_Host && g_PeerMenuReady &&
        g_StartupPhase != STARTUP_MENU_COMMITTED &&
        g_StartupPhase != STARTUP_GAME_WAIT && g_StartupPhase != STARTUP_GAME_COMMITTED)
    {
        if (g_PeerMenuState != g_MenuReadyState)
        {
            FailStartup("Devices reached different menu state");
            return false;
        }
        if (CommitMenuBarrier(g_BarrierEpoch + 1))
        {
            SendStartupPacket(kMenuCommit);
            SetStatus("Menu synchronized");
            return true;
        }
    }
    return false;
}
bool Online::IsAwaitingMenuCommit()
{
    return g_MultiplayerSession && !g_LocalSession && g_MenuReadySent &&
           g_StartupPhase != STARTUP_MENU_COMMITTED &&
           g_StartupPhase != STARTUP_GAME_WAIT && g_StartupPhase != STARTUP_GAME_COMMITTED;
}
bool Online::IsInputSynchronizationActive()
{
    return g_InputSynchronizationActive;
}
void Online::NotifyGameReady(i32 difficulty, i32 stage)
{
    if (!g_MultiplayerSession || g_LocalSession || !g_InputSynchronizationActive) return;
    if (difficulty < 0 || difficulty > 5 || stage < 0 || stage > 7)
    {
        FailStartup("Invalid game configuration");
        return;
    }
    if (g_GameReadySent && (g_GameDifficulty != difficulty || g_GameStage != stage))
    {
        FailStartup("Local game configuration changed while synchronizing");
        return;
    }
    g_GameDifficulty = difficulty;
    g_GameStage = stage;
    g_GameReadySent = true;
    g_StartupPhase = STARTUP_GAME_WAIT;
    g_StartupStartedAt = SDL_GetTicks();
    SendStartupPacket(kGameReady);
    g_LastStartupSend = SDL_GetTicks();
    SetStatus("Waiting for peer game configuration...");
    if (g_Host && g_PeerGameReady && g_PeerGameDifficulty == g_GameDifficulty &&
        g_PeerGameStage == g_GameStage && g_PeerCharacters[0] == g_PlayerCharacters[0] &&
        g_PeerShots[0] == g_PlayerShots[0] && g_PeerCharacters[1] == g_PlayerCharacters[1] &&
        g_PeerShots[1] == g_PlayerShots[1])
    {
        CommitGameplayBarrier(g_BarrierEpoch + 1);
        SendStartupPacket(kGameCommit);
        SetStatus("Game configuration synchronized");
    }
    else if (g_Host && g_PeerGameReady)
    {
        FailStartup("Game settings differ between devices");
    }
}
bool Online::IsAwaitingGameCommit()
{
    return g_MultiplayerSession && !g_LocalSession && g_GameReadySent &&
           g_StartupPhase != STARTUP_GAME_COMMITTED;
}
bool Online::ConsumeGameplayCommit()
{
    const bool value = g_GameplayCommit;
    g_GameplayCommit = false;
    return value;
}
void Online::StartHost()
{
    ApplyTransportInputDelay(MODE_NEARBY_LAN);
    g_Mode = MODE_NEARBY_LAN; if (!OpenSocket()) { SetStatus("Unable to open UDP port 37707"); return; }
    g_Host = true; g_Session = SDL_GetTicks() ^ 0x07a7c0deu; g_State = STATE_HOSTING; g_LastPeer = SDL_GetTicks(); SetStatus("Hosting LAN room on port 37707"); TH07_IOS_StartBonjourHost(kPort);
}
void Online::StartSearch()
{
    ApplyTransportInputDelay(MODE_NEARBY_LAN);
    g_Mode = MODE_NEARBY_LAN; if (!OpenSocket()) { SetStatus("Unable to open UDP port 37707"); return; }
    TH07_IOS_TriggerLocalNetworkPermission();
    g_Host = false; g_Session = SDL_GetTicks() ^ 0x07a7c0deu; g_Peer = {}; g_State = STATE_SEARCHING; g_LastDiscovery = 0; SetStatus("Waiting for local network permission...");
}
void Online::JoinAddress(const char *address) { StartDirect(address, kPort); }
void Online::StartDirect(const char *address, u16 port)
{
    ApplyTransportInputDelay(MODE_DIRECT);
    sockaddr_in peer = {}; peer.sin_family = AF_INET; peer.sin_port = htons(port ? port : kPort);
    if (!address || !*address)
    {
        SetStatus("Enter an address or domain");
        return;
    }
    if (inet_pton(AF_INET, address, &peer.sin_addr) != 1)
    {
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo *result = nullptr;
        if (getaddrinfo(address, nullptr, &hints, &result) != 0 || !result ||
            result->ai_addrlen < (int)sizeof(sockaddr_in))
        {
            if (result) freeaddrinfo(result);
            SetStatus("Unable to resolve host address");
            return;
        }
        peer.sin_addr = ((const sockaddr_in *)result->ai_addr)->sin_addr;
        freeaddrinfo(result);
    }
    // Resolve before replacing an existing socket.  A typo must not leave a
    // bound-but-idle UDP endpoint behind or disturb an active room.
    if (!OpenSocket()) { SetStatus("Unable to open UDP port 37707"); return; }
    SDL_strlcpy(g_DirectAddress, address, sizeof(g_DirectAddress));
    g_Mode = MODE_DIRECT; g_Host = false; g_Peer = peer; g_Session = SDL_GetTicks() ^ 0x07a7c0deu;
    SDL_strlcpy(g_PeerAddress, address, sizeof(g_PeerAddress)); g_State = STATE_SEARCHING;
    g_LastHandshake = 0; SetStatus("Connecting to direct address...");
}
void Online::StartRelay(const char *endpoint, const char *room, bool hostRole)
{
    ApplyTransportInputDelay(MODE_RELAY);
    SDL_strlcpy(g_RelayEndpoint, endpoint ? endpoint : "", sizeof(g_RelayEndpoint));
    SDL_strlcpy(g_RelayRoom, room ? room : "", sizeof(g_RelayRoom));
    if (!g_RelayEndpoint[0] || !g_RelayRoom[0] || strchr(g_RelayRoom, ' '))
    {
        SetStatus("Relay address and room are required"); return;
    }
    char host[128] = {}; u32 port = 3478;
    const char *colon = strrchr(g_RelayEndpoint, ':');
    if (colon)
    {
        const size_t hostLength = std::min((size_t)(colon - g_RelayEndpoint), sizeof(host) - 1);
        memcpy(host, g_RelayEndpoint, hostLength); host[hostLength] = '\0';
        const int parsedPort = atoi(colon + 1); if (parsedPort > 0 && parsedPort <= 65535) port = (u32)parsedPort;
    }
    else SDL_strlcpy(host, g_RelayEndpoint, sizeof(host));
    sockaddr_in relay = {}; relay.sin_family = AF_INET; relay.sin_port = htons((u16)port);
    if (inet_pton(AF_INET, host, &relay.sin_addr) != 1)
    {
        SetStatus("Relay must be an IPv4 address[:port]"); return;
    }
    if (!OpenSocket()) { SetStatus("Unable to open relay socket"); return; }
    g_Mode = MODE_RELAY; g_Host = hostRole; g_Peer = relay;
    SDL_strlcpy(g_PeerAddress, host, sizeof(g_PeerAddress));
    g_Session = SDL_GetTicks() ^ 0x07a7c0deu;
    g_State = hostRole ? STATE_HOSTING : STATE_SEARCHING; g_LastRelayRegister = 0;
    SendRelayRegister();
}
void Online::StartBluetoothHost()
{
    Leave(); g_Mode = MODE_BLUETOOTH; g_Host = true; g_State = STATE_HOSTING;
    ApplyTransportInputDelay(MODE_BLUETOOTH);
#if defined(TH07_IOS)
    if (!TH07_IOS_BluetoothStart(1)) { SetStatus("Bluetooth start failed"); return; } SetStatus("Waiting for nearby guest...");
#else
    SetStatus("Bluetooth is iOS only");
#endif
}
void Online::StartBluetoothGuest()
{
    Leave(); g_Mode = MODE_BLUETOOTH; g_Host = false; g_State = STATE_SEARCHING;
    ApplyTransportInputDelay(MODE_BLUETOOTH);
#if defined(TH07_IOS)
    if (!TH07_IOS_BluetoothStart(0)) { SetStatus("Bluetooth start failed"); return; } SetStatus("Searching nearby Bluetooth host...");
#else
    SetStatus("Bluetooth is iOS only");
#endif
}
void Online::Leave()
{
    if (g_State == STATE_CONNECTED)
    {
        if (g_Mode == MODE_BLUETOOTH)
        {
#if defined(TH07_IOS)
            SendBluetooth(kBye); TH07_IOS_BluetoothStop();
#endif
        }
        else SendPacket(g_Peer, kBye);
    }
    CloseSocket();
#if defined(TH07_IOS)
    TH07_IOS_StopBonjourHost(); if (g_Mode == MODE_BLUETOOTH) TH07_IOS_BluetoothStop();
#endif
    g_State = STATE_IDLE; g_Host = false; g_LastRtt = 0;
    g_MultiplayerSession = false; g_LocalSession = false;
    g_InputSynchronizationActive = false; g_StartupPhase = STARTUP_NONE;
    g_GameplayCommit = false; g_LaunchNonce = 0; g_BarrierEpoch = 0; ResetSyncState();
    g_ShellKind = SHARED_SHELL_NONE;
    g_ShellSelection[0] = g_ShellSelection[1] = 0;
    g_ShellVote[0] = g_ShellVote[1] = 0;
    g_ShellCommit = SHARED_COMMIT_NONE;
    g_ShellConfirmAction = SHARED_CONFIRM_NONE;
    g_ShellHandoffFrame = 0;
    g_ShellCommitRevision = 0;
    g_ShellCommitDelivered = false;
    g_ShellCommitAcked = false;
    g_ShellHandoffReady = false;
    g_ShellRevision = 0;
    g_QueuedShellButtons = 0;
    g_QueuedInputButtons = 0;
    g_LocalMenuCursor = g_RemoteMenuCursor = g_MenuCursorTarget = -1;
    g_LocalShellSelectionRequest = g_RemoteShellSelectionRequest = -1;
    g_GameManager.isInPauseMenu = 0;
    g_GameManager.isInRetryMenu = 0;
    g_GameManager.isPaused = 0;
    g_Peer = {}; g_PeerAddress[0] = '\0'; SetStatus("Offline");
}
void Online::HandleTouch(f32 x, f32 y, f32 panelX, f32 panelY, f32 panelW, f32 panelH)
{
    if (panelW <= 0.0f || panelH <= 0.0f || x < panelX || x > panelX + panelW) return;
    const f32 top = panelY + panelH * 0.16f, rowHeight = panelH * 0.073f;
    const i32 row = (i32)((y - top) / rowHeight);
    if (row == 0) SetMode(MODE_NEARBY_LAN); else if (row == 1) SetMode(MODE_DIRECT); else if (row == 2) SetMode(MODE_RELAY); else if (row == 3) SetMode(MODE_BLUETOOTH);
    else if (row == 4) { if (g_Mode == MODE_NEARBY_LAN) StartHost(); else if (g_Mode == MODE_DIRECT) TH07_IOS_RequestOnlineText(1, "Host address or domain", g_DirectAddress); else if (g_Mode == MODE_RELAY && !g_RelayEndpoint[0]) TH07_IOS_RequestOnlineText(2, "Relay server (ip:port)", g_RelayEndpoint); else if (g_Mode == MODE_RELAY && !g_RelayRoom[0]) TH07_IOS_RequestOnlineText(3, "Relay room code", g_RelayRoom); else if (g_Mode == MODE_RELAY) StartRelay(g_RelayEndpoint, g_RelayRoom, true); else StartBluetoothHost(); }
    else if (row == 5) { if (g_Mode == MODE_NEARBY_LAN) StartSearch(); else if (g_Mode == MODE_DIRECT) StartDirect(g_DirectAddress, kPort); else if (g_Mode == MODE_BLUETOOTH) StartBluetoothGuest(); else if (!g_RelayEndpoint[0]) TH07_IOS_RequestOnlineText(2, "Relay server (ip:port)", g_RelayEndpoint); else if (!g_RelayRoom[0]) TH07_IOS_RequestOnlineText(3, "Relay room code", g_RelayRoom); else StartRelay(g_RelayEndpoint, g_RelayRoom, false); }
    else if (row == 6) Leave();
    else if (row == 7) RequestStartGame();
    else if (row == 8) StartLocalGame();
    else if (row == 9 || y > panelY + panelH * 0.94f) CloseMenu();
}
Online::State Online::GetState() { return g_State; }
bool Online::IsConnected() { return g_State == STATE_CONNECTED; }
bool Online::IsHost() { return g_Host; }
const char *Online::GetStatusText() { return g_Status; }
const char *Online::GetPeerAddress() { return g_PeerAddress; }
u32 Online::GetRoundTripMs() { return g_LastRtt; }
bool Online::IsMultiplayerSession() { return g_MultiplayerSession; }
bool Online::IsNetworkSession() { return g_MultiplayerSession && !g_LocalSession; }
int Online::GetLocalPlayerSlot() { return g_Host || g_LocalSession ? 0 : 1; }
void Online::ResetInputSynchronization() { ResetSyncState(); }
bool Online::IsSharedShellActive(SharedShellKind kind)
{
    if (!g_MultiplayerSession || g_LocalSession) return false;
    return kind == SHARED_SHELL_NONE || g_ShellKind == kind;
}
Online::SharedShellKind Online::GetSharedShellKind() { return g_ShellKind; }
u8 Online::GetSharedShellSelection(u8 playerId)
{
    return playerId < 2 ? g_ShellSelection[playerId] : 0;
}
u8 Online::GetSharedShellVote(u8 playerId)
{
    return playerId < 2 ? g_ShellVote[playerId] : 0;
}
Online::SharedShellConfirmAction Online::GetSharedShellConfirmAction()
{
    return g_ShellConfirmAction;
}
bool Online::RequestSharedShellEnter(SharedShellKind kind)
{
    if (!IsNetworkSession() || kind == SHARED_SHELL_NONE) return false;
    if (g_Host)
    {
        OpenSharedShellOnHost(kind);
        return g_ShellKind == kind;
    }
    g_ShellRequest = (u8)kind;
    return true;
}
bool Online::ConsumeSharedShellCommit(SharedShellCommit *commit)
{
    if (!commit || g_ShellCommit == SHARED_COMMIT_NONE || g_ShellCommitDelivered)
        return false;
    if (g_ShellHandoffFrame != 0 && g_InputFrame < g_ShellHandoffFrame)
        return false;
    g_ShellHandoffReady = true;
    *commit = g_ShellCommit;
    g_ShellCommitDelivered = true;
    return true;
}
void Online::QueueSharedShellButton(u16 button) { g_QueuedShellButtons |= button; }
void Online::QueueSharedShellSelection(u8 selection)
{
    if (IsNetworkSession()) g_LocalShellSelectionRequest = std::clamp((int)selection, 0, 2);
}
void Online::QueueInputPulse(u16 buttons)
{
    if (!IsNetworkSession()) return;
    // Menu gestures are momentary edges.  Keep only the newest unsent edge so
    // a stalled frame cannot combine an old tap with a later swipe.
    g_QueuedInputButtons = buttons;
    if ((buttons & TH_BUTTON_SELECTMENU) == 0) g_LocalMenuCursor = -1;
}
void Online::QueueMenuCursor(i32 cursor)
{
    if (IsNetworkSession()) g_LocalMenuCursor = std::max(-1, cursor);
}
bool Online::ConsumeMenuCursorTarget(i32 *cursor)
{
    if (!cursor || g_MenuCursorTarget < 0) return false;
    *cursor = g_MenuCursorTarget;
    g_MenuCursorTarget = -1;
    return true;
}
bool Online::SynchronizeInputs(u16 localButtons, f32 localDx, f32 localDy,
                               u16 *p1Buttons, u16 *p2Buttons,
                               f32 *p1Dx, f32 *p1Dy, f32 *p2Dx, f32 *p2Dy)
{
    if (!p1Buttons || !p2Buttons || !p1Dx || !p1Dy || !p2Dx || !p2Dy) return false;
    UpdateShellCloseComplete();
    if (g_ShellCommit != SHARED_COMMIT_NONE && g_ShellHandoffFrame != 0 &&
        g_InputFrame >= g_ShellHandoffFrame)
    {
        g_ShellHandoffReady = true;
    }
    // The host publishes the inactive shell only after both local and remote
    // sides consumed the commit.  This replaces the old per-device timer,
    // which allowed one phone to resume while the other was still in the
    // closing animation.
    if (g_Host && g_ShellCommit != SHARED_COMMIT_NONE && g_ShellHandoffReady &&
        g_ShellCommitDelivered && g_ShellCloseComplete && g_ShellCommitAcked)
    {
        g_ShellKind = SHARED_SHELL_NONE;
        g_ShellSelection[0] = g_ShellSelection[1] = 0;
        g_ShellVote[0] = g_ShellVote[1] = 0;
        g_ShellCommit = SHARED_COMMIT_NONE;
        g_ShellConfirmAction = SHARED_CONFIRM_NONE;
        g_ShellHandoffFrame = 0;
        g_ShellCommitRevision = 0;
        g_ShellCommitAcked = false;
        g_ShellHandoffReady = false;
        g_ShellCommitDelivered = false;
        g_ShellCloseComplete = false;
        g_RemoteAuthoritativeStatePresent = false;
        g_RemoteAuthoritativeShellRevision = 0;
        g_LastAuthoritativeSendFrame = 0xffffffffu;
        g_ShellRequest = 0;
        g_LocalShellSelectionRequest = g_RemoteShellSelectionRequest = -1;
        g_LastShellButtons[0] = g_LastShellButtons[1] = 0;
        g_ShellOpenedFrame = 0xffffffffu;
        ++g_ShellRevision;
        g_GameManager.isInPauseMenu = 0;
        g_GameManager.isInRetryMenu = 0;
        g_GameManager.isPaused = 0;
        MobileDiagnostics::Log("online/shell", "handoff complete frame=%u revision=%u",
                               g_InputFrame, g_ShellRevision);
    }
    const u16 queuedShellButtons = g_QueuedShellButtons;
    const u16 queuedInputButtons = g_QueuedInputButtons;
    localButtons |= queuedShellButtons | queuedInputButtons;
    ApplyPendingAuthoritativeState();
    if (!g_MultiplayerSession || (!g_LocalSession && !g_InputSynchronizationActive))
    {
        const bool guestLane = g_MultiplayerSession && !g_LocalSession && !g_Host;
        *p1Buttons = guestLane ? 0 : localButtons;
        *p2Buttons = guestLane ? localButtons : 0;
        *p1Dx = guestLane ? 0.0f : localDx;
        *p1Dy = guestLane ? 0.0f : localDy;
        *p2Dx = guestLane ? localDx : 0.0f;
        *p2Dy = guestLane ? localDy : 0.0f;
        return true;
    }
    if (g_LocalSession)
    {
        *p1Buttons = localButtons; *p2Buttons = localButtons;
        *p1Dx = localDx; *p1Dy = localDy; *p2Dx = *p2Dy = 0.0f; return true;
    }
    if (g_Mode == Online::MODE_BLUETOOTH && g_BluetoothDisconnectedAt)
    {
        const u32 now = SDL_GetTicks();
        if (now - g_BluetoothDisconnectedAt < kBluetoothReconnectGraceMs)
            return false;
        FailStartup("Nearby peer disconnected");
        *p1Buttons = localButtons; *p2Buttons = 0;
        *p1Dx = localDx; *p1Dy = localDy; *p2Dx = *p2Dy = 0.0f;
        return true;
    }
    if (g_State != STATE_CONNECTED)
    {
        FailStartup("Connection lost during synchronized play");
        *p1Buttons = localButtons; *p2Buttons = 0;
        *p1Dx = localDx; *p1Dy = localDy; *p2Dx = *p2Dy = 0.0f;
        return true;
    }
    const u8 localPlayerId = g_Host ? 0 : 1;
    if (g_ShellKind == Online::SHARED_SHELL_NONE && IsBattlePauseInputAllowed() &&
        ShellButtonEdge(localButtons, TH_BUTTON_MENU, localPlayerId))
    {
        if (g_Host)
            OpenSharedShellOnHost(Online::SHARED_SHELL_PAUSE);
        else
            g_ShellRequest = (u8)Online::SHARED_SHELL_PAUSE;
    }
    if (!g_LocalInputSent)
    {
        const u32 targetFrame = g_InputFrame + (u32)g_InputDelay;
        InputFrame input = {};
        input.frame = targetFrame;
        input.buttons = localButtons;
        input.touchDx = localDx;
        input.touchDy = localDy;
        input.stateHash = targetFrame % 120 == 0 ? ComputeSynchronizationHash(targetFrame) : 0;
        input.shellSelectionRequest = g_LocalShellSelectionRequest >= 0
                                          ? (i8)std::clamp(g_LocalShellSelectionRequest, -1, 2)
                                          : -1;
        input.shellSelectionValid = input.shellSelectionRequest >= 0;
        input.menuCursor = g_LocalMenuCursor >= 0 ? (i16)g_LocalMenuCursor : -1;
        input.present = true;
        g_LocalInputHistory.Store(targetFrame, input);
        g_LocalInputSent = true;
        // Clear only after the pulse has been copied into a concrete frame.
        // If the previous frame is still waiting for the peer, leave it
        // queued so a touch confirmation cannot disappear between frames.
        g_QueuedShellButtons &= (u16)~queuedShellButtons;
        g_QueuedInputButtons &= (u16)~queuedInputButtons;
    }

    const u32 now = SDL_GetTicks();
    const u32 firstFrame = g_InputFrame > 32 ? g_InputFrame - 32 : 0;
    const u32 lastFrame = g_InputFrame + (u32)g_InputDelay;
    u32 sendBudget = kInputSendBudgetPerTick;
    auto sendDueFrame = [&](u32 frame) {
        if (!sendBudget) return;
        auto *slot = g_LocalInputHistory.Find(frame);
        if (!slot || slot->acknowledged ||
            (slot->lastSendMs && now - slot->lastSendMs < kInputRetransmitIntervalMs))
            return;
        if (SendStoredInput(frame, slot->value))
        {
            if (slot->lastSendMs) ++g_InputRetransmits;
            slot->lastSendMs = now ? now : 1;
            --sendBudget;
        }
    };
    // Always put the newest frame on the wire first. If an older frame was
    // lost, sending it before the newest frame can starve the live edge when
    // the history has accumulated many unacknowledged entries.
    sendDueFrame(lastFrame);
    for (u32 frame = firstFrame; frame <= lastFrame && sendBudget; ++frame)
    {
        if (frame != lastFrame) sendDueFrame(frame);
    }

    InputFrame &remote = g_RemoteInputs[g_InputFrame % kInputHistorySize];
    if (!remote.present || remote.frame != g_InputFrame)
    {
        if (!g_InputStalled)
        {
            g_InputStalled = true;
            MobileDiagnostics::Log("online/frame", "stall begin frame=%u rtt=%u retransmit=%u",
                                   g_InputFrame, g_LastRtt, g_InputRetransmits);
        }
        if (now - g_LastSyncProgress > kInputStallTimeoutMs)
        {
            FailStartup("Peer input timed out; synchronized play stopped");
            *p1Buttons = localButtons; *p2Buttons = 0;
            *p1Dx = localDx; *p1Dy = localDy; *p2Dx = *p2Dy = 0.0f;
            return true;
        }
        return false;
    }
    if (g_InputStalled)
    {
        g_InputStalled = false;
        MobileDiagnostics::Log("online/frame", "stall end frame=%u rtt=%u retransmit=%u",
                               g_InputFrame, g_LastRtt, g_InputRetransmits);
    }
    auto *localSlot = g_LocalInputHistory.Find(g_InputFrame);
    if (!localSlot)
    {
        FailStartup("Local input history overflow");
        return true;
    }
    const InputFrame local = localSlot->value;
    if (local.stateHash && remote.stateHash && local.stateHash != remote.stateHash)
    {
        // State hashes are diagnostics, not a transport failure condition.
        // The input stream remains authoritative and continues to advance;
        // transient cross-version state differences must not drop the peer.
        MobileDiagnostics::Log("online/sync",
            "state hash warning frame=%u local=%08x remote=%08x; continuing",
            g_InputFrame, local.stateHash, remote.stateHash);
    }
    if (g_Host)
    {
        *p1Buttons = local.buttons; *p2Buttons = remote.buttons;
        *p1Dx = local.touchDx; *p1Dy = local.touchDy;
        *p2Dx = remote.touchDx; *p2Dy = remote.touchDy;
    }
    else
    {
        *p1Buttons = remote.buttons; *p2Buttons = local.buttons;
        *p1Dx = remote.touchDx; *p1Dy = remote.touchDy;
        *p2Dx = local.touchDx; *p2Dy = local.touchDy;
    }
    const u8 menuLane = g_SelectingPlayer2Loadout ? 1 : 0;
    const InputFrame &hostInput = g_Host ? local : remote;
    const InputFrame &guestInput = g_Host ? remote : local;
    const InputFrame &activeMenuInput = menuLane == 0 ? hostInput : guestInput;
    g_MenuCursorTarget = activeMenuInput.menuCursor;
    ProcessSharedShellFrame(*p1Buttons, *p2Buttons,
                            g_Host ? local : remote,
                            g_Host ? remote : local);
    g_LastShellButtons[0] = *p1Buttons;
    g_LastShellButtons[1] = *p2Buttons;
    remote = {};
    g_InputFrame++;
    g_LocalInputSent = false;
    g_LastSyncProgress = now;
    if (g_InputFrame > 1 && (g_InputFrame - 1) % 120 == 0)
    {
        MobileDiagnostics::Log("online/frame",
            "frame=%u ack=%u mask=%08x rtt=%u retransmit=%u duplicate=%u outOfOrder=%u hash=%08x",
            g_InputFrame, g_RemoteAckFrame, g_RemoteAckMask, g_LastRtt,
            g_InputRetransmits, g_InputDuplicates, g_InputOutOfOrder,
            local.stateHash);
    }
    return true;
}
void Online::PublishAuthoritativeState()
{
    // State packets are intentionally repeated. UDP loss must only delay a
    // correction; it must never make the simulation wait for a second channel.
    if (!g_Host || !g_MultiplayerSession || g_InputFrame == g_LastAuthoritativeSendFrame)
    {
        return;
    }
    if (SendAuthoritativeStatePacket())
    {
        g_LastAuthoritativeSendFrame = g_InputFrame;
    }
}
void Online::ResetLoadouts()
{
    g_PlayerCharacters[0] = g_PlayerCharacters[1] = 0;
    g_PlayerShots[0] = g_PlayerShots[1] = 0;
    g_Player2LoadoutSelected = false;
    g_SelectingPlayer2Loadout = false;
    g_GameReadySent = g_PeerGameReady = false;
    g_GameplayCommit = false;
    g_GameDifficulty = g_GameStage = -1;
    g_PeerGameDifficulty = g_PeerGameStage = -1;
    if (g_MultiplayerSession && !g_LocalSession && g_InputSynchronizationActive)
    {
        ++g_BarrierEpoch;
        g_MenuReadySent = g_PeerMenuReady = false;
        g_MenuReadyState = g_PeerMenuState = -1;
        g_MenuReadyCursor = g_PeerMenuCursor = -1;
        g_StartupPhase = STARTUP_MENU_WAIT;
        g_StartupStartedAt = SDL_GetTicks();
    }
}
bool Online::NeedsPlayer2Loadout()
{
    return g_MultiplayerSession && !g_Player2LoadoutSelected;
}
bool Online::IsSelectingPlayer2Loadout() { return g_SelectingPlayer2Loadout; }
void Online::BeginPlayer2Loadout(i32 p1Character, i32 p1Shot)
{
    g_PlayerCharacters[0] = std::clamp(p1Character, 0, 2);
    g_PlayerShots[0] = std::clamp(p1Shot, 0, 1);
    g_SelectingPlayer2Loadout = true;
}
void Online::CompletePlayer2Loadout(i32 p2Character, i32 p2Shot)
{
    g_PlayerCharacters[1] = std::clamp(p2Character, 0, 2);
    g_PlayerShots[1] = std::clamp(p2Shot, 0, 1);
    g_Player2LoadoutSelected = true;
    g_SelectingPlayer2Loadout = false;
}
i32 Online::GetPlayerCharacter(u8 playerId)
{
    return playerId < 2 ? g_PlayerCharacters[playerId] : 0;
}
i32 Online::GetPlayerShot(u8 playerId)
{
    return playerId < 2 ? g_PlayerShots[playerId] : 0;
}
