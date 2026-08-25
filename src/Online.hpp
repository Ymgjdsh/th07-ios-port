#pragma once

#include "inttypes.hpp"

// Platform-neutral transport and two-player lockstep session used by the
// in-game Online menu. Once Start Game is acknowledged, menu and gameplay
// input are advanced only when both player lanes for the same logical frame
// are available.
namespace Online
{
enum SharedShellKind
{
    SHARED_SHELL_NONE = 0,
    SHARED_SHELL_PAUSE = 1,
    SHARED_SHELL_RETRY = 2,
};

enum SharedShellCommit
{
    SHARED_COMMIT_NONE = 0,
    SHARED_COMMIT_RESUME = 1,
    SHARED_COMMIT_RETRY = 2,
    SHARED_COMMIT_TITLE = 3,
    SHARED_COMMIT_RESET = 4,
};
enum SharedShellConfirmAction
{
    SHARED_CONFIRM_NONE = 0,
    SHARED_CONFIRM_TITLE = 1,
    SHARED_CONFIRM_RESET = 2,
};
enum State
{
    STATE_IDLE = 0,
    STATE_HOSTING,
    STATE_SEARCHING,
    STATE_CONNECTED,
};

enum Mode
{
    MODE_NEARBY_LAN = 0,
    MODE_DIRECT = 1,
    MODE_RELAY = 2,
    MODE_BLUETOOTH = 3,
};

void Initialize();
void Shutdown();
void Update();
// Called by the SDL lifecycle bridge. A backgrounded peer pauses the shared
// simulation; foreground only resumes after the peer has acknowledged return.
void NotifyAppBackgrounded();
void NotifyAppForegrounded();

void OpenMenu();
void CloseMenu();
bool IsMenuOpen();

void StartHost();
void StartSearch();
void JoinAddress(const char *address);
void StartDirect(const char *address, u16 port);
void StartRelay(const char *endpoint, const char *room, bool hostRole);
void StartBluetoothHost();
void StartBluetoothGuest();
void SetMode(Mode mode);
Mode GetMode();
void SetInputDelay(int frames);
int GetInputDelay();
u16 GetPort();
const char *GetRelayEndpoint();
const char *GetRelayRoom();
bool IsBluetooth();
void RequestStartGame();
void StartLocalGame();
bool ConsumeStartGameRequested();
bool ConsumeLocalGameRequested();
void Leave();
void HandleTouch(f32 x, f32 y, f32 panelX, f32 panelY, f32 panelW, f32 panelH);

// Startup barriers. Netplay does not begin consuming lockstep frames until
// both devices reached the same difficulty menu, and gameplay does not load
// until both devices acknowledged the final game configuration.
// Returns true when this call commits the menu barrier and resets the
// lockstep frame history. The caller must not consume the pre-reset input
// that was sampled earlier in the same engine frame.
bool NotifyMenuReady(i32 gameState, i32 cursor);
bool IsAwaitingMenuCommit();
bool IsInputSynchronizationActive();
void NotifyGameReady(i32 difficulty, i32 stage);
bool IsAwaitingGameCommit();
bool ConsumeGameplayCommit();

State GetState();
bool IsConnected();
bool IsHost();
const char *GetStatusText();
const char *GetPeerAddress();
u32 GetRoundTripMs();

bool IsMultiplayerSession();
bool IsNetworkSession();
int GetLocalPlayerSlot();
// Returns the remote player's auto-bomb setting after it has been received.
bool IsRemoteAutoBombEnabled();

// Returns false while the matching peer frame is still in flight. P1 is
// always the host and P2 is always the guest, independent of local device.
bool SynchronizeInputs(u16 localButtons, f32 localDx, f32 localDy,
                       u16 *p1Buttons, u16 *p2Buttons,
                       f32 *p1Dx, f32 *p1Dy, f32 *p2Dx, f32 *p2Dy);
// The host periodically publishes the canonical player state. The guest
// applies it before the next lockstep frame so small platform differences
// cannot leave the two player scenes permanently divergent.
void PublishAuthoritativeState();
void ResetInputSynchronization();

// Shared pause/retry shell. The host owns the state and both player lanes
// vote independently; a commit is exposed once on each device.
bool IsSharedShellActive(SharedShellKind kind = SHARED_SHELL_NONE);
SharedShellKind GetSharedShellKind();
u8 GetSharedShellSelection(u8 playerId);
u8 GetSharedShellVote(u8 playerId);
SharedShellConfirmAction GetSharedShellConfirmAction();
bool RequestSharedShellEnter(SharedShellKind kind);
bool ConsumeSharedShellCommit(SharedShellCommit *commit);
void QueueSharedShellButton(u16 button);
void QueueSharedShellSelection(u8 selection);
// Queue a one-frame menu/action edge for the next synchronized input frame.
// Unlike the mobile UI pulse queue, this survives a lockstep stall until the
// frame has actually been stored and transmitted.
void QueueInputPulse(u16 buttons);
void ReportMenuState(i32 gameState);
void QueueMenuCursor(i32 cursor);
bool ConsumeMenuCursorTarget(i32 *cursor);

void ResetLoadouts();
bool NeedsPlayer2Loadout();
bool IsSelectingPlayer2Loadout();
void BeginPlayer2Loadout(i32 p1Character, i32 p1Shot);
void CompletePlayer2Loadout(i32 p2Character, i32 p2Shot);
i32 GetPlayerCharacter(u8 playerId);
i32 GetPlayerShot(u8 playerId);
} // namespace Online
