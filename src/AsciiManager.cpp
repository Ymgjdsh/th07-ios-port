#include "AsciiManager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "AnmIdx.hpp"
#include "AnmManager.hpp"
#include "Chain.hpp"
#include "Controller.hpp"
#include "EnemyManager.hpp"
#include "GameManager.hpp"
#include "GameWindow.hpp"
#include "Gui.hpp"
#include "MobileDiagnostics.hpp"
#include "MobileUi.hpp"
#include "Localization.hpp"
#include "Online.hpp"
#include "Player.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "ZunResult.hpp"

ChainElem g_AsciiManagerOnDrawMenusChain;

AsciiManager g_AsciiManager;

ChainElem g_AsciiManagerCalcChain;

ChainElem g_AsciiManagerOnDrawPopupsChain;

void AsciiManager::UpdateScripts()
{
    g_AnmManager->ExecuteScript(&this->cherryGauge);
    g_AnmManager->ExecuteScript(&this->cherryDigit);
    g_AnmManager->ExecuteScript(&this->bossMarkers[0]);
    g_AnmManager->ExecuteScript(&this->bossMarkers[1]);
    g_AnmManager->ExecuteScript(&this->bossMarkers[2]);
    g_AnmManager->ExecuteScript(&this->bossMarkers[3]);
    g_AnmManager->ExecuteScript(&this->cherryBorderActive);
}

AsciiManager::AsciiManager()
{
}

PauseMenu::PauseMenu()
{
}

RetryMenu::RetryMenu()
{
}

static void DrawLocalizedMenuText(AnmVm *vm, const char *chinese, const char *english)
{
    if (Localization::GetLanguage() == Localization::Language::Japanese || !vm || !vm->sprite)
        return;
    const char *text = Localization::GetLanguage() == Localization::Language::Chinese ? chinese : english;
    AnmManager::DrawVmTextFmt(g_AnmManager, vm, 0xffffff, 0, "%s", text);
}

void IncrementCapped(u32 *param, u32 cap)
{
    if (*param < cap)
    {
        (*param)++;
    }
}

static void ApplySharedShellCommit(PauseMenu &pauseMenu, RetryMenu &retryMenu,
                                   Online::SharedShellCommit commit);
static void SynchronizeSharedPausePage(PauseMenu &pauseMenu);
static void UpdateSharedShellVisuals(PauseMenu &pauseMenu, RetryMenu &retryMenu);
static bool g_SharedRetryTitlePending = false;

