#include "GameManager.hpp"

#include "Player.hpp"

// P1 stays in TH07's integrity-checked globals. P2 uses a deterministic
// sidecar because the original save/replay structures contain one resource
// set only.
MultiplayerPlayerResources g_MultiplayerPlayerResources[1] = {{0, 0, 0}};

static MultiplayerPlayerResources *GetSidecarResources(u8 playerId)
{
    return playerId == 1 ? &g_MultiplayerPlayerResources[0] : nullptr;
}

i32 GetPlayerLives(u8 playerId)
{
    MultiplayerPlayerResources *resources = GetSidecarResources(playerId);
    return resources ? resources->livesRemaining
                     : (i32)g_GameManager.globals->livesRemaining;
}

i32 GetPlayerBombs(u8 playerId)
{
    MultiplayerPlayerResources *resources = GetSidecarResources(playerId);
    return resources ? resources->bombsRemaining
                     : (i32)g_GameManager.globals->bombsRemaining;
}

i32 GetPlayerPower(u8 playerId)
{
    MultiplayerPlayerResources *resources = GetSidecarResources(playerId);
    return resources ? resources->currentPower
                     : (i32)g_GameManager.globals->currentPower;
}

void SetPlayerLives(u8 playerId, i32 amount)
{
    MultiplayerPlayerResources *resources = GetSidecarResources(playerId);
    if (resources)
        resources->livesRemaining = amount;
    else
        g_GameManager.SetLivesRemaining(amount);
}

void SetPlayerBombs(u8 playerId, i32 amount)
{
    MultiplayerPlayerResources *resources = GetSidecarResources(playerId);
    if (resources)
        resources->bombsRemaining = amount;
    else
        g_GameManager.SetBombsRemainingAndComputeCsum(amount);
}

void SetPlayerPower(u8 playerId, i32 amount)
{
    MultiplayerPlayerResources *resources = GetSidecarResources(playerId);
    if (resources)
        resources->currentPower = amount;
    else
    {
        g_GameManager.SetCurrentPower(amount);
        g_GameManager.RegenerateGameIntegrityCsum();
    }
}

void AddPlayerLives(u8 playerId, i32 amount)
{
    SetPlayerLives(playerId, GetPlayerLives(playerId) + amount);
}

void AddPlayerBombs(u8 playerId, i32 amount)
{
    SetPlayerBombs(playerId, GetPlayerBombs(playerId) + amount);
}

void AddPlayerPower(u8 playerId, i32 amount)
{
    SetPlayerPower(playerId, GetPlayerPower(playerId) + amount);
}

void ResetMultiplayerPlayerResources(u8 playerId)
{
    MultiplayerPlayerResources *resources = GetSidecarResources(playerId);
    if (!resources)
    {
        return;
    }
    resources->livesRemaining = g_GameManager.defaultCfg
                                    ? g_GameManager.defaultCfg->lifeCount
                                    : (i32)g_GameManager.globals->livesRemaining;
    resources->bombsRemaining = g_Players[playerId].shooterData
                                    ? (i32)g_Players[playerId].shooterData->initialBombs
                                    : 0;
    resources->currentPower = 0;
}