u32 AsciiManager::OnUpdate(AsciiManager *arg)
{
    i32 i;
    AsciiManagerPopup *curPopup;

    arg->UpdatePrev();

    if (!g_GameManager.isInPauseMenu && !g_GameManager.isInRetryMenu)
    {
        curPopup = arg->popups;
        for (i = 0; i < 723; i++, curPopup++)
        {
            if (!curPopup->inUse)
            {
                continue;
            }

            curPopup->prevPos = curPopup->pos;
            curPopup->pos.y -= 0.5f * g_Supervisor.effectiveFramerateMultiplier;
            curPopup->timer++;
            if (curPopup->timer > 60)
            {
                curPopup->inUse = 0;
            }
        }
    }
    else if (g_GameManager.isInPauseMenu)
    {
        if (Online::IsSharedShellActive(Online::SHARED_SHELL_PAUSE))
        {
            f32 gameX = 0.0f;
            f32 gameY = 0.0f;
            const MobileUi::OverlayTouchAction touchAction =
                MobileUi::ConsumeOverlayTouch(gameX, gameY);
            // Run the native menu state machine once before applying the
            // synchronized selection.  A freshly opened shared shell starts
            // in state 0; jumping straight to state 1/2 skips sprite setup and
            // leaves every option invisible on both devices.
            g_LastFrameRawInput = g_CurFrameRawInput = 0;
            arg->pauseMenu.OnUpdate(true);
            if (touchAction != MobileUi::OVERLAY_TOUCH_NONE)
                arg->pauseMenu.HandleMobileTouch(touchAction, gameX, gameY);
            Online::SharedShellCommit commit = Online::SHARED_COMMIT_NONE;
            if (Online::ConsumeSharedShellCommit(&commit))
                ApplySharedShellCommit(arg->pauseMenu, arg->retryMenu, commit);
            SynchronizeSharedPausePage(arg->pauseMenu);
            UpdateSharedShellVisuals(arg->pauseMenu, arg->retryMenu);
        }
        else
        {
            f32 gameX = 0.0f;
            f32 gameY = 0.0f;
            const MobileUi::OverlayTouchAction action =
                MobileUi::ConsumeOverlayTouch(gameX, gameY);
            if (action != MobileUi::OVERLAY_TOUCH_NONE)
                arg->pauseMenu.HandleMobileTouch(action, gameX, gameY);
            arg->pauseMenu.OnUpdate();
        }
    }
    if (g_GameManager.isInRetryMenu)
    {
        if (Online::IsSharedShellActive(Online::SHARED_SHELL_RETRY))
        {
            f32 gameX = 0.0f;
            f32 gameY = 0.0f;
            const MobileUi::OverlayTouchAction touchAction =
                MobileUi::ConsumeOverlayTouch(gameX, gameY);
            // See the pause shell above: state 0 performs the VM/sprite
            // initialization and must run before the synchronized cursor is
            // projected into the visual state.
            g_LastFrameRawInput = g_CurFrameRawInput = 0;
            arg->retryMenu.OnUpdate(true);
            if (touchAction != MobileUi::OVERLAY_TOUCH_NONE)
                arg->retryMenu.HandleMobileTouch(touchAction, gameX, gameY);
            Online::SharedShellCommit commit = Online::SHARED_COMMIT_NONE;
            if (Online::ConsumeSharedShellCommit(&commit))
                ApplySharedShellCommit(arg->pauseMenu, arg->retryMenu, commit);
            UpdateSharedShellVisuals(arg->pauseMenu, arg->retryMenu);
        }
        else
        {
            f32 gameX = 0.0f;
            f32 gameY = 0.0f;
            const MobileUi::OverlayTouchAction action =
                MobileUi::ConsumeOverlayTouch(gameX, gameY);
            if (action != MobileUi::OVERLAY_TOUCH_NONE)
                arg->retryMenu.HandleMobileTouch(action, gameX, gameY);
            arg->retryMenu.OnUpdate();
        }
    }
    if (g_SharedRetryTitlePending && !g_GameManager.isInRetryMenu)
    {
        g_SharedRetryTitlePending = false;
        g_Supervisor.curState = 1;
        g_Supervisor.currentTime = SDL_GetTicks();
        MobileDiagnostics::Log("online/shell", "shared retry title transition");
    }
    arg->UpdateScripts();
    if (g_GameManager.demo)
    {
        if (arg->vm.anmFileIdx == 0)
        {
            g_AnmManager->SetAnmIdxAndExecuteScript(&arg->vm, 7);
        }
        g_AnmManager->ExecuteScript(&arg->vm);
    }
    else
    {
        arg->vm.anmFileIdx = 0;
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

static void ApplySharedShellCommit(PauseMenu &pauseMenu, RetryMenu &retryMenu,
                                   Online::SharedShellCommit commit)
{
    g_LastFrameRawInput = g_CurFrameRawInput = 0;
    g_SharedRetryTitlePending = false;
    if (commit == Online::SHARED_COMMIT_RESUME)
    {
        for (i32 i = 0; i < 4; ++i)
            pauseMenu.menuSprites[i].pendingInterrupt = 2;
        pauseMenu.menuBackground.pendingInterrupt = 1;
        pauseMenu.curState = 4;
        pauseMenu.numFrames = 0;
        pauseMenu.OnUpdate(false);
    }
    else if (commit == Online::SHARED_COMMIT_RESET)
    {
        for (i32 i = 0; i < 4; ++i)
            pauseMenu.menuSprites[i].pendingInterrupt = 2;
        pauseMenu.menuBackground.pendingInterrupt = 1;
        pauseMenu.curState = 10;
        pauseMenu.numFrames = 0;
        pauseMenu.OnUpdate(false);
    }
    else if (commit == Online::SHARED_COMMIT_TITLE)
    {
        if (g_GameManager.isInPauseMenu)
        {
            for (i32 i = 0; i < 4; ++i)
                pauseMenu.menuSprites[i].pendingInterrupt = 2;
            pauseMenu.menuBackground.pendingInterrupt = 1;
            pauseMenu.curState = 9;
            pauseMenu.numFrames = 0;
            pauseMenu.OnUpdate(false);
        }
        else
        {
            for (i32 i = 0; i < 5; ++i)
                retryMenu.menuSprites[i].pendingInterrupt = 2;
            retryMenu.menuBackground.pendingInterrupt = 1;
            retryMenu.curState = 4;
            retryMenu.numFrames = 0;
            retryMenu.OnUpdate(false);
            g_SharedRetryTitlePending = true;
        }
    }
    else if (commit == Online::SHARED_COMMIT_RETRY)
    {
        for (i32 i = 0; i < 5; ++i)
            retryMenu.menuSprites[i].pendingInterrupt = 2;
        retryMenu.menuBackground.pendingInterrupt = 1;
        retryMenu.curState = 3;
        retryMenu.numFrames = 0;
        retryMenu.OnUpdate(false);
        // The stock retry animation resets only TH07's single P1 resource
        // block.  Netplay keeps P2 in a registered chain, so restore both
        // player objects now while the shared menu is covering the playfield.
        // This makes the first post-menu frame identical on host and guest.
        for (u8 playerId = 0; playerId < 2; ++playerId)
        {
            ResetPlayerForSharedRetry(playerId);
            ResetMultiplayerPlayerResources(playerId);
        }
        g_Gui.lifeDisplayUpdateFrames = 2;
        g_Gui.bombDisplayUpdateFrames = 2;
        g_Gui.powerDisplayUpdateFrames = 2;
    }
}

static void SynchronizeSharedPausePage(PauseMenu &pauseMenu)
{
    if (Online::GetSharedShellKind() != Online::SHARED_SHELL_PAUSE)
        return;

    const Online::SharedShellConfirmAction action = Online::GetSharedShellConfirmAction();
    if (action != Online::SHARED_CONFIRM_NONE &&
        pauseMenu.curState >= 1 && pauseMenu.curState <= 3)
    {
        for (i32 i = 0; i < 4; ++i)
            pauseMenu.menuSprites[i].pendingInterrupt = 2;
        for (i32 i = 4; i < 7; ++i)
            pauseMenu.menuSprites[i].pendingInterrupt = 1;
        // Native states 5/7 are the affirmative cursor for the title/reset
        // confirmation pages; states 6/8 are the negative cursor.
        pauseMenu.curState = action == Online::SHARED_CONFIRM_TITLE ? 5 : 7;
        pauseMenu.numFrames = 0;
    }
    else if (action == Online::SHARED_CONFIRM_NONE &&
             pauseMenu.curState >= 5 && pauseMenu.curState <= 8)
    {
        for (i32 i = 0; i < 4; ++i)
            pauseMenu.menuSprites[i].pendingInterrupt = 1;
        for (i32 i = 4; i < 7; ++i)
            pauseMenu.menuSprites[i].pendingInterrupt = 2;
        const u8 primary = std::clamp(Online::GetSharedShellSelection(0), (u8)1, (u8)2);
        pauseMenu.curState = primary + 1;
        pauseMenu.numFrames = 0;
    }
}

static void UpdateSharedShellVisuals(PauseMenu &pauseMenu, RetryMenu &retryMenu)
{
    const Online::SharedShellKind kind = Online::GetSharedShellKind();
    if (kind == Online::SHARED_SHELL_PAUSE)
    {
        const bool confirming = Online::GetSharedShellConfirmAction() !=
                                Online::SHARED_CONFIRM_NONE;
        const u8 maxSelection = confirming ? 1 : 2;
        const u8 p1Selection = std::clamp(Online::GetSharedShellSelection(0),
                                          (u8)0, maxSelection);
        const u8 p2Selection = std::clamp(Online::GetSharedShellSelection(1),
                                          (u8)0, maxSelection);
        AnmVm *items = confirming ? pauseMenu.menuSprites + 5
                                  : pauseMenu.menuSprites + 1;
        for (u8 option = 0; option <= maxSelection; ++option)
        {
            AnmVm &vm = items[option];
            const bool selected = p1Selection == option || p2Selection == option;
            vm.color.color = selected ? 0xffffffffu : 0x80303030u;
            vm.prevColor = vm.color;
            vm.offset = selected ? ZunVec3(-4.0f, -4.0f, 0.0f)
                                 : ZunVec3(0.0f, 0.0f, 0.0f);
        }
    }
    else if (kind == Online::SHARED_SHELL_RETRY)
    {
        const u8 p1Selection = std::clamp(Online::GetSharedShellSelection(0), (u8)0, (u8)1);
        const u8 p2Selection = std::clamp(Online::GetSharedShellSelection(1), (u8)0, (u8)1);
        for (u8 option = 0; option < 2; ++option)
        {
            AnmVm &vm = retryMenu.menuSprites[option + 2];
            const bool selected = p1Selection == option || p2Selection == option;
            vm.color.color = selected ? 0xffff8080u : 0x80808080u;
            vm.prevColor = vm.color;
            vm.offset = selected ? ZunVec3(-4.0f, -4.0f, 0.0f)
                                 : ZunVec3(0.0f, 0.0f, 0.0f);
        }
    }
}

static void DrawSharedShellMarkerText(AsciiManager &manager, const char *text,
                                      const ZunVec3 &position, u32 color)
{
    if (!text || !text[0]) return;
    AnmVm vm = manager.vm0;
    vm.visible = 1;
    vm.active = 1;
    vm.anchor = 3;
    vm.zWriteDisable = 1;
    vm.blendMode = 0;
    vm.scale.x = 0.65f;
    vm.scale.y = 0.65f;
    vm.prevScale = vm.scale;
    vm.pos = position;
    vm.prevPos = position;
    vm.offset = ZunVec3(0.0f, 0.0f, 0.0f);
    vm.useColor2 = 0;
    vm.prevUseColor2 = 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor)
    {
        if (*cursor < 1) continue;
        vm.sprite = &g_AnmManager->sprites[*cursor - 1];
        vm.color.color = color;
        vm.prevColor = vm.color;
        g_AnmManager->DrawNoRotation(&vm);
        vm.pos.x += 14.0f * vm.scale.x;
    }
}

static void DrawSharedShellMarkers(AsciiManager &manager)
{
    const Online::SharedShellKind kind = Online::GetSharedShellKind();
    if (kind == Online::SHARED_SHELL_NONE ||
        (!g_GameManager.isInPauseMenu && !g_GameManager.isInRetryMenu))
        return;
    const bool retry = kind == Online::SHARED_SHELL_RETRY;
    const bool confirmingPause = kind == Online::SHARED_SHELL_PAUSE &&
        Online::GetSharedShellConfirmAction() != Online::SHARED_CONFIRM_NONE;
    const i32 count = retry || confirmingPause ? 2 : 3;
    const AnmVm *items = retry ? manager.retryMenu.menuSprites + 2
        : (confirmingPause ? manager.pauseMenu.menuSprites + 5
                           : manager.pauseMenu.menuSprites + 1);
    for (i32 option = 0; option < count; ++option)
    {
        const bool p1 = Online::GetSharedShellVote(0) != 0 &&
                        Online::GetSharedShellSelection(0) == (u8)option;
        const bool p2 = Online::GetSharedShellVote(1) != 0 &&
                        Online::GetSharedShellSelection(1) == (u8)option;
        if (!p1 && !p2) continue;

        char label[8] = {};
        if (p1 && p2)
            SDL_strlcpy(label, "P1 P2", sizeof(label));
        else if (p1)
            SDL_strlcpy(label, "P1", sizeof(label));
        else
            SDL_strlcpy(label, "P2", sizeof(label));

        const AnmVm &item = items[option];
        const f32 scale = std::max(0.5f, std::abs(item.scale.x));
        const f32 width = item.sprite ? item.sprite->widthPx * scale : 64.0f;
        const ZunVec3 position(item.pos.x + item.offset.x + width * 0.5f + 12.0f,
                               item.pos.y + item.offset.y - 6.0f, 0.02f);
        DrawSharedShellMarkerText(manager, label, position,
                                  p1 && p2 ? 0xffd8f0ffu : (p1 ? 0xffb8d8ffu : 0xffb8ffd0u));
    }
}

static bool MobileMenuVmContainsPoint(const AnmVm &vm, f32 x, f32 y)
{
    if (!vm.active || !vm.visible || !vm.sprite) return false;
    const f32 scaleX = std::max(0.001f, std::abs(vm.scale.x));
    const f32 scaleY = std::max(0.001f, std::abs(vm.scale.y));
    const f32 halfW = std::max(36.0f, vm.sprite->widthPx * scaleX * 0.5f);
    const f32 halfH = std::max(14.0f, vm.sprite->heightPx * scaleY * 0.5f);
    const f32 posX = vm.pos.x + vm.offset.x;
    const f32 posY = vm.pos.y + vm.offset.y;
    f32 left = posX - halfW;
    f32 right = posX + halfW;
    f32 top = posY - halfH;
    f32 bottom = posY + halfH;
    if (vm.anchor & 1)
    {
        left = posX;
        right = posX + halfW * 2.0f;
    }
    if (vm.anchor & 2)
    {
        top = posY;
        bottom = posY + halfH * 2.0f;
    }
    return x >= left - 18.0f && x <= right + 18.0f &&
           y >= top - 10.0f && y <= bottom + 10.0f;
}

static i32 MobileHitMenuVms(AnmVm *items, i32 count, f32 x, f32 y)
{
    for (i32 i = 0; i < count; ++i)
    {
        if (MobileMenuVmContainsPoint(items[i], x, y)) return i;
    }
    return -1;
}

static void InjectMobileMenuButton(u16 button)
{
    if (Online::IsNetworkSession())
    {
        Online::QueueSharedShellButton(button);
        return;
    }
    g_LastFrameRawInput &= ~button;
    g_CurFrameRawInput |= button;
}

void PauseMenu::HandleMobileTouch(MobileUi::OverlayTouchAction action, f32 gameX, f32 gameY)
{
    if (action == MobileUi::OVERLAY_TOUCH_BACK)
    {
        if (Online::IsNetworkSession())
        {
            Online::QueueSharedShellButton(TH_BUTTON_MENU);
            MobileDiagnostics::Log("mobile/pause", "two-finger withdraw shared vote");
            return;
        }
        if (this->curState >= 5 && this->curState <= 8)
        {
            this->curState = this->curState <= 6 ? 6 : 8;
            this->numFrames = std::max(this->numFrames, 4);
            InjectMobileMenuButton(TH_BUTTON_SELECTMENU);
            MobileDiagnostics::Log("mobile/pause", "two-finger cancel-confirm state=%d",
                                   this->curState);
        }
        else if (this->curState >= 1 && this->curState <= 3)
        {
            InjectMobileMenuButton(TH_BUTTON_MENU);
            MobileDiagnostics::Log("mobile/pause", "two-finger resume state=%d",
                                   this->curState);
        }
        return;
    }
    if (action != MobileUi::OVERLAY_TOUCH_TAP) return;

    i32 hit = -1;
    if (this->curState >= 1 && this->curState <= 3)
    {
        hit = MobileHitMenuVms(&this->menuSprites[1], 3, gameX, gameY);
        if (hit >= 0)
        {
            if (Online::IsNetworkSession())
                Online::QueueSharedShellSelection((u8)hit);
            else
                this->curState = hit + 1;
            InjectMobileMenuButton(TH_BUTTON_SELECTMENU);
        }
    }
    else if (this->curState == 5 || this->curState == 6 ||
             this->curState == 7 || this->curState == 8)
    {
        hit = MobileHitMenuVms(&this->menuSprites[5], 2, gameX, gameY);
        if (hit >= 0)
        {
            const i32 base = this->curState <= 6 ? 5 : 7;
            if (!Online::IsNetworkSession()) this->curState = base + hit;
            InjectMobileMenuButton(TH_BUTTON_SELECTMENU);
        }
    }
    MobileDiagnostics::Log("mobile/pause", "tap state=%d hit=%d at=(%.1f,%.1f)",
                           this->curState, hit, gameX, gameY);
}

void RetryMenu::HandleMobileTouch(MobileUi::OverlayTouchAction action, f32 gameX, f32 gameY)
{
    i32 hit = -1;
    if (action == MobileUi::OVERLAY_TOUCH_BACK)
    {
        // Retry has no native cancel button; choose the safe "No" branch.
        if (Online::IsNetworkSession())
        {
            // Shared retry uses the primary list as the vote surface. A back
            // gesture only withdraws this player's vote; it must not mutate
            // the native animation state on one device.
            Online::QueueSharedShellButton(TH_BUTTON_MENU);
        }
        else
        {
            this->curState = 2;
            this->numFrames = std::max(this->numFrames, 30);
            InjectMobileMenuButton(TH_BUTTON_SELECTMENU);
        }
        MobileDiagnostics::Log("mobile/retry", "two-finger choose-no");
        return;
    }
    if (action == MobileUi::OVERLAY_TOUCH_TAP &&
        (this->curState == 1 || this->curState == 2))
    {
        hit = MobileHitMenuVms(&this->menuSprites[2], 2, gameX, gameY);
        if (hit >= 0)
        {
            if (Online::IsNetworkSession())
                Online::QueueSharedShellSelection((u8)hit);
            else
                this->curState = hit + 1;
            if (this->curState == 2) this->numFrames = std::max(this->numFrames, 30);
            InjectMobileMenuButton(TH_BUTTON_SELECTMENU);
        }
    }
    MobileDiagnostics::Log("mobile/retry", "tap state=%d hit=%d at=(%.1f,%.1f)",
                           this->curState, hit, gameX, gameY);
}

u32 AsciiManager::OnDrawMenus(AsciiManager *arg)
{
    arg->DrawStrings();
    arg->numStrings = 0;
    arg->pauseMenu.OnDraw();
    arg->retryMenu.OnDraw();
    DrawSharedShellMarkers(*arg);
    if (arg->vm.anmFileIdx != 0)
    {
        g_AnmManager->DrawNoRotation(&arg->vm);
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 AsciiManager::OnDrawPopups(AsciiManager *arg)
{
    arg->DrawPopups();
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

void AsciiManager::InitializeVms()
{
    memset(&this->vm1, 0, sizeof(AnmVm));
    memset(&this->vm0, 0, sizeof(AnmVm));
    memset(&this->strings, 0, sizeof(this->strings));
    memset(&this->pauseMenu, 0, sizeof(PauseMenu));
    memset(&this->retryMenu, 0, sizeof(RetryMenu));
    memset(&this->popups, 0, sizeof(this->popups));
    this->numStrings = 0;
    this->isGui = 0;
    this->isSelected = 0;
    this->nextPopupIndex1 = 0;
    this->nextPopupIndex2 = 0;
    this->unused_74e4 = 0;
    this->color = 0xffffffff;
    this->scale.x = 1.0f;
    this->scale.y = 1.0f;
    this->vm1.anchor = 3;
    g_AnmManager->InitializeAndSetActiveSprite(&this->vm1, 0);
    g_AnmManager->InitializeAndSetActiveSprite(&this->vm0, 32);
    this->vm1.pos.z = 0.1f;
    this->isSelected = 0;
    this->fontSpacing = 14;
    this->SetFadeState(this->uiFadeState);
}

void AsciiManager::InitializeOtherVms()
{
    g_AnmManager->SetAnmIdxAndExecuteScript(&this->cherryGauge, ANM_OFFSET_CHERRY_GAUGE);
    g_AnmManager->SetAnmIdxAndExecuteScript(&this->cherryDigit, ANM_OFFSET_CHERRY_DIGIT);
    g_AnmManager->SetAnmIdxAndExecuteScript(&this->cherryBorderActive, ANM_OFFSET_CHERRY_BORDER);
    g_AnmManager->SetAnmIdxAndExecuteScript(&this->bossMarkers[0], ANM_OFFSET_BOSS_MARKER);
    g_AnmManager->SetAnmIdxAndExecuteScript(&this->bossMarkers[1], ANM_OFFSET_BOSS_MARKER);
    g_AnmManager->SetAnmIdxAndExecuteScript(&this->bossMarkers[2], ANM_OFFSET_BOSS_MARKER);
    g_AnmManager->SetAnmIdxAndExecuteScript(&this->bossMarkers[3], ANM_OFFSET_BOSS_MARKER);
}

ZunResult AsciiManager::AddedCallback(AsciiManager *arg)
{
    memset(arg, 0, sizeof(AsciiManager));
    if (g_AnmManager->LoadAnms(ANM_FILE_ASCII, "data/ascii.anm", ANM_OFFSET_ASCII) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    if (g_AnmManager->LoadAnms(ANM_FILE_CAPTURE, "data/capture.anm", ANM_OFFSET_CAPTURE) !=
        ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    arg->InitializeVms();
    arg->InitializeOtherVms();
    return ZUN_SUCCESS;
}

ZunResult AsciiManager::DeletedCallback(AsciiManager *arg)
{
    (void)arg;

    g_AnmManager->ReleaseAnm(1);
    g_AnmManager->ReleaseAnm(2);
    g_AnmManager->ReleaseAnm(4);
    g_AnmManager->ReleaseAnm(3);
    return ZUN_SUCCESS;
}

ZunResult AsciiManager::RegisterChain()
{
    AsciiManager *mgr = &g_AsciiManager;
    g_AsciiManagerCalcChain.callback = (ChainCallback)OnUpdate;
    g_AsciiManagerCalcChain.addedCallback = NULL;
    g_AsciiManagerCalcChain.deletedCallback = NULL;
    g_AsciiManagerCalcChain.addedCallback = (ChainLifecycleCallback)AddedCallback;
    g_AsciiManagerCalcChain.deletedCallback = (ChainLifecycleCallback)DeletedCallback;
    g_AsciiManagerCalcChain.arg = mgr;

    if (g_Chain.AddToCalcChain(&g_AsciiManagerCalcChain, 1))
    {
        return ZUN_ERROR;
    }

    g_AsciiManagerOnDrawMenusChain.callback = (ChainCallback)OnDrawMenus;
    g_AsciiManagerOnDrawMenusChain.addedCallback = NULL;
    g_AsciiManagerOnDrawMenusChain.deletedCallback = NULL;
    g_AsciiManagerOnDrawMenusChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_AsciiManagerOnDrawMenusChain, 16);
    g_AsciiManagerOnDrawPopupsChain.callback = (ChainCallback)OnDrawPopups;
    g_AsciiManagerOnDrawPopupsChain.addedCallback = NULL;
    g_AsciiManagerOnDrawPopupsChain.deletedCallback = NULL;
    g_AsciiManagerOnDrawPopupsChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_AsciiManagerOnDrawPopupsChain, 11);
    return ZUN_SUCCESS;
}

void AsciiManager::CutChain()
{
    g_Chain.Cut(&g_AsciiManagerCalcChain);
    g_Chain.Cut(&g_AsciiManagerOnDrawMenusChain);
    g_Chain.Cut(&g_AsciiManagerOnDrawPopupsChain);
}

void AsciiManager::AddString(ZunVec3 *pos, const char *text)
{
    if (this->numStrings >= 256)
    {
        return;
    }

    AsciiManagerString *curString = &this->strings[this->numStrings];
    const std::string localized = Localization::Translate(text);

    this->numStrings++;

    std::strncpy(curString->text, localized.c_str(), sizeof(curString->text) - 1);
    curString->text[sizeof(curString->text) - 1] = '\0';
    curString->pos = *pos;
    curString->color = this->color;
    curString->scale.x = this->scale.x;
    curString->scale.y = this->scale.y;
    curString->isGui = this->isGui;

    if (g_Supervisor.cfg.loaded | g_Supervisor.cfg.unused)
    {
        curString->isSelected = this->isSelected;
    }
    else
    {
        curString->isSelected = 0;
    }
}

void AsciiManager::AddFormatText(AsciiManager *manager, ZunVec3 *pos, const char *fmt, ...)
{
    char str[508];
    va_list args;

    va_start(args, fmt);
    vsprintf(str, fmt, args);
    manager->AddString(pos, str);

    va_end(args);
}

void AsciiManager::DrawStrings()
{
    f32 charWidth;
    i32 guiString;
    char *text;
    AsciiManagerString *string;
    i32 i;

    std::stable_sort(
        this->strings, this->strings + this->numStrings,
        [](const AsciiManagerString &a, const AsciiManagerString &b) { return a.isGui < b.isGui; });
    guiString = 1;
    string = this->strings;
    this->vm0.visible = 1;
    this->vm0.anchor = 3;
    for (i = 0; i < this->numStrings; i++, string++)
    {
        this->vm0.pos = string->pos;
        text = string->text;
        this->vm0.scale.x = string->scale.x;
        this->vm0.scale.y = string->scale.y;
        this->vm0.prevScale = this->vm0.scale;
        charWidth = (f32)this->fontSpacing * string->scale.x;
        if (guiString != string->isGui)
        {
            guiString = string->isGui;
            g_AnmManager->Flush();
            if (guiString != 0)
            {
                g_Supervisor.viewport.x = g_GameManager.arcadeRegionTopLeftPos.x;
                g_Supervisor.viewport.y = g_GameManager.arcadeRegionTopLeftPos.y;
                g_Supervisor.viewport.width = g_GameManager.arcadeRegionSize.x;
                g_Supervisor.viewport.height = g_GameManager.arcadeRegionSize.y;
                g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
            }
            else
            {
                g_Supervisor.viewport.x = 0;
                g_Supervisor.viewport.y = 0;
                g_Supervisor.viewport.width = 640;
                g_Supervisor.viewport.height = 480;
                g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
            }
        }
        while (*(u8 *)text != 0)
        {
            if (*(u8 *)text == '\n')
            {
                this->vm0.pos.y += 16.0f * string->scale.y;
                this->vm0.pos.x = string->pos.x;
            }
            else if (*(u8 *)text == ' ')
            {
                this->vm0.pos.x += charWidth;
            }
            else
            {
                if (!string->isSelected)
                {
                    this->vm0.sprite = &g_AnmManager->sprites[(u8)*text - 1];
                    this->vm0.color.color = string->color;
                }
                else
                {
                    this->vm0.sprite = &g_AnmManager->sprites[(u8)*text + 124];
                    this->vm0.color.color = 0xffffffff;
                }
                this->vm0.prevColor = this->vm0.color;
                g_AnmManager->DrawNoRotation(&this->vm0);
                this->vm0.pos.x += charWidth;
            }
            text++;
        }
    }

    g_AnmManager->Flush();
    g_Supervisor.viewport.x = 0;
    g_Supervisor.viewport.y = 0;
    g_Supervisor.viewport.width = 640;
    g_Supervisor.viewport.height = 480;
    g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
    Player *markerPlayer = GetPrimaryActivePlayer();
    f32 interpPlayerX = utils::Lerp(markerPlayer->prevPositionCenter.x,
                                    markerPlayer->positionCenter.x, g_RenderAlpha);
    for (i = 0; i < 4; i++)
    {
        Enemy *boss = g_EnemyManager.bosses[i];
        if (boss && !boss->hasNoCollision)
        {
            f32 interpBossX = utils::Lerp(boss->prevPos.x, boss->pos.x, g_RenderAlpha);
            this->bossMarkers[i].pos.x = interpBossX + 32.0f;
            this->bossMarkers[i].pos.y = 472.0f;
            this->bossMarkers[i].pos.z = 0.0f;
        }
        else
        {
            this->bossMarkers[i].pos.x = -999.0f;
        }

        if (this->bossMarkers[i].pos.x >= 56.0f && this->bossMarkers[i].pos.x <= 392.0f)
        {
            charWidth = fabsf(this->bossMarkers[i].pos.x - 32.0f - interpPlayerX);
            if (charWidth < 64.0f)
            {
                this->bossMarkers[i].color.bytes.a = charWidth * 128.0f / 64.0f + 48.0f;
            }
            else
            {
                this->bossMarkers[i].color.bytes.a = 176;
            }
            if (this->bossDamageTint[i] != 0)
            {
                this->bossMarkers[i].color.bytes.a = 128;
                this->bossMarkers[i].color.bytes.r = 64;
                this->bossMarkers[i].color.bytes.g = 64;
                this->bossMarkers[i].color.bytes.b = 255;
            }
            else
            {
                this->bossMarkers[i].color.bytes.r = 255;
                this->bossMarkers[i].color.bytes.g = 255;
                this->bossMarkers[i].color.bytes.b = 255;
            }
            this->bossMarkers[i].prevColor = this->bossMarkers[i].color;
            g_AnmManager->DrawNoRotation(&this->bossMarkers[i]);
        }
    }
}

void AsciiManager::CreatePopup1(ZunVec3 *pos, i32 value, u32 color)
{
    i32 characterCount;
    AsciiManagerPopup *popup;

    if (this->nextPopupIndex1 >= 720)
    {
        this->nextPopupIndex1 = 0;
    }
    popup = this->popups + this->nextPopupIndex1;
    popup->inUse = 1;
    characterCount = 0;
    if (value >= 0)
    {
        while (value != 0)
        {
            popup->digits[characterCount++] = (char)(value % 10);
            value /= 10;
        }
    }
    else
    {
        popup->digits[characterCount++] = '\n';
    }
    if (characterCount == 0)
    {
        popup->digits[characterCount++] = 0;
    }
    popup->characterCount = (u8)characterCount;
    popup->color = color;
    popup->timer = 0;
    popup->pos = *pos;
    popup->pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
    popup->pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
    popup->prevPos = popup->pos;
    this->nextPopupIndex1++;
}

void AsciiManager::CreatePopup2(ZunVec3 *pos, i32 value, u32 color)
{
    i32 characterCount;
    AsciiManagerPopup *popup;

    if (this->nextPopupIndex2 >= 3)
    {
        this->nextPopupIndex2 = 0;
    }
    popup = &this->popups[this->nextPopupIndex2 + 720];
    popup->inUse = 1;
    characterCount = 0;
    if (value >= 0)
    {
        while (value != 0)
        {
            popup->digits[characterCount++] = (char)(value % 10);
            value /= 10;
        }
    }
    else
    {
        popup->digits[characterCount++] = '\n';
    }
    if (characterCount == 0)
    {
        popup->digits[characterCount++] = 0;
    }
    popup->characterCount = (u8)characterCount;
    popup->color = color;
    popup->timer = 0;
    popup->pos = *pos;
    popup->pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
    popup->pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
    popup->prevPos = popup->pos;
    this->nextPopupIndex2++;
}

i32 PauseMenu::OnUpdate(bool suppressInput)
{
    u32 i;

    this->UpdatePrev();

    if (!suppressInput && WAS_PRESSED_RAW(TH_BUTTON_MENU) && this->curState != 4)
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
        this->curState = 4;
        for (i = 0; i < 10; i++)
        {
            if (this->menuSprites[i].visible)
            {
                this->menuSprites[i].pendingInterrupt = 2;
            }
        }
        this->numFrames = 0;
        this->menuBackground.pendingInterrupt = 1;
    }
    if (!suppressInput && WAS_PRESSED_RAW(TH_BUTTON_Q) && this->curState != 9)
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
        this->curState = 9;
        for (i = 0; i < 10; i++)
        {
            if (this->menuSprites[i].visible)
            {
                this->menuSprites[i].pendingInterrupt = 2;
            }
        }
        this->numFrames = 0;
    }
    if (!suppressInput && !g_GameManager.replay && WAS_PRESSED_RAW(TH_BUTTON_RESET) && this->curState != 9)
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
        this->curState = 10;
        for (i = 0; i < 10; i++)
        {
            if (this->menuSprites[i].visible)
            {
                this->menuSprites[i].pendingInterrupt = 2;
            }
        }
        this->numFrames = 0;
    }
    switch (this->curState)
    {
    case 0:
        for (i = 0; i < 10; i++)
        {
            g_AnmManager->SetAnmIdxAndExecuteScript(&this->menuSprites[i],
                                                    i + ANM_OFFSET_RETRY_MENU);
        }
        for (i = 0; (i32)i < 4; i++)
        {
            this->menuSprites[i].pendingInterrupt = 1;
        }
        g_AnmManager->SetActiveSprite(this->menuSprites + 7, g_GameManager.difficulty + 269);
        if (!g_GameManager.practice)
        {
            this->menuSprites[8].SetInvisible();
        }
        if (g_GameManager.defaultCfg->slowMode == 0)
        {
            this->menuSprites[9].SetInvisible();
        }
        if (g_GameManager.replay)
        {
            this->menuSprites[3].currentInstruction = NULL;
        }
        this->curState++;
        this->numFrames = 0;
        if ((g_Supervisor.flags >> 1 & 1) != 0)
        {
            g_AnmManager->SetAnmIdxAndExecuteScript(&this->menuBackground, ANM_OFFSET_MENU_BG);
            if (g_AnmManager->CreateScreenshotTexture(
                    this->menuBackground.sprite->startPixelInclusive.x,
                    this->menuBackground.sprite->startPixelInclusive.y,
                    this->menuBackground.sprite->heightPx, this->menuBackground.sprite->widthPx))
            {
                this->curState = 0;
                return 0;
            }
            g_AnmManager->TakeScreenshotIfRequested();
            this->menuBackground.pos.x = 32.0f;
            this->menuBackground.pos.y = 16.0f;
            this->menuBackground.pos.z = 0.0f;
        }
    case 1:
        this->menuSprites[1].color.color = 0xffffffff;
        this->menuSprites[3].color.color = 0x80303030;
        this->menuSprites[2].color.color = 0x80303030;
        this->menuSprites[1].offset = ZunVec3(-4.0f, -4.0f, 0.0f);
        this->menuSprites[3].offset = ZunVec3(0.0f, 0.0f, 0.0f);
        this->menuSprites[2].offset = this->menuSprites[3].offset;
        if (this->numFrames >= 4)
        {
            if (!g_GameManager.replay)
            {
                if (WAS_PRESSED_RAW(TH_BUTTON_UP))
                {
                    this->curState = 3;
                    g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
                }
            }
            else if (WAS_PRESSED_RAW(TH_BUTTON_UP))
            {
                this->curState = 2;
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_DOWN))
            {
                this->curState = 2;
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
            {
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
                for (i = 0; (i32)i < 4; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 2;
                }
                this->curState = 4;
                this->numFrames = 0;
                this->menuBackground.pendingInterrupt = 1;
            }
        }
        break;
    case 2:
        this->menuSprites[3].color.color = 0x80303030;
        this->menuSprites[1].color.color = 0x80303030;
        this->menuSprites[2].color.color = 0xffffffff;
        this->menuSprites[3].offset = ZunVec3(0.0f, 0.0f, 0.0f);
        this->menuSprites[1].offset = this->menuSprites[3].offset;
        this->menuSprites[2].offset = ZunVec3(-4.0f, -4.0f, 0.0f);
        if (this->numFrames >= 4)
        {
            if (WAS_PRESSED_RAW(TH_BUTTON_UP))
            {
                this->curState = 1;
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (g_GameManager.replay)
            {
                if (WAS_PRESSED_RAW(TH_BUTTON_DOWN))
                {
                    this->curState = 1;
                    g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
                }
            }
            else
            {
                if (WAS_PRESSED_RAW(TH_BUTTON_DOWN))
                {
                    this->curState = 3;
                    g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
                }
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
            {
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
                for (i = 0; (i32)i < 4; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 2;
                }
                for (; (i32)i < 7; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 1;
                }
                this->curState = 6;
                this->numFrames = 0;
            }
        }
        break;
    case 3:
        this->menuSprites[2].color.color = 0x80303030;
        this->menuSprites[1].color.color = 0x80303030;
        this->menuSprites[3].color.color = 0xffffffff;
        this->menuSprites[2].offset = ZunVec3(0.0f, 0.0f, 0.0f);
        this->menuSprites[1].offset = this->menuSprites[2].offset;
        this->menuSprites[3].offset = ZunVec3(-4.0f, -4.0f, 0.0f);
        if (this->numFrames >= 4)
        {
            if (WAS_PRESSED_RAW(TH_BUTTON_UP))
            {
                this->curState = 2;
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_DOWN))
            {
                this->curState = 1;
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
            {
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
                for (i = 0; (i32)i < 4; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 2;
                }
                for (; (i32)i < 7; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 1;
                }
                this->curState = 8;
                this->numFrames = 0;
            }
        }
        break;
    case 4:
        if (this->numFrames >= 20)
        {
            this->curState = 0;
            g_GameManager.isInPauseMenu = 0;
            for (i = 0; i < 10; i++)
            {
                this->menuSprites[i].SetInvisible();
            }
            if (g_GameManager.currentStage != 6 || g_Gui.frameCounter >= 300)
            {
                g_SoundPlayer.PushCommand(AUDIO_UNPAUSE, 0, (char *)"UnPause");
            }
            g_Supervisor.currentTime = SDL_GetTicks();
            g_GameWindow.ResetAccumulator();
        }
        break;
    case 5:
    case 7:
        this->menuSprites[5].color.color = 0xffff8080;
        this->menuSprites[6].color.color = 0x80808080;
        this->menuSprites[5].offset = ZunVec3(-4.0f, -4.0f, 0.0f);
        this->menuSprites[6].offset = ZunVec3(0.0f, 0.0f, 0.0f);
        if (this->numFrames >= 4)
        {
            if (WAS_PRESSED_RAW(TH_BUTTON_UP) || WAS_PRESSED_RAW(TH_BUTTON_DOWN))
            {
                if (this->curState == 5)
                {
                    this->curState = 6;
                }
                else
                {
                    this->curState = 8;
                }
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
            {
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
                for (i = 4; (i32)i < 7; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 2;
                }
                if (this->curState == 5)
                {
                    this->curState = 9;
                }
                else
                {
                    this->curState = 10;
                }
                this->numFrames = 0;
            }
        }
        break;
    case 6:
    case 8:
        this->menuSprites[5].color.color = 0x80808080;
        this->menuSprites[6].color.color = 0xffff8080;
        this->menuSprites[5].offset = ZunVec3(0.0f, 0.0f, 0.0f);
        this->menuSprites[6].offset = ZunVec3(-4.0f, -4.0f, 0.0f);
        if (this->numFrames >= 4)
        {
            if (WAS_PRESSED_RAW(TH_BUTTON_UP) || WAS_PRESSED_RAW(TH_BUTTON_DOWN))
            {
                if (this->curState == 6)
                {
                    this->curState = 5;
                }
                else
                {
                    this->curState = 7;
                }
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
            {
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
                for (i = 0; (i32)i < 4; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 1;
                }
                for (; (i32)i < 7; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 2;
                }
                if (this->curState == 6)
                {
                    this->curState = 2;
                }
                else
                {
                    this->curState = 3;
                }
                this->numFrames = 0;
            }
        }
        break;
    case 9:
        if (this->numFrames >= 20)
        {
            this->curState = 0;
            g_GameManager.isInPauseMenu = 0;
            g_Supervisor.curState = 1;
            for (i = 0; i < 10; i++)
            {
                this->menuSprites[i].SetInvisible();
            }
            g_Supervisor.currentTime = SDL_GetTicks();
        }
        break;
    case 10:
        if (this->numFrames >= 20)
        {
            this->curState = 0;
            g_GameManager.isInPauseMenu = 0;
            g_Supervisor.curState = 10;
            for (i = 0; i < 10; i++)
            {
                this->menuSprites[i].SetInvisible();
            }
            g_Supervisor.currentTime = SDL_GetTicks();
        }
    }
    for (i = 0; i < 10; i++)
    {
        g_AnmManager->ExecuteScript(&this->menuSprites[i]);
    }
    if ((g_Supervisor.flags >> 1 & 1) != 0)
    {
        g_AnmManager->ExecuteScript(&this->menuBackground);
    }
    this->numFrames++;
    return 0;
}

void PauseMenu::OnDraw()
{
    u32 i;

    if (g_GameManager.isInPauseMenu)
    {
        g_AnmManager->Flush();
        g_Supervisor.viewport.x = (u32)g_GameManager.arcadeRegionTopLeftPos.x;
        g_Supervisor.viewport.y = (u32)g_GameManager.arcadeRegionTopLeftPos.y;
        g_Supervisor.viewport.width = (u32)g_GameManager.arcadeRegionSize.x;
        g_Supervisor.viewport.height = (u32)g_GameManager.arcadeRegionSize.y;
        g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
        // Pause labels are baked into the stock ANM sprites. Rewrite the same
        // texture regions for localized builds so the Japanese glyphs are
        // replaced rather than covered by a second overlay.
        DrawLocalizedMenuText(this->menuSprites + 1, "继续游戏", "CONTINUE");
        DrawLocalizedMenuText(this->menuSprites + 2, "重新开始", "RETRY");
        DrawLocalizedMenuText(this->menuSprites + 3, "返回标题", "QUIT TO TITLE");
        DrawLocalizedMenuText(this->menuSprites + 5, "是", "YES");
        DrawLocalizedMenuText(this->menuSprites + 6, "否", "NO");
        if ((g_Supervisor.flags >> 1 & 1) != 0 && this->curState != 0)
        {
            AnmVm local_25c = this->menuBackground;
            local_25c.zWriteDisable = 1;
            g_AnmManager->DrawNoRotation(&local_25c);
        }
        for (i = 0; i < 10; i++)
        {
            if (this->menuSprites[i].visible)
            {
                g_AnmManager->DrawNoRotation(this->menuSprites + i);
            }
        }
    }
}

i32 RetryMenu::OnUpdate(bool suppressInput)
{
    i32 i;
    const bool sharedShell =
        Online::IsSharedShellActive(Online::SHARED_SHELL_RETRY);

    this->UpdatePrev();

    // In a network game the host owns the retry/title decision.  Local
    // practice/replay/maximum-retry shortcuts would otherwise close the
    // shared menu before its two options have been drawn or voted on.
    if (!sharedShell && g_GameManager.practice)
    {
        g_GameManager.isInRetryMenu = 0;
        g_GameManager.globals->guiScore = g_GameManager.globals->score;
        g_Supervisor.curState = 6;
        return 1;
    }
    if (!sharedShell && g_GameManager.replay)
    {
        g_GameManager.isInRetryMenu = 0;
        g_Supervisor.curState = 7;
        g_GameManager.globals->guiScore = g_GameManager.globals->score;
        return 1;
    }
    if (!sharedShell &&
        ((i32)(u32)g_GameManager.globals->numRetries >= g_GameManager.maxRetries ||
         g_GameManager.difficulty >= 4))
    {
        g_GameManager.isInRetryMenu = 0;
        g_Supervisor.curState = 6;
        g_GameManager.globals->guiScore = g_GameManager.globals->score;
        return 1;
    }
    switch (this->curState)
    {
    case 0:
        if (this->numFrames == 0)
        {
            g_SoundPlayer.PushCommand(AUDIO_PAUSE, 0, "Pause");
            for (i = 0; i < 4; i++)
            {
                g_AnmManager->SetAnmIdxAndExecuteScript(&this->menuSprites[i], i + 264);
                this->menuSprites[i].pendingInterrupt = 1;
            }
            g_AnmManager->SetAnmIdxAndExecuteScript(&this->menuSprites[4], 268);
            g_AnmManager->SetActiveSprite(&this->menuSprites[4],
                                          g_GameManager.maxRetries + 262 -
                                              (u32)g_GameManager.globals->numRetries);
            this->menuSprites[4].pendingInterrupt = 1;
            if ((g_Supervisor.flags >> 1 & 1) != 0)
            {
                g_AnmManager->SetAnmIdxAndExecuteScript(&this->menuBackground, ANM_OFFSET_MENU_BG);
                if (g_AnmManager->CreateScreenshotTexture(
                        this->menuBackground.sprite->startPixelInclusive.x,
                        this->menuBackground.sprite->startPixelInclusive.y,
                        this->menuBackground.sprite->heightPx,
                        this->menuBackground.sprite->widthPx))
                {
                    this->curState = 0;
                    return 0;
                }
                g_AnmManager->TakeScreenshotIfRequested();
                this->menuBackground.pos.x = 32.0f;
                this->menuBackground.pos.y = 16.0f;
                this->menuBackground.pos.z = 0.0f;
            }
            g_Supervisor.UpdateTime();
        }
        if (8 < this->numFrames)
        {
            break;
        }
        this->curState += 2;
        this->numFrames = 0;
    case 1:
        this->menuSprites[2].color.color = 0xffff8080;
        this->menuSprites[3].color.color = 0x80808080;
        this->menuSprites[2].offset = ZunVec3(-4.0f, -4.0f, 0.0f);
        this->menuSprites[3].offset = ZunVec3(0.0f, 0.0f, 0.0f);
        if (this->numFrames >= 4)
        {
            if (WAS_PRESSED_RAW(TH_BUTTON_UP) || WAS_PRESSED_RAW(TH_BUTTON_DOWN))
            {
                this->curState = 2;
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
            {
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
                for (i = 0; i < 5; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 2;
                }
                this->curState = 3;
                this->menuBackground.pendingInterrupt = 1;
                this->numFrames = 0;
            }
        }
        break;
    case 2:
        this->menuSprites[3].color.color = 0xffff8080;
        this->menuSprites[2].color.color = 0x80808080;
        this->menuSprites[3].offset = ZunVec3(-4.0f, -4.0f, 0.0f);
        this->menuSprites[2].offset = ZunVec3(0.0f, 0.0f, 0.0f);
        if (this->numFrames >= 30)
        {
            if (WAS_PRESSED_RAW(TH_BUTTON_UP) || WAS_PRESSED_RAW(TH_BUTTON_DOWN))
            {
                this->curState = 1;
                g_SoundPlayer.PlaySoundByIdx(SOUND_0, 0);
            }
            if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
            {
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
                for (i = 0; i < 5; i++)
                {
                    this->menuSprites[i].pendingInterrupt = 2;
                }
                this->curState = 4;
                this->numFrames = 0;
            }
        }
        break;
    case 4:
        if (this->numFrames >= 20)
        {
            this->curState = 0;
            this->numFrames = 0;
            g_GameManager.isInRetryMenu = 0;
            g_Supervisor.curState = 6;
            for (i = 0; i < 5; i++)
            {
                this->menuSprites[i].SetInvisible();
            }
            g_GameManager.globals->guiScore = g_GameManager.globals->score;
            g_Supervisor.currentTime = SDL_GetTicks();
            return 0;
        }
        break;
    case 3:
        if (this->numFrames >= 30)
        {
            this->curState = 0;
            this->numFrames = 0;
            g_GameManager.isInRetryMenu = 0;
            for (i = 0; i < 5; i++)
            {
                this->menuSprites[i].SetInvisible();
            }
            g_GameManager.globals->numRetries++;
            g_GameManager.globals->guiScore = (u32)g_GameManager.globals->numRetries;
            g_GameManager.globals->guiScoreDifference = 0;
            g_GameManager.globals->score = g_GameManager.globals->guiScore;
            g_GameManager.SetLivesRemaining(g_GameManager.defaultCfg->lifeCount);
            g_GameManager.RegenerateGameIntegrityCsum();
            g_GameManager.SetBombsRemainingAndComputeCsum(g_Player.shooterData->initialBombs);
            g_GameManager.globals->grazeInStage = 0;
            g_GameManager.globals->pointItemsCollectedThisStage = 0;
            g_GameManager.globals->pointItemsCollectedForExtend = 0;
            g_GameManager.globals->currentPower = 0.0f;
            g_GameManager.RegenerateGameIntegrityCsum();
            // The stock retry branch only restores the global P1 counters.
            // Netplay keeps P2 in a separate resource/state lane, so restore
            // both registered players at the same shared commit frame.
            ResetPlayerForSharedRetry(0);
            if (Online::IsMultiplayerSession())
            {
                ResetPlayerForSharedRetry(1);
                ResetMultiplayerPlayerResources(1);
            }
            g_GameManager.globals->extendsFromPointItems = 0;
            g_GameManager.globals->nextNeededPointItemsForExtend = 50;
            g_GameManager.cherry = g_GameManager.globals->cherryStart;
            g_Gui.lifeDisplayUpdateFrames = 2;
            g_Gui.bombDisplayUpdateFrames = 2;
            g_Gui.grazeDisplayUpdateFrames = 2;
            g_Gui.pointDisplayUpdateFrames = 2;
            g_Gui.powerDisplayUpdateFrames = 2;
            IncrementCapped(
                &g_GameManager.plst.playDataByDifficulty[g_GameManager.difficulty].playCount,
                999999);
            IncrementCapped(&g_GameManager.plst.playDataByDifficulty[6].playCount, 999999);
            IncrementCapped(&g_GameManager.plst.playDataByDifficulty[g_GameManager.difficulty]
                                 .playCountPerShotType[g_GameManager.shotTypeAndCharacter],
                            999999);
            IncrementCapped(&g_GameManager.plst.playDataByDifficulty[6]
                                 .playCountPerShotType[g_GameManager.shotTypeAndCharacter],
                            999999);
            IncrementCapped(
                &g_GameManager.plst.playDataByDifficulty[g_GameManager.difficulty].retryCount,
                999999);
            IncrementCapped(&g_GameManager.plst.playDataByDifficulty[6].retryCount, 999999);
            g_SoundPlayer.PushCommand(AUDIO_UNPAUSE, 0, "UnPause");
            g_Supervisor.currentTime = SDL_GetTicks();
            g_GameWindow.ResetAccumulator();
            return 0;
        }
        break;
    }
    for (i = 0; i < 5; i++)
    {
        g_AnmManager->ExecuteScript(&this->menuSprites[i]);
    }
    if ((g_Supervisor.flags >> 1 & 1) != 0)
    {
        g_AnmManager->ExecuteScript(&this->menuBackground);
    }
    this->numFrames++;
    return 0;
}

void RetryMenu::OnDraw()
{
    i32 i;

    if (g_GameManager.isInRetryMenu)
    {
        g_AnmManager->Flush();
        g_Supervisor.viewport.x = g_GameManager.arcadeRegionTopLeftPos.x;
        g_Supervisor.viewport.y = g_GameManager.arcadeRegionTopLeftPos.y;
        g_Supervisor.viewport.width = g_GameManager.arcadeRegionSize.x;
        g_Supervisor.viewport.height = g_GameManager.arcadeRegionSize.y;
        g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
        DrawLocalizedMenuText(this->menuSprites + 2, "重新开始", "RETRY");
        DrawLocalizedMenuText(this->menuSprites + 3, "返回标题", "QUIT TO TITLE");
        if ((g_Supervisor.flags >> 1 & 1) != 0 && (this->curState != 0 || 2 < this->numFrames))
        {
            g_AnmManager->DrawNoRotation(&this->menuBackground);
        }
        if (this->curState == 1 || this->curState == 2)
        {
            g_AnmManager->DrawNoRotation(&this->menuSprites[4]);
        }
        for (i = 0; i < 5; i++)
        {
            if (this->menuSprites[i].visible)
            {
                g_AnmManager->DrawNoRotation(&this->menuSprites[i]);
            }
        }
    }
}

void AsciiManager::DrawPopups()
{
    i32 divisor;
    bool hasNonZeroDigit;
    i32 xInc;
    i32 cherry;
    u8 *digits;
    i32 i;
    i32 j;
    f32 dx;
    f32 dy;
    i32 alpha;
    AsciiManagerPopup *popup;

    popup = this->popups;
    if (!g_Supervisor.cfg.disableFog)
    {
        g_Supervisor.DisableFog();
    }
    g_Supervisor.gfxDevice->SetDepthFunc(DEPTH_FUNC_ALWAYS);

    for (i = 0; i < 723; i++, popup++)
    {
        if (!popup->inUse)
        {
            continue;
        }

        ZunVec3 drawPos = popup->prevPos.Lerp(popup->pos, g_RenderAlpha);
        this->vm1.pos.x = drawPos.x - (f32)(popup->characterCount << 2);
        this->vm1.pos.y = drawPos.y;
        this->vm1.color.color = popup->color;
        this->vm1.prevColor = this->vm1.color;

        Player *popupPlayer = GetClosestActivePlayer(&popup->pos);
        dx = popupPlayer->positionCenter.x - popup->pos.x;
        dy = popupPlayer->positionCenter.y - popup->pos.y;
        alpha = (i32)(dx * dx + dy * dy);

        if (alpha > 4096)
        {
            alpha = 208;
        }
        else
        {
            if (alpha > 1024)
            {
                alpha = (alpha - 1024) * 128 / 3072 + 80;
            }
            else
            {
                alpha = 80;
            }
        }

        digits = &popup->digits[popup->characterCount - 1];

        for (j = popup->characterCount; j > 0; j--)
        {
            if (popup->timer < 52 || *digits == 10)
            {
                this->vm1.sprite = &g_AnmManager->sprites[*digits];
                this->vm1.color.bytes.a = alpha;
            }
            else if (popup->timer < 56)
            {
                this->vm1.sprite = &g_AnmManager->sprites[*digits + 11];
                this->vm1.color.bytes.a = alpha;
            }
            else
            {
                this->vm1.sprite = &g_AnmManager->sprites[*digits + 21];
                this->vm1.color.bytes.a = alpha;
            }
            this->vm1.prevColor = this->vm1.color;

            g_AnmManager->DrawNoRotation(&this->vm1);
            this->vm1.pos.x += 8.0f;
            digits--;
        }
    }

    if (this->cherryGauge.visible)
    {
        divisor = 100000;
        hasNonZeroDigit = false;
        cherry = g_GameManager.cherry - g_GameManager.globals->cherryStart;

        g_AnmManager->DrawNoRotation(&this->cherryGauge);

        this->cherryDigit.pos.x = this->cherryGauge.pos.x + 40.0f + 6.0f;
        this->cherryDigit.pos.y = this->cherryGauge.pos.y + 11.0f;
        this->cherryDigit.pos.z = this->cherryGauge.pos.z;
        this->cherryDigit.color.bytes.a = this->cherryGauge.color.bytes.a;
        this->cherryDigit.prevColor = this->cherryDigit.color;

        if (g_GameManager.IsCherryAtMax())
        {
            this->cherryDigit.color.bytes.r = 255;
            this->cherryDigit.color.bytes.g = 208;
            this->cherryDigit.color.bytes.b = 128;
        }
        else
        {
            if (cherry >= 50000)
            {
                this->cherryDigit.color.bytes.r = 255;
                this->cherryDigit.color.bytes.g = 255;
                this->cherryDigit.color.bytes.b = 128;
            }
            else
            {
                this->cherryDigit.color.bytes.r = 255;
                this->cherryDigit.color.bytes.g = 255;
                this->cherryDigit.color.bytes.b = 255;
            }
        }

        // ZUN bug: When Cherry exceeds 1 million, it'll result in overflowing
        // the display, causing it to no longer display correctly. Strangely,
        // ZUN actually did fix this bug for displaying CherryMax, and he
        // totally could have just done the same thing for Cherry, but he
        // didn't. Weird...
        for (i = 0; i < 6; i++, divisor /= 10)
        {
            j = cherry / divisor;
            cherry %= divisor;
            if (j != 0)
            {
                hasNonZeroDigit = true;
            }
            if (hasNonZeroDigit || divisor == 1)
            {
                g_AnmManager->SetActiveSprite(&this->cherryDigit, j + 132);
                this->cherryDigit.prevColor = this->cherryDigit.color;
                g_AnmManager->DrawNoRotation(&this->cherryDigit);
            }
            this->cherryDigit.pos.x += 7.0f;
        }

        hasNonZeroDigit = false;
        cherry = g_GameManager.cherryMax - g_GameManager.globals->cherryStart;

        this->cherryDigit.color.bytes.r = 240;
        this->cherryDigit.color.bytes.g = 208;
        this->cherryDigit.color.bytes.b = 224;
        this->cherryDigit.pos.x += 9.0f;

        if (cherry < 1000000)
        {
            divisor = 100000;
            i = 6;
        }
        else
        {
            divisor = 1000000;
            i = 7;
        }

        for (; i > 0; i--, divisor /= 10)
        {
            j = cherry / divisor;
            cherry %= divisor;
            if (j != 0)
            {
                hasNonZeroDigit = true;
            }
            if (hasNonZeroDigit || divisor == 1)
            {
                g_AnmManager->SetActiveSprite(&this->cherryDigit, j + 132);
                this->cherryDigit.prevColor = this->cherryDigit.color;
                g_AnmManager->DrawNoRotation(&this->cherryDigit);
            }
            this->cherryDigit.pos.x += 7.0f;
        }

        this->cherryDigit.scale.x = 1.0f;
        this->cherryDigit.scale.y = 1.0f;
        this->cherryDigit.prevScale = this->cherryDigit.scale;
        hasNonZeroDigit = false;
        this->cherryDigit.pos.x = this->cherryGauge.pos.x + 40.0f + 6.0f + 7.0f;
        this->cherryDigit.pos.y = this->cherryGauge.pos.y + 2.0f;

        cherry = g_GameManager.cherryPlus - g_GameManager.globals->cherryStart;

        Player *borderPlayer = GetPrimaryActivePlayer();
        if (borderPlayer->hasBorder)
        {
            this->cherryDigit.color.bytes.r = 255;
            divisor = cherry % 4000;
            if (divisor >= 2000)
            {
                divisor = 4000 - divisor;
            }
            this->cherryDigit.color.bytes.g = cherry * 192 / 50000 + divisor * 64 / 2000;
            this->cherryDigit.color.bytes.b = cherry * 192 / 50000 + divisor * 64 / 2000;
            this->cherryDigit.scale.x = 1.41f;
            this->cherryDigit.scale.y = 1.41f;
            this->cherryDigit.prevScale = this->cherryDigit.scale;
            xInc = 10;
            this->cherryDigit.pos.x += 2.0f;
            this->cherryDigit.pos.y -= 2.0f;
        }
        else
        {
            this->cherryDigit.color.bytes.r = 192;
            this->cherryDigit.color.bytes.g = 128;
            this->cherryDigit.color.bytes.b = 0xb0;
            xInc = 7;
        }

        for (divisor = 10000, i = 0; i < 5; i++, divisor /= 10)
        {
            j = cherry / divisor;
            cherry %= divisor;
            if (j != 0)
            {
                hasNonZeroDigit = true;
            }
            if (hasNonZeroDigit || divisor == 1)
            {
                g_AnmManager->SetActiveSprite(&this->cherryDigit, j + 132);
                this->cherryDigit.prevColor = this->cherryDigit.color;
                g_AnmManager->DrawNoRotation(&this->cherryDigit);
            }
            this->cherryDigit.pos.x += (f32)xInc;
        }

        this->cherryDigit.scale.x = 1.0f;
        this->cherryDigit.scale.y = 1.0f;
        this->cherryDigit.prevScale = this->cherryDigit.scale;

        if (borderPlayer->hasBorder == BORDER_ACTIVE)
        {
            this->cherryBorderActive.pos = this->cherryGauge.pos;
            this->cherryBorderActive.pos.x += 24.0f;
            this->cherryBorderActive.pos.y += 8.0f;
            this->cherryBorderActive.color.color = this->cherryGauge.color.color;
            this->cherryBorderActive.prevColor.color = this->cherryGauge.prevColor.color;
            g_AnmManager->DrawNoRotation(&this->cherryBorderActive);
        }
    }
}
