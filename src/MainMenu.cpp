#include "MainMenu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "AnmIdx.hpp"
#include "AnmManager.hpp"
#include "AsciiManager.hpp"
#include "Chain.hpp"
#include "Controller.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "GameManager.hpp"
#include "MobileDiagnostics.hpp"
#include "MobileUi.hpp"
#include "Localization.hpp"
#include "Online.hpp"
#include "ReplayManager.hpp"
#include "ScreenEffect.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "ZunResult.hpp"
#include "dxutil.hpp"

namespace fs = std::filesystem;

const char *g_DemoReplayPaths[3] = {
    "data/demo/demorpy0.rpy",
    "data/demo/demorpy1.rpy",
    "data/demo/demorpy2.rpy",
};

const char *g_StagePracticeStrings[6] = {
    "Stage1", "Stage2", "Stage3", "Stage4", "Stage5", "Stage6",
};

const char *g_StageReplayStrings[7] = {
    "Stage1  ", "Stage2  ", "Stage3  ", "Stage4  ", "Stage5  ", "Stage6  ", "Extra   ",
};

const char *g_PhantasmReplayString = "Phantasm";

const char *g_DifficultyStrings[6] = {
    "Easy    ", "Normal  ", "Hard    ", "Lunatic ", "Extra   ", "Phantasm",
};

const char *g_CharacterAndShottypeReplayStrings[6] = {
    "ReimuA ", "ReimuB ", "MarisaA", "MarisaB", "SakuyaA", "SakuyaB",
};

i16 g_LastJoystickInput = 32;
i32 g_OnlineP1Character = 0;
i32 g_OnlineP1Shot = 0;

const char *g_KeyConfigStrings[12] = {
    "ショット、決定ボタンを設定します",
    "ボム、キャンセルボタンを設定します",
    "低速移動ボタンを設定します",
    "メッセージスキップボタンを設定します",
    "ポーズボタンを設定します",
    "上移動ボタンを設定します",
    "下移動ボタンを設定します",
    "左移動ボタンを設定します",
    "右移動ボタンを設定します",
    "ショット押しっぱなしで低速移動になるようにします",
    "初期設定に戻します",
    "おおよそ終了します",
};

const char *g_OptionsStrings[9] = {
    "プレイヤーの初期数を変更します。（初期設定　３）",
    "画面の色数を変更します。３２ＢＩＴだと最も綺麗に表示されます。",
    "ＢＧＭの再生方法を変更します。（初期設定　ＷＡＶ）",
    "効果音を再生するか選択します",
    "ウィンドウかフルスクリーンか選択します",
    "弾が多い場面でわざと処理落ちさせます(スコア、リプレイ記録不可)",
    "全て初期設定にします",
    "パッド操作のボタン配置を変更します",
    "おいそれと終了します",
};

const char *g_MainMenuStrings[9] = {
    "ゲームを開始します",
    "エキストラステージを開始します",
    "ステージを選択し、練習を開始します",
    "リプレイを鑑賞できます",
    "過去のスコアやスペルカードの取得歴を見られます",
    "音楽を聴けます",
    "各種設定できます",
    "いろいろと終了します",
    "通信方法を選択して二人プレイを開始します",
};

void InitializeTimingVars(Supervisor *arg)
{
    arg->timingErrorCount = 0;
    arg->maxTimingError = 0;
    arg->checkTiming = 0;
    arg->timingSpikeAccumulator = 0;
    arg->timingBadCount = 0;
}

void MainMenu::SetGameState(GameState gameState)
{
    this->prevGameState = this->gameState;
    this->gameState = gameState;
    // A cursor target belongs to the page on which it was tapped. Drop any
    // unsent target when that page transitions; the synchronized select edge
    // will be handled by the new state without carrying an old target into it.
    if (Online::IsNetworkSession()) Online::QueueMenuCursor(-1);
    this->inputDelayTimer = 0;
    this->stateTimer = 0;
    this->menuSubState = 0;
    this->idleFrames = 0;
}

u32 MainMenu::OnUpdate(MainMenu *arg)
{
    u32 result;

    Localization::SetTitlePageActive(arg->gameState == STATE_PRE_INPUT);

    arg->UpdatePrev();
    if (Online::IsNetworkSession()) Online::ReportMenuState(arg->gameState);
    MobileUi::SetMainMenuHome(arg->gameState == STATE_PRE_INPUT && arg->menuSubState == 1);
    f32 mobileTapX = 0.0f;
    f32 mobileTapY = 0.0f;
    f32 mobileDelta = 0.0f;
    const MobileUi::MenuTouchAction mobileAction =
        MobileUi::ConsumeMainMenuTouch(mobileTapX, mobileTapY, mobileDelta);
    if (mobileAction != MobileUi::MENU_TOUCH_NONE)
    {
        if (Online::IsNetworkSession())
        {
            // Use the same hit testing as single-player. The selected cursor
            // is sent with the next lockstep frame so both devices confirm
            // the item under the finger instead of requiring a swipe first.
            const bool handled =
                arg->HandleMobileTouch(mobileAction, mobileTapX, mobileTapY, mobileDelta);
            if (handled)
            {
                const u16 synchronizedButton =
                    mobileAction == MobileUi::MENU_TOUCH_TAP
                        ? TH_BUTTON_SELECTMENU
                        : (mobileAction == MobileUi::MENU_TOUCH_SWIPE_HORIZONTAL
                               ? (mobileDelta > 0.0f ? TH_BUTTON_RIGHT : TH_BUTTON_LEFT)
                               : (mobileDelta < 0.0f ? TH_BUTTON_UP : TH_BUTTON_DOWN));
                // A UIKit touch is a pulse, not a held button. Queue it in
                // Online so it survives a lockstep stall until stored in a
                // concrete input frame.
                Online::QueueInputPulse(synchronizedButton);
                MobileDiagnostics::Log("online/input", "queued synchronized menu pulse=0x%x",
                                       synchronizedButton);
            }
        }
        else
        {
            arg->HandleMobileTouch(mobileAction, mobileTapX, mobileTapY, mobileDelta);
        }
    }
    if (Online::IsMenuOpen())
    {
        const bool startOnline = Online::ConsumeStartGameRequested();
        const bool startLocal = Online::ConsumeLocalGameRequested();
        if (startOnline || startLocal)
        {
            Online::CloseMenu();
            arg->cursor = 0;
            g_LastFrameRawInput &= ~TH_BUTTON_SELECTMENU;
            g_CurFrameRawInput |= TH_BUTTON_SELECTMENU;
            MobileDiagnostics::Log("online", "launcher start local=%d connected=%d",
                                   startLocal, Online::IsConnected());
        }
        else
        {
            g_AnmManager->ExecuteScripts(arg->vmHead, arg->vmCount);
            if (arg->cursorVm) g_AnmManager->ExecuteScript(arg->cursorVm);
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
    }

    if (Online::IsNetworkSession())
    {
        i32 synchronizedCursor = -1;
        if (Online::ConsumeMenuCursorTarget(&synchronizedCursor))
        {
            arg->cursor = synchronizedCursor;
            MobileDiagnostics::Log("online/input", "applied synchronized menu cursor=%d",
                                   synchronizedCursor);
        }
    }

    const bool difficultySelection =
        arg->gameState == STATE_NORMAL_SELECT_DIFFICULTY ||
        arg->gameState == STATE_PRACTICE_SELECT_DIFFICULTY ||
        arg->gameState == STATE_EXTRA_SELECT_DIFFICULTY;
    if (Online::IsNetworkSession() && difficultySelection && arg->menuSubState == 1)
    {
        // Menu input is replicated through Online's control channel. It is
        // safe to keep running the native menu while the peer acknowledges
        // the page; the old implementation blocked here waiting for a
        // gameplay frame and could turn a dropped menu packet into a timeout.
        Online::NotifyMenuReady(arg->gameState, arg->cursor);
    }

    const bool shotSelection =
        arg->gameState == STATE_NORMAL_SELECT_SHOTTYPE ||
        arg->gameState == STATE_PRACTICE_SELECT_SHOTTYPE ||
        arg->gameState == STATE_EXTRA_SELECT_SHOTTYPE;
    if (Online::IsNetworkSession() && shotSelection && arg->menuSubState == 1)
    {
        if (Online::ConsumeGameplayCommit())
        {
            g_LastFrameRawInput &= ~TH_BUTTON_SELECTMENU;
            g_CurFrameRawInput |= TH_BUTTON_SELECTMENU;
            MobileDiagnostics::Log("online/startup", "game commit consumed; entering scene");
        }
        else if (Online::IsAwaitingGameCommit())
        {
            g_CurFrameRawInput &= ~TH_BUTTON_SELECTMENU;
            g_AnmManager->ExecuteScripts(arg->vmHead, arg->vmCount);
            if (arg->cursorVm) g_AnmManager->ExecuteScript(arg->cursorVm);
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
    }
    if (Online::IsMultiplayerSession() && shotSelection &&
        arg->menuSubState == 1 && WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
    {
        if (!Online::IsSelectingPlayer2Loadout() && Online::NeedsPlayer2Loadout())
        {
            g_OnlineP1Character = g_GameManager.character;
            g_OnlineP1Shot = arg->cursor;
            Online::BeginPlayer2Loadout(g_OnlineP1Character, g_OnlineP1Shot);
            const GameState characterState =
                arg->gameState == STATE_EXTRA_SELECT_SHOTTYPE
                    ? STATE_EXTRA_SELECT_CHARACTER
                    : (arg->gameState == STATE_PRACTICE_SELECT_SHOTTYPE
                           ? STATE_PRACTICE_SELECT_CHARACTER
                           : STATE_NORMAL_SELECT_CHARACTER);
            arg->SetGameState(characterState);
            g_GameManager.character = 0;
            g_GameManager.shotType = 0;
            arg->cursor = 0;
            MobileDiagnostics::Log("online", "P1 loadout=%d/%d; selecting P2",
                                   g_OnlineP1Character, g_OnlineP1Shot);
            g_AnmManager->ExecuteScripts(arg->vmHead, arg->vmCount);
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        if (Online::IsSelectingPlayer2Loadout())
        {
            Online::CompletePlayer2Loadout(g_GameManager.character, arg->cursor);
            MobileDiagnostics::Log("online", "P2 loadout=%d/%d",
                                   g_GameManager.character, arg->cursor);
            g_GameManager.character = (u8)g_OnlineP1Character;
            g_GameManager.shotType = (u8)g_OnlineP1Shot;
            arg->cursor = g_OnlineP1Shot;
            if (Online::IsNetworkSession())
            {
                const i32 difficulty = g_Supervisor.cfg.defaultDifficulty;
                const i32 stage = difficulty < DIFF_EXTRA ? 0 : difficulty + DIFF_HARD;
                g_GameManager.difficulty = difficulty;
                g_GameManager.currentStage = stage;
                Online::NotifyGameReady(difficulty, stage);
                g_CurFrameRawInput &= ~TH_BUTTON_SELECTMENU;
                g_AnmManager->ExecuteScripts(arg->vmHead, arg->vmCount);
                if (arg->cursorVm) g_AnmManager->ExecuteScript(arg->cursorVm);
                return CHAIN_CALLBACK_RESULT_CONTINUE;
            }
            // Local two-player continues through the original start handler.
        }
    }

    switch (arg->gameState)
    {
    case STATE_PRE_INPUT:
        result = arg->OnUpdatePreInput();
        break;
    case STATE_SELECT_REPLAY:
        result = arg->OnUpdateSelectReplay();
        break;
    case STATE_OPTIONS:
        result = arg->OnUpdateOptionsMenu();
        break;
    case STATE_KEY_CONFIG:
        result = arg->OnUpdateKeyConfig();
        break;
    case STATE_NORMAL_SELECT_DIFFICULTY:
    case STATE_PRACTICE_SELECT_DIFFICULTY:
    case STATE_EXTRA_SELECT_DIFFICULTY:
        result = arg->OnUpdateSelectDifficulty();
        break;
    case STATE_NORMAL_SELECT_CHARACTER:
    case STATE_PRACTICE_SELECT_CHARACTER:
    case STATE_EXTRA_SELECT_CHARACTER:
        result = arg->OnUpdateSelectCharacter();
        break;
    case STATE_NORMAL_SELECT_SHOTTYPE:
    case STATE_PRACTICE_SELECT_SHOTTYPE:
    case STATE_EXTRA_SELECT_SHOTTYPE:
        result = arg->OnUpdateSelectShotType();
        break;
    case STATE_SELECT_PRACTICE_STAGE:
        result = arg->OnUpdateSelectPracticeStage();
    }
    g_AnmManager->ExecuteScripts(arg->vmHead, arg->vmCount);
    if (arg->cursorVm)
    {
        g_AnmManager->ExecuteScript(arg->cursorVm);
    }

    return result;
}

u32 MainMenu::OnUpdatePreInput()
{
    i32 i;

    switch (this->menuSubState)
    {
    case 0:
        if (this->prevGameState == STATE_PRE_INPUT && g_Supervisor.prevState != 5)
        {
            g_Supervisor.PlayLoadedAudio(8);
        }
        if ((this->prevGameState == STATE_PRE_INPUT || this->prevGameState == 4 ||
             this->prevGameState == STATE_SELECT_REPLAY ||
             (this->prevGameState == 8 || this->prevGameState == STATE_EXTRA_SELECT_DIFFICULTY)) &&
            g_AnmManager->LoadSurface(0, "data/title/title00.jpg") != ZUN_SUCCESS)
        {
            return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
        }
        if (this->vmCount == 0)
        {
            this->vmCount = 164;
            this->vmHead = new AnmVm[this->vmCount];
            g_AnmManager->ExecuteVmsAnms(this->vmHead, 2304, this->vmCount);
        }
        g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 2);
        for (i = 0; i < 8; i++)
        {
            g_AnmManager->SetActiveSprite(&this->vmHead[i + 1],
                                          this->vmHead[i + 1].baseSpriteIdx + 1);
        }
        if (this->cursor < 8)
        {
            g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 1],
                                          (i32)this->vmHead[this->cursor + 1].baseSpriteIdx);
        }
        this->menuSubState = 0;
        this->inputDelayTimer = 0;
        this->selected = -1;
        this->menuSubState = 1;
        this->demoFramesCount = 0;
        if (g_GameManager.replay)
        {
            this->prevGameState = this->gameState;
            this->gameState = STATE_SELECT_REPLAY;
            this->inputDelayTimer = 0;
            this->stateTimer = 0;
            this->menuSubState = 0;
            this->idleFrames = 0;
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 13);
            this->cursorVm->SetInterrupt(2);
            g_GameManager.SetReplay(0);
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        if (this->isPracticeMode)
        {
            this->prevGameState = this->gameState;
            this->gameState = STATE_PRACTICE_SELECT_DIFFICULTY;
            this->inputDelayTimer = 0;
            this->stateTimer = 0;
            this->menuSubState = 0;
            this->idleFrames = 0;
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 5);
            this->cursorVm->SetInterrupt(2);
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        for (i = 0; (u32)i < 9; i++)
        {
            g_AnmManager->DrawStringFormat2(&this->vms[i], 0xfff0e0, 0x300000,
                                            g_MainMenuStrings[i]);
        }
    case 1: {
        i = MoveCursorVertical(9);
        if (i != 0)
        {
            while (g_GameManager.HasReachedMaxClearsAllShotTypes() == 0 && this->cursor == 1)
            {
                this->cursor += i;
            }
            for (i = 0; i < 8; i++)
            {
                g_AnmManager->SetActiveSprite(&this->vmHead[i + 1],
                                              this->vmHead[i + 1].baseSpriteIdx + 1);
            }
            if (this->cursor < 8)
            {
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 1],
                                              (i32)this->vmHead[this->cursor + 1].baseSpriteIdx);
            }
        }
        // The original idle timer started a demo replay after 15 seconds. Mobile
        // settings and the Online launcher must never be interrupted by it.
        this->demoFramesCount = 0;
        if (this->selected != this->cursor)
        {
            this->cursorVm = &this->vms[this->cursor];
            this->cursorVm->SetInterrupt(1);
        }
        this->selected = this->cursor;
        if (this->stateTimer < 10)
        {
            break;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_SoundPlayer.ProcessQueues();
            switch (this->cursor)
            {
            case 0:
                g_GameManager.practice = 0;
                this->cursor = g_Supervisor.cfg.defaultDifficulty;
                if (this->cursor >= 4)
                {
                    this->cursor = 2;
                }
                this->prevGameState = this->gameState;
                this->gameState = STATE_NORMAL_SELECT_DIFFICULTY;
                this->inputDelayTimer = 0;
                this->stateTimer = 0;
                this->menuSubState = 0;
                this->idleFrames = 0;
                g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 5);
                this->cursorVm->SetInterrupt(2);
                return CHAIN_CALLBACK_RESULT_CONTINUE;
            case 2:
                g_GameManager.practice = 1;
                this->cursor = g_Supervisor.cfg.defaultDifficulty;
                if (this->cursor >= 4)
                {
                    this->cursor = 2;
                }
                this->prevGameState = this->gameState;
                this->gameState = STATE_PRACTICE_SELECT_DIFFICULTY;
                this->inputDelayTimer = 0;
                this->stateTimer = 0;
                this->menuSubState = 0;
                this->idleFrames = 0;
                g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 5);
                this->cursorVm->SetInterrupt(2);
                return CHAIN_CALLBACK_RESULT_CONTINUE;
            case 1:
                if (g_GameManager.HasReachedMaxClearsAllShotTypes())
                {
                    g_GameManager.practice = 0;
                    this->cursor = g_Supervisor.cfg.defaultDifficulty == 5;
                    this->prevGameState = this->gameState;
                    this->gameState = STATE_EXTRA_SELECT_DIFFICULTY;
                    this->inputDelayTimer = 0;
                    this->stateTimer = 0;
                    this->menuSubState = 0;
                    this->idleFrames = 0;
                    g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 5);
                    this->cursorVm->SetInterrupt(2);
                    return CHAIN_CALLBACK_RESULT_CONTINUE;
                }
            case 3:
                g_GameManager.practice = 0;
                this->prevGameState = this->gameState;
                this->gameState = STATE_SELECT_REPLAY;
                this->inputDelayTimer = 0;
                this->stateTimer = 0;
                this->menuSubState = 0;
                this->idleFrames = 0;
                g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 13);
                this->cursorVm->SetInterrupt(2);
                return CHAIN_CALLBACK_RESULT_CONTINUE;
            case 5:
                g_Supervisor.curState = 8;
                this->cursorVm->SetInterrupt(2);
                return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
            case 4:
                g_Supervisor.curState = 5;
                this->cursorVm->SetInterrupt(2);
                return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
            case 6:
                this->menuSubState = 0;
                this->cursor = 0;
                this->stateTimer = 0;
                this->inputDelayTimer = 0;
                this->menuSubState = 3;
                this->inputDelayTimer = 0;
                OnUpdateOptionsMenu();
                this->cursor = 0;
                break;
            case 7:
                this->menuSubState = 2;
                this->inputDelayTimer = 0;
                g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 1);
                if (g_Supervisor.cfg.musicMode == 2)
                {
                    g_Supervisor.midiOutput->PlayLoaded(30);
                }
                break;
            case 8:
                Online::OpenMenu();
                MobileDiagnostics::Log("online", "opened from native title item");
                break;
            }
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            if (this->cursor < 8)
            {
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 1],
                                              this->vmHead[this->cursor + 1].baseSpriteIdx + 1);
            }
            this->cursor = 7;
            g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 1],
                                          (i32)this->vmHead[this->cursor + 1].baseSpriteIdx);
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            g_SoundPlayer.ProcessQueues();
        }
        break;
    }
    case 2:
        if (this->inputDelayTimer >= 60)
        {
            delete[] this->vmHead;
            this->vmHead = NULL;
            this->vmHead = NULL;
            this->vmCount = 0;
            this->stateTimer = 0;
            g_Supervisor.curState = -1;
            return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
        }
        break;
    case 3:
        if (this->inputDelayTimer >= 30)
        {
            this->prevGameState = this->gameState;
            this->gameState = STATE_OPTIONS;
            this->inputDelayTimer = 0;
            this->stateTimer = 0;
            this->menuSubState = 0;
            this->idleFrames = 0;
            this->cursor = 0;
            this->cfg = g_Supervisor.cfg;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        break;
    }
    this->idleFrames++;
    this->inputDelayTimer++;
    this->stateTimer++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 MainMenu::OnUpdateOptionsMenu()
{
    i32 i;

    switch (this->menuSubState)
    {
    default:
        goto LAB_00456e08;
    case 0:
        if (this->stateTimer == 0)
        {
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 3);
            for (i = 0; i < 9; i++)
            {
                g_AnmManager->SetActiveSprite(&this->vmHead[i + 9],
                                              this->vmHead[i + 9].baseSpriteIdx + 1);
            }
            g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 9],
                                          (i32)this->vmHead[this->cursor + 9].baseSpriteIdx);
            this->menuSubState = 0;
            this->inputDelayTimer = 0;
            this->selected = -1;
        }
        this->menuSubState = 1;
        for (i = 0; (u32)i < 9; i++)
        {
            g_AnmManager->DrawStringFormat2(&this->vms[i], 0xfff0e0, 0x300000, g_OptionsStrings[i]);
        }
    case 1:
        break;
    }

    if (MoveCursorVertical(9))
    {
        for (i = 0; i < 9; i++)
        {
            g_AnmManager->SetActiveSprite(&this->vmHead[i + 9],
                                          this->vmHead[i + 9].baseSpriteIdx + 1);
        }
        g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 9],
                                      (i32)this->vmHead[this->cursor + 9].baseSpriteIdx);
    }

    if (this->selected != this->cursor)
    {
        this->cursorVm = &this->vms[this->cursor];
        this->cursorVm->SetInterrupt(1);
    }
    this->selected = this->cursor;

    for (i = 18; i <= 22; i++)
    {
        g_AnmManager->SetActiveSprite(&this->vmHead[i], this->vmHead[i].baseSpriteIdx + 1);
    }
    i = g_Supervisor.cfg.lifeCount + 18;
    g_AnmManager->SetActiveSprite(&this->vmHead[i], (i32)this->vmHead[i].baseSpriteIdx);

    for (i = 23; i <= 24; i++)
    {
        g_AnmManager->SetActiveSprite(&this->vmHead[i], this->vmHead[i].baseSpriteIdx + 1);
    }
    i = g_Supervisor.cfg.colorMode16bit + 23;
    g_AnmManager->SetActiveSprite(&this->vmHead[i], (i32)this->vmHead[i].baseSpriteIdx);

    for (i = 25; i <= 27; i++)
    {
        g_AnmManager->SetActiveSprite(&this->vmHead[i], this->vmHead[i].baseSpriteIdx + 1);
    }
    i = g_Supervisor.cfg.musicMode + 25;
    g_AnmManager->SetActiveSprite(&this->vmHead[i], (i32)this->vmHead[i].baseSpriteIdx);

    for (i = 28; i <= 29; i++)
    {
        g_AnmManager->SetActiveSprite(&this->vmHead[i], this->vmHead[i].baseSpriteIdx + 1);
    }
    i = g_Supervisor.cfg.playSounds + 28;
    g_AnmManager->SetActiveSprite(&this->vmHead[i], (i32)this->vmHead[i].baseSpriteIdx);

    for (i = 30; i <= 31; i++)
    {
        g_AnmManager->SetActiveSprite(&this->vmHead[i], this->vmHead[i].baseSpriteIdx + 1);
    }
    i = g_Supervisor.cfg.windowed + 30;
    g_AnmManager->SetActiveSprite(&this->vmHead[i], (i32)this->vmHead[i].baseSpriteIdx);

    for (i = 32; i <= 33; i++)
    {
        g_AnmManager->SetActiveSprite(&this->vmHead[i], this->vmHead[i].baseSpriteIdx + 1);
    }
    i = g_Supervisor.cfg.slowMode + 32;
    g_AnmManager->SetActiveSprite(&this->vmHead[i], (i32)this->vmHead[i].baseSpriteIdx);

    if (this->stateTimer < 4)
    {
        goto LAB_00456e08;
    }

    if (WAS_PRESSED_RAW_AND_IS_EIGHTH(TH_BUTTON_LEFT))
    {
        switch (this->cursor)
        {
        case 0:
            if (g_Supervisor.cfg.lifeCount == 0)
            {
                g_Supervisor.cfg.lifeCount = 4;
            }
            else
            {
                g_Supervisor.cfg.lifeCount--;
            }
            break;
        case 1:
            if (!g_Supervisor.cfg.colorMode16bit)
            {
                g_Supervisor.cfg.colorMode16bit = 1;
            }
            else
            {
                g_Supervisor.cfg.colorMode16bit--;
            }
            break;
        case 2:
            g_Supervisor.StopAudio();
            if (g_Supervisor.cfg.musicMode == MUSIC_MIDI)
            {
                g_Supervisor.midiOutput->PlayLoaded(30);
            }
            if (g_Supervisor.cfg.musicMode == MUSIC_OFF)
            {
                g_Supervisor.cfg.musicMode = MUSIC_MIDI;
            }
            else
            {
                g_Supervisor.cfg.musicMode--;
            }
            if (!g_Supervisor.cfg.preloadBgm && g_Supervisor.cfg.musicMode == MUSIC_MIDI)
            {
                g_SoundPlayer.StartBGM("thbgm.dat");
            }
            g_Supervisor.LoadAudio(8, "bgm/th07_01.mid");
            g_Supervisor.PlayLoadedAudio(8);
            break;
        case 3:
            if (!g_Supervisor.cfg.playSounds)
            {
                g_Supervisor.cfg.playSounds = 1;
            }
            else
            {
                g_Supervisor.cfg.playSounds--;
            }
            break;
        case 4:
            if (!g_Supervisor.cfg.windowed)
            {
                g_Supervisor.cfg.windowed = 1;
            }
            else
            {
                g_Supervisor.cfg.windowed--;
            }
            break;
        case 5:
            if (!g_Supervisor.cfg.slowMode)
            {
                g_Supervisor.cfg.slowMode = 1;
            }
            else
            {
                g_Supervisor.cfg.slowMode--;
            }
            break;
        default:
            goto skip_left_sound;
        }
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
        g_SoundPlayer.ProcessQueues();
    }

skip_left_sound:
    if (WAS_PRESSED_RAW_AND_IS_EIGHTH(TH_BUTTON_RIGHT))
    {
        switch (this->cursor)
        {
        case 0:
            if (g_Supervisor.cfg.lifeCount >= 4)
            {
                g_Supervisor.cfg.lifeCount = 0;
            }
            else
            {
                g_Supervisor.cfg.lifeCount++;
            }
            break;
        case 1:
            if (g_Supervisor.cfg.colorMode16bit >= 1)
            {
                g_Supervisor.cfg.colorMode16bit = 0;
            }
            else
            {
                g_Supervisor.cfg.colorMode16bit++;
            }
            break;
        case 2:
            g_Supervisor.StopAudio();
            if (g_Supervisor.cfg.musicMode >= MUSIC_MIDI)
            {
                g_Supervisor.cfg.musicMode = MUSIC_OFF;
            }
            else
            {
                g_Supervisor.cfg.musicMode++;
            }
            g_Supervisor.LoadAudio(8, "bgm/th07_01.mid");
            g_Supervisor.PlayLoadedAudio(8);
            break;
        case 3:
            if (g_Supervisor.cfg.playSounds >= 1)
            {
                g_Supervisor.cfg.playSounds = 0;
            }
            else
            {
                g_Supervisor.cfg.playSounds++;
            }
            break;
        case 4:
            if (g_Supervisor.cfg.windowed >= 1)
            {
                g_Supervisor.cfg.windowed = 0;
            }
            else
            {
                g_Supervisor.cfg.windowed++;
            }
            break;
        case 5:
            if (g_Supervisor.cfg.slowMode >= 1)
            {
                g_Supervisor.cfg.slowMode = 0;
            }
            else
            {
                g_Supervisor.cfg.slowMode++;
            }
            break;
        default:
            goto skip_right_sound;
        }
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
        g_SoundPlayer.ProcessQueues();
    }

skip_right_sound:
    if (g_CurFrameRawInput)
    {
        this->idleFrames = 0;
    }

    if (this->idleFrames >= 3600)
    {
        goto LAB_00456cc0;
    }

    if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
    {
        switch (this->cursor)
        {
        case 6:
            g_Supervisor.cfg.lifeCount = 2;
            g_Supervisor.cfg.bombCount = 3;
            g_Supervisor.cfg.musicMode = MUSIC_WAV;
            g_Supervisor.cfg.playSounds = 1;
            g_Supervisor.cfg.slowMode = 0;
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_SoundPlayer.ProcessQueues();
            break;
        case 7:
            this->cursor = 0;
            SetGameState(STATE_KEY_CONFIG);
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_SoundPlayer.ProcessQueues();
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        case 8:
        LAB_00456cc0:
            this->cursor = 6;
            SetGameState(STATE_PRE_INPUT);
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            g_SoundPlayer.ProcessQueues();
            if (this->cfg.colorMode16bit != g_Supervisor.cfg.colorMode16bit ||
                this->cfg.windowed != g_Supervisor.cfg.windowed)
            {
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_ERROR;
            }
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
    }

    if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
    {
        if (this->cursor == 8)
        {
            goto LAB_00456cc0;
        }
        g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 9],
                                      this->vmHead[this->cursor + 9].baseSpriteIdx + 1);
        this->cursor = 8;
        g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 9],
                                      (i32)this->vmHead[this->cursor + 9].baseSpriteIdx);
        g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
        g_SoundPlayer.ProcessQueues();
    }

LAB_00456e08:
    this->idleFrames++;
    this->inputDelayTimer++;
    this->stateTimer++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

void MainMenu::SwapMapping(i16 btnPressed, i16 oldMapping)
{
    if (this->controlMapping.shootButton == btnPressed)
    {
        this->controlMapping.shootButton = oldMapping;
    }
    if (this->controlMapping.bombButton == btnPressed)
    {
        this->controlMapping.bombButton = oldMapping;
    }
    if (this->controlMapping.focusButton == btnPressed)
    {
        this->controlMapping.focusButton = oldMapping;
    }
    if (this->controlMapping.upButton == btnPressed)
    {
        this->controlMapping.upButton = oldMapping;
    }
    if (this->controlMapping.downButton == btnPressed)
    {
        this->controlMapping.downButton = oldMapping;
    }
    if (this->controlMapping.leftButton == btnPressed)
    {
        this->controlMapping.leftButton = oldMapping;
    }
    if (this->controlMapping.rightButton == btnPressed)
    {
        this->controlMapping.rightButton = oldMapping;
    }
    if (this->controlMapping.menuButton == btnPressed)
    {
        this->controlMapping.menuButton = oldMapping;
    }
    if (this->controlMapping.skipButton == btnPressed)
    {
        this->controlMapping.skipButton = oldMapping;
    }
}

u32 MainMenu::OnUpdateKeyConfig()
{
    AnmVm *vm;
    i32 i;
    i16 btnPressed;
    u8 *controllerState;
    AnmVm *cursorVmTmp;

    switch (this->menuSubState)
    {
    case 0:
        if (this->stateTimer == 0)
        {
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 4);
            for (i = 0; i < 12; i++)
            {
                g_AnmManager->SetActiveSprite(&this->vmHead[i + 35],
                                              this->vmHead[i + 35].baseSpriteIdx + 1);
            }
            g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 35],
                                          (i32)this->vmHead[this->cursor + 35].baseSpriteIdx);
            this->menuSubState = 0;
            this->inputDelayTimer = 0;
            this->controlMapping = g_Supervisor.cfg.controllerMapping;
            g_Supervisor.cfg.controllerMapping.upButton = -1;
            g_Supervisor.cfg.controllerMapping.downButton = -1;

            vm = &this->vmHead[47];
            UpdateMenuDigits(vm, this->controlMapping.shootButton);
            vm += 2;
            UpdateMenuDigits(vm, this->controlMapping.bombButton);
            vm += 2;
            UpdateMenuDigits(vm, this->controlMapping.focusButton);
            vm += 2;
            UpdateMenuDigits(vm, this->controlMapping.skipButton);
            vm += 2;
            UpdateMenuDigits(vm, this->controlMapping.menuButton);
            vm += 2;
            UpdateMenuDigits(vm, this->controlMapping.upButton);
            vm += 2;
            UpdateMenuDigits(vm, this->controlMapping.downButton);
            vm += 2;
            UpdateMenuDigits(vm, this->controlMapping.leftButton);
            vm += 2;
            UpdateMenuDigits(vm, this->controlMapping.rightButton);

            this->selected = -1;
        }
        this->menuSubState = 1;
        for (i = 0; (u32)i < 12; i++)
        {
            g_AnmManager->DrawStringFormat2(&this->vms[i], 0xfff0e0, 0x300000,
                                            g_KeyConfigStrings[i]);
        }
    case 1:
        if (MoveCursorVertical(12))
        {
            for (i = 0; i < 12; i++)
            {
                g_AnmManager->SetActiveSprite(&this->vmHead[i + 35],
                                              this->vmHead[i + 35].baseSpriteIdx + 1);
            }
            g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 35],
                                          (i32)this->vmHead[this->cursor + 35].baseSpriteIdx);
        }
        if (this->selected != this->cursor)
        {
            this->cursorVm = &this->vms[this->cursor];
            // this should be using SetInterrupt?
            cursorVmTmp = this->cursorVm;
            cursorVmTmp->pendingInterrupt = 1;
        }
        this->selected = this->cursor;

        vm = &this->vmHead[47];
        UpdateMenuDigits(vm, this->controlMapping.shootButton);
        vm += 2;
        UpdateMenuDigits(vm, this->controlMapping.bombButton);
        vm += 2;
        UpdateMenuDigits(vm, this->controlMapping.focusButton);
        vm += 2;
        UpdateMenuDigits(vm, this->controlMapping.skipButton);
        vm += 2;
        UpdateMenuDigits(vm, this->controlMapping.menuButton);
        vm += 2;
        UpdateMenuDigits(vm, this->controlMapping.upButton);
        vm += 2;
        UpdateMenuDigits(vm, this->controlMapping.downButton);
        vm += 2;
        UpdateMenuDigits(vm, this->controlMapping.leftButton);
        vm += 2;
        UpdateMenuDigits(vm, this->controlMapping.rightButton);

        for (i = 65; i <= 66; i++)
        {
            g_AnmManager->SetActiveSprite(&this->vmHead[i], this->vmHead[i].baseSpriteIdx + 1);
        }
        i = g_Supervisor.cfg.shotSlow + 65;
        g_AnmManager->SetActiveSprite(&this->vmHead[i], (i32)this->vmHead[i].baseSpriteIdx);

        controllerState = Controller::GetControllerState();
        for (btnPressed = 0; btnPressed < 32; btnPressed++)
        {
            if (controllerState[btnPressed] & 0x80)
            {
                break;
            }
        }
        if (btnPressed < 32 && g_LastJoystickInput != btnPressed)
        {
            switch (this->cursor)
            {
            case 0:
                SwapMapping(btnPressed, this->controlMapping.shootButton);
                this->controlMapping.shootButton = btnPressed;
                break;
            case 1:
                SwapMapping(btnPressed, this->controlMapping.bombButton);
                this->controlMapping.bombButton = btnPressed;
                break;
            case 2:
                SwapMapping(btnPressed, this->controlMapping.focusButton);
                this->controlMapping.focusButton = btnPressed;
                break;
            case 4:
                SwapMapping(btnPressed, this->controlMapping.menuButton);
                this->controlMapping.menuButton = btnPressed;
                break;
            case 5:
                SwapMapping(btnPressed, this->controlMapping.upButton);
                this->controlMapping.upButton = btnPressed;
                break;
            case 6:
                SwapMapping(btnPressed, this->controlMapping.downButton);
                this->controlMapping.downButton = btnPressed;
                break;
            case 7:
                SwapMapping(btnPressed, this->controlMapping.leftButton);
                this->controlMapping.leftButton = btnPressed;
                break;
            case 8:
                SwapMapping(btnPressed, this->controlMapping.rightButton);
                this->controlMapping.rightButton = btnPressed;
                break;
            case 3:
                SwapMapping(btnPressed, this->controlMapping.skipButton);
                this->controlMapping.skipButton = btnPressed;
                break;
            default:
                goto switchD_00457548_default;
            }
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_SoundPlayer.ProcessQueues();
        }
    switchD_00457548_default:
        g_LastJoystickInput = btnPressed;

        if (WAS_PRESSED_RAW(TH_BUTTON_LEFT))
        {
            switch (this->cursor)
            {
            case 9:
                g_Supervisor.cfg.shotSlow = 1 - g_Supervisor.cfg.shotSlow;
            }
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RIGHT))
        {
            switch (this->cursor)
            {
            case 9:
                g_Supervisor.cfg.shotSlow = 1 - g_Supervisor.cfg.shotSlow;
            }
        }
        if (g_CurFrameRawInput)
        {
            this->idleFrames = 0;
        }
        if (this->idleFrames >= 3600)
        {
            goto exit_config;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            switch (this->cursor)
            {
            case 10:
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
                g_SoundPlayer.ProcessQueues();
                this->controlMapping = g_ControllerMapping;
                g_Supervisor.cfg.shotSlow = 1;
                break;
            case 11:
            exit_config:
                g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
                g_SoundPlayer.ProcessQueues();
                SetGameState(STATE_OPTIONS);
                g_Supervisor.cfg.controllerMapping = this->controlMapping;
                this->cursor = 7;
                return CHAIN_CALLBACK_RESULT_CONTINUE;
            }
        }
        break;
    }
    this->idleFrames++;
    this->inputDelayTimer++;
    this->stateTimer++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

ZunResult MainMenu::UpdateMenuDigits(AnmVm *param_1, i16 param_2)
{
    if (param_2 < 0)
    {
        param_1->active = 0;
        param_1[1].active = 0;
    }
    else
    {
        g_AnmManager->SetActiveSprite(param_1, (i32)param_1->baseSpriteIdx + (i32)param_2 / 10 * 2);
        g_AnmManager->SetActiveSprite(param_1 + 1,
                                      (i32)param_1[1].baseSpriteIdx + (i32)param_2 % 10 * 2);
        param_1->active = 1;
        param_1[1].active = 1;
    }
    return ZUN_SUCCESS;
}

u32 MainMenu::OnUpdateSelectDifficulty()
{
    i32 oldGameState;
    i32 numDifficulties;
    i32 i;

    switch (this->menuSubState)
    {
    case 0:
        if (this->stateTimer == 0)
        {
            if (this->prevGameState != 5 && this->prevGameState != 9 &&
                this->prevGameState != STATE_EXTRA_SELECT_CHARACTER &&
                g_AnmManager->LoadSurface(0, "data/title/select00.jpg") != ZUN_SUCCESS)
            {
                return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
            }
            this->cursor = g_Supervisor.cfg.defaultDifficulty;
            if (this->gameState != STATE_EXTRA_SELECT_DIFFICULTY)
            {
                g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 7);
            }
            else if (g_GameManager.HasUnlockedPhantomAndMaxClears() == 0)
            {
                g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 12);
                this->cursor = 4;
            }
            else
            {
                g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 22);
            }
            if (this->gameState != STATE_EXTRA_SELECT_DIFFICULTY)
            {
                if (this->cursor >= 4)
                {
                    this->cursor = 1;
                }
                for (i = 0; i < 4; i++)
                {
                    g_AnmManager->SetActiveSprite(&this->vmHead[i + 67],
                                                  this->vmHead[i + 67].baseSpriteIdx + 1);
                }
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 67],
                                              (i32)this->vmHead[this->cursor + 67].baseSpriteIdx);
            }
            else
            {
                this->cursor -= 4;
                if (this->cursor < 0)
                {
                    this->cursor = 0;
                }
                for (i = 0; i < 2; i++)
                {
                    g_AnmManager->SetActiveSprite(&this->vmHead[i + 162],
                                                  this->vmHead[i + 162].baseSpriteIdx + 1);
                }
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 162],
                                              (i32)this->vmHead[this->cursor + 162].baseSpriteIdx);
            }
            this->menuSubState = 0;
            this->inputDelayTimer = 0;
            this->cursorVm = NULL;
        }
        if (this->isPracticeMode)
        {
            SetGameState(STATE_PRACTICE_SELECT_CHARACTER);
            this->cursor = 0;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        if (this->stateTimer == 30)
        {
            this->menuSubState = 1;
        }
        break;
    case 1:
        numDifficulties = this->gameState != STATE_EXTRA_SELECT_DIFFICULTY ? 4
                          : g_GameManager.HasUnlockedPhantomAndMaxClears() ? 2
                                                                           : 1;
        if (MoveCursorVertical(numDifficulties))
        {
            if (this->gameState != STATE_EXTRA_SELECT_DIFFICULTY)
            {
                for (i = 0; i < 4; i++)
                {
                    g_AnmManager->SetActiveSprite(&this->vmHead[i + 67],
                                                  this->vmHead[i + 67].baseSpriteIdx + 1);
                }
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 67],
                                              (i32)this->vmHead[this->cursor + 67].baseSpriteIdx);
            }
            else if (numDifficulties == 2)
            {
                for (i = 0; i < 2; i++)
                {
                    g_AnmManager->SetActiveSprite(&this->vmHead[i + 162],
                                                  this->vmHead[i + 162].baseSpriteIdx + 1);
                }
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 162],
                                              (i32)this->vmHead[this->cursor + 162].baseSpriteIdx);
            }
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            if (this->gameState != STATE_EXTRA_SELECT_DIFFICULTY)
            {
                g_Supervisor.cfg.defaultDifficulty = this->cursor;
            }
            else
            {
                g_Supervisor.cfg.defaultDifficulty = this->cursor + 4;
            }
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_SoundPlayer.ProcessQueues();
            if (this->gameState != STATE_EXTRA_SELECT_DIFFICULTY)
            {
                if (!g_GameManager.practice)
                {
                    SetGameState(STATE_NORMAL_SELECT_CHARACTER);
                }
                else
                {
                    SetGameState(STATE_PRACTICE_SELECT_CHARACTER);
                }
            }
            else
            {
                SetGameState(STATE_EXTRA_SELECT_CHARACTER);
            }

            this->cursor = 0;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            if (this->gameState != STATE_EXTRA_SELECT_DIFFICULTY)
            {
                g_Supervisor.cfg.defaultDifficulty = this->cursor;
            }
            else
            {
                g_Supervisor.cfg.defaultDifficulty = this->cursor + 4;
            }
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            g_SoundPlayer.ProcessQueues();
            this->menuSubState = 3;
            this->inputDelayTimer = 0;
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 6);
        }
        break;
    case 3:
        if (this->inputDelayTimer >= 30)
        {
            oldGameState = this->gameState;
            SetGameState(STATE_PRE_INPUT);
            if (oldGameState != STATE_EXTRA_SELECT_DIFFICULTY)
            {
                if (!g_GameManager.practice)
                {
                    this->cursor = 0;
                }
                else
                {
                    this->cursor = 2;
                }
            }
            else
            {
                this->cursor = 1;
            }
            g_GameManager.practice = 0;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
    }
    this->inputDelayTimer++;
    this->idleFrames++;
    this->stateTimer++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 MainMenu::OnUpdateSelectCharacter()
{
    switch (this->menuSubState)
    {
    case 0:
        if (this->stateTimer == 0)
        {
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 8);
            if (g_Supervisor.cfg.defaultDifficulty < 4)
            {
                this->vmHead[g_Supervisor.cfg.defaultDifficulty + 67].SetInterrupt(9);
            }
            else
            {
                if (g_GameManager.HasUnlockedPhantomAndMaxClears() == 0)
                {
                    this->vmHead[161].SetInterrupt(9);
                }
                else
                {
                    this->vmHead[g_Supervisor.cfg.defaultDifficulty + 158].SetInterrupt(9);
                }
            }
            this->cursor = g_GameManager.character;
            if (g_Supervisor.cfg.defaultDifficulty == 4)
            {
                while (g_GameManager.HasReachedMaxClears(this->cursor << 1) == 0 &&
                       g_GameManager.HasReachedMaxClears(this->cursor * 2 + 1) == 0)
                {
                    this->cursor++;
                    if (this->cursor >= 3)
                    {
                        this->cursor -= 3;
                    }
                }
            }
            else if (g_Supervisor.cfg.defaultDifficulty == 5)
            {
                while (g_GameManager.HasUnlockedPhantom(this->cursor << 1) == 0 &&
                       g_GameManager.HasUnlockedPhantom(this->cursor * 2 + 1) == 0)
                {
                    this->cursor++;
                    if (this->cursor >= 3)
                    {
                        this->cursor -= 3;
                    }
                }
            }
            this->vmHead[72].active = 0;
            this->vmHead[73].active = 0;
            this->vmHead[71].active = 0;
            this->vmHead[80].active = 0;
            this->vmHead[83].active = 0;
            this->vmHead[75].active = 0;
            this->vmHead[76].active = 0;
            this->vmHead[74].active = 0;
            this->vmHead[81].active = 0;
            this->vmHead[84].active = 0;
            this->vmHead[78].active = 0;
            this->vmHead[79].active = 0;
            this->vmHead[77].active = 0;
            this->vmHead[82].active = 0;
            this->vmHead[85].active = 0;
            switch (this->cursor)
            {
            case 0:
                this->vmHead[72].active = 1;
                this->vmHead[73].active = 1;
                this->vmHead[71].active = 1;
                this->vmHead[80].active = 1;
                this->vmHead[83].active = 1;
                break;
            case 1:
                this->vmHead[75].active = 1;
                this->vmHead[76].active = 1;
                this->vmHead[74].active = 1;
                this->vmHead[81].active = 1;
                this->vmHead[84].active = 1;
                break;
            case 2:
                this->vmHead[78].active = 1;
                this->vmHead[79].active = 1;
                this->vmHead[77].active = 1;
                this->vmHead[82].active = 1;
                this->vmHead[85].active = 1;
                break;
            }
            switch (this->cursor)
            {
            case 0:
                this->vmHead[71].SetInterrupt(9);
                this->vmHead[74].SetInterrupt(8);
                this->vmHead[77].SetInterrupt(8);
                this->vmHead[74].color.bytes.a = 0;
                this->vmHead[77].color.bytes.a = 0;
                this->vmHead[80].SetInterrupt(9);
                this->vmHead[81].SetInterrupt(8);
                this->vmHead[82].SetInterrupt(8);
                this->vmHead[81].color.bytes.a = 0;
                this->vmHead[82].color.bytes.a = 0;
                this->vmHead[83].SetInterrupt(9);
                this->vmHead[84].SetInterrupt(8);
                this->vmHead[85].SetInterrupt(8);
                this->vmHead[84].color.bytes.a = 0;
                this->vmHead[85].color.bytes.a = 0;
                break;
            case 1:
                this->vmHead[71].SetInterrupt(8);
                this->vmHead[74].SetInterrupt(9);
                this->vmHead[77].SetInterrupt(8);
                this->vmHead[71].color.bytes.a = 0;
                this->vmHead[77].color.bytes.a = 0;
                this->vmHead[80].SetInterrupt(8);
                this->vmHead[81].SetInterrupt(9);
                this->vmHead[82].SetInterrupt(8);
                this->vmHead[80].color.bytes.a = 0;
                this->vmHead[82].color.bytes.a = 0;
                this->vmHead[83].SetInterrupt(8);
                this->vmHead[84].SetInterrupt(9);
                this->vmHead[85].SetInterrupt(8);
                this->vmHead[83].color.bytes.a = 0;
                this->vmHead[85].color.bytes.a = 0;
                break;
            case 2:
                this->vmHead[71].SetInterrupt(8);
                this->vmHead[74].SetInterrupt(8);
                this->vmHead[77].SetInterrupt(9);
                this->vmHead[74].color.bytes.a = 0;
                this->vmHead[71].color.bytes.a = 0;
                this->vmHead[80].SetInterrupt(8);
                this->vmHead[81].SetInterrupt(8);
                this->vmHead[82].SetInterrupt(9);
                this->vmHead[80].color.bytes.a = 0;
                this->vmHead[81].color.bytes.a = 0;
                this->vmHead[83].SetInterrupt(8);
                this->vmHead[84].SetInterrupt(8);
                this->vmHead[85].SetInterrupt(9);
                this->vmHead[83].color.bytes.a = 0;
                this->vmHead[84].color.bytes.a = 0;
                break;
            }
            this->menuSubState = 0;
            this->inputDelayTimer = 0;
        }
        if (this->isPracticeMode)
        {
            SetGameState(STATE_PRACTICE_SELECT_SHOTTYPE);
            this->cursor = 0;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        if (this->stateTimer == 30)
        {
            this->menuSubState = 1;
        }
        break;
    case 1:
        if (MoveCursorHorizontal(3) != ZUN_SUCCESS)
        {
            if (g_Supervisor.cfg.defaultDifficulty == 4)
            {
                while (g_GameManager.HasReachedMaxClears(this->cursor << 1) == 0 &&
                       g_GameManager.HasReachedMaxClears(this->cursor * 2 + 1) == 0)
                {
                    this->cursor++;
                    if (this->cursor >= 3)
                    {
                        this->cursor -= 3;
                    }
                }
            }
            else if (g_Supervisor.cfg.defaultDifficulty == 5)
            {
                while (g_GameManager.HasUnlockedPhantom(this->cursor << 1) == 0 &&
                       g_GameManager.HasUnlockedPhantom(this->cursor * 2 + 1) == 0)
                {
                    this->cursor++;
                    if (this->cursor >= 3)
                    {
                        this->cursor -= 3;
                    }
                }
            }
            this->vmHead[72].flags = this->vmHead[72].flags | 2;
            this->vmHead[73].flags = this->vmHead[73].flags | 2;
            this->vmHead[71].flags = this->vmHead[71].flags | 2;
            this->vmHead[80].flags = this->vmHead[80].flags | 2;
            this->vmHead[83].flags = this->vmHead[83].flags | 2;
            this->vmHead[75].flags = this->vmHead[75].flags | 2;
            this->vmHead[76].flags = this->vmHead[76].flags | 2;
            this->vmHead[74].flags = this->vmHead[74].flags | 2;
            this->vmHead[81].flags = this->vmHead[81].flags | 2;
            this->vmHead[84].flags = this->vmHead[84].flags | 2;
            this->vmHead[78].flags = this->vmHead[78].flags | 2;
            this->vmHead[79].flags = this->vmHead[79].flags | 2;
            this->vmHead[77].flags = this->vmHead[77].flags | 2;
            this->vmHead[82].flags = this->vmHead[82].flags | 2;
            this->vmHead[85].flags = this->vmHead[85].flags | 2;
            switch (this->cursor)
            {
            case 0:
                this->vmHead[71].SetInterrupt(9);
                this->vmHead[74].SetInterrupt(8);
                this->vmHead[77].SetInterrupt(8);
                this->vmHead[80].SetInterrupt(9);
                this->vmHead[81].SetInterrupt(8);
                this->vmHead[82].SetInterrupt(8);
                this->vmHead[83].SetInterrupt(9);
                this->vmHead[84].SetInterrupt(8);
                this->vmHead[85].SetInterrupt(8);
                break;
            case 1:
                this->vmHead[71].SetInterrupt(8);
                this->vmHead[74].SetInterrupt(9);
                this->vmHead[77].SetInterrupt(8);
                this->vmHead[80].SetInterrupt(8);
                this->vmHead[81].SetInterrupt(9);
                this->vmHead[82].SetInterrupt(8);
                this->vmHead[83].SetInterrupt(8);
                this->vmHead[84].SetInterrupt(9);
                this->vmHead[85].SetInterrupt(8);
                break;
            case 2:
                this->vmHead[71].SetInterrupt(8);
                this->vmHead[74].SetInterrupt(8);
                this->vmHead[77].SetInterrupt(9);
                this->vmHead[80].SetInterrupt(8);
                this->vmHead[81].SetInterrupt(8);
                this->vmHead[82].SetInterrupt(9);
                this->vmHead[83].SetInterrupt(8);
                this->vmHead[84].SetInterrupt(8);
                this->vmHead[85].SetInterrupt(9);
                break;
            }
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            g_GameManager.character = this->cursor;
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_SoundPlayer.ProcessQueues();
            if (this->gameState != STATE_EXTRA_SELECT_CHARACTER)
            {
                if (!g_GameManager.practice)
                {
                    SetGameState(STATE_NORMAL_SELECT_SHOTTYPE);
                }
                else
                {
                    SetGameState(STATE_PRACTICE_SELECT_SHOTTYPE);
                }
            }
            else
            {
                SetGameState(STATE_EXTRA_SELECT_SHOTTYPE);
            }
            this->cursor = 0;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            g_SoundPlayer.ProcessQueues();
            g_GameManager.character = this->cursor;
            if (this->gameState != STATE_EXTRA_SELECT_CHARACTER)
            {
                if (!g_GameManager.practice)
                {
                    SetGameState(STATE_NORMAL_SELECT_DIFFICULTY);
                }
                else
                {
                    SetGameState(STATE_PRACTICE_SELECT_DIFFICULTY);
                }
            }
            else
            {
                SetGameState(STATE_EXTRA_SELECT_DIFFICULTY);
            }
            this->cursor = 0;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        break;
    }
    this->idleFrames++;
    this->inputDelayTimer++;
    this->stateTimer++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 MainMenu::OnUpdateSelectShotType()
{
    switch (this->menuSubState)
    {
    case 0:
        if (this->stateTimer == 0)
        {
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 10);
            if (g_Supervisor.cfg.defaultDifficulty < 4)
            {
                this->vmHead[g_Supervisor.cfg.defaultDifficulty + 67].SetInterrupt(9);
            }
            else
            {
                if (g_GameManager.HasUnlockedPhantomAndMaxClears() == 0)
                {
                    this->vmHead[161].SetInterrupt(9);
                }
                else
                {
                    this->vmHead[g_Supervisor.cfg.defaultDifficulty + 158].SetInterrupt(9);
                }
            }
            this->vmHead[72].active = 0;
            this->vmHead[73].active = 0;
            this->vmHead[71].active = 0;
            this->vmHead[80].active = 0;
            this->vmHead[83].active = 0;
            this->vmHead[75].active = 0;
            this->vmHead[76].active = 0;
            this->vmHead[74].active = 0;
            this->vmHead[81].active = 0;
            this->vmHead[84].active = 0;
            this->vmHead[78].active = 0;
            this->vmHead[79].active = 0;
            this->vmHead[77].active = 0;
            this->vmHead[82].active = 0;
            this->vmHead[85].active = 0;
            this->cursor = g_GameManager.shotType;
            if (g_Supervisor.cfg.defaultDifficulty == 4)
            {
                while (g_GameManager.HasReachedMaxClears(this->cursor +
                                                         (u32)g_GameManager.character * 2) == 0)
                {
                    this->cursor++;
                    if (this->cursor >= 2)
                    {
                        this->cursor = this->cursor - 2;
                    }
                }
            }
            else if (g_Supervisor.cfg.defaultDifficulty == 5)
            {
                while (g_GameManager.HasUnlockedPhantom(this->cursor +
                                                        (u32)g_GameManager.character * 2) == 0)
                {
                    this->cursor++;
                    if (this->cursor >= 2)
                    {
                        this->cursor = this->cursor - 2;
                    }
                }
            }
            switch (g_GameManager.character)
            {
            case CHAR_REIMU:
                this->vmHead[72].active = 1;
                this->vmHead[73].active = 1;
                this->vmHead[71].active = 1;
                g_AnmManager->SetActiveSprite(&this->vmHead[73 - this->cursor],
                                              this->vmHead[73 - this->cursor].baseSpriteIdx + 1);
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 72],
                                              (i32)this->vmHead[this->cursor + 72].baseSpriteIdx);
                break;
            case CHAR_MARISA:
                this->vmHead[75].active = 1;
                this->vmHead[76].active = 1;
                this->vmHead[74].active = 1;
                g_AnmManager->SetActiveSprite(&this->vmHead[76 - this->cursor],
                                              this->vmHead[76 - this->cursor].baseSpriteIdx + 1);
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 75],
                                              (i32)this->vmHead[this->cursor + 75].baseSpriteIdx);
                break;
            case CHAR_SAKUYA:
                this->vmHead[78].active = 1;
                this->vmHead[79].active = 1;
                this->vmHead[77].active = 1;
                g_AnmManager->SetActiveSprite(&this->vmHead[79 - this->cursor],
                                              this->vmHead[79 - this->cursor].baseSpriteIdx + 1);
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 78],
                                              (i32)this->vmHead[this->cursor + 78].baseSpriteIdx);
                break;
            }
            this->menuSubState = 0;
            this->inputDelayTimer = 0;
        }
        if (this->isPracticeMode)
        {
            SetGameState(STATE_SELECT_PRACTICE_STAGE);
            this->isPracticeMode = 0;
            this->cursor = g_GameManager.currentStage - 1;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        if (this->stateTimer == 30)
        {
            this->menuSubState = 1;
        }
        break;
    case 1:
        if (MoveCursorVertical(2))
        {
            if (g_Supervisor.cfg.defaultDifficulty == 4)
            {
                while (g_GameManager.HasReachedMaxClears(this->cursor +
                                                         (u32)g_GameManager.character * 2) == 0)
                {
                    this->cursor++;
                    if (this->cursor >= 2)
                    {
                        this->cursor = this->cursor - 2;
                    }
                }
            }
            else if (g_Supervisor.cfg.defaultDifficulty == 5)
            {
                while (g_GameManager.HasUnlockedPhantom(this->cursor +
                                                        (u32)g_GameManager.character * 2) == 0)
                {
                    this->cursor++;
                    if (this->cursor >= 2)
                    {
                        this->cursor = this->cursor - 2;
                    }
                }
            }
            switch (g_GameManager.character)
            {
            case CHAR_REIMU:
                g_AnmManager->SetActiveSprite(&this->vmHead[73 - this->cursor],
                                              this->vmHead[73 - this->cursor].baseSpriteIdx + 1);
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 72],
                                              (i32)this->vmHead[this->cursor + 72].baseSpriteIdx);
                break;
            case CHAR_MARISA:
                g_AnmManager->SetActiveSprite(&this->vmHead[76 - this->cursor],
                                              this->vmHead[76 - this->cursor].baseSpriteIdx + 1);
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 75],
                                              (i32)this->vmHead[this->cursor + 75].baseSpriteIdx);
                break;
            case CHAR_SAKUYA:
                g_AnmManager->SetActiveSprite(&this->vmHead[79 - this->cursor],
                                              this->vmHead[79 - this->cursor].baseSpriteIdx + 1);
                g_AnmManager->SetActiveSprite(&this->vmHead[this->cursor + 78],
                                              (i32)this->vmHead[this->cursor + 78].baseSpriteIdx);
                break;
            }
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            g_GameManager.shotType = this->cursor;
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_SoundPlayer.ProcessQueues();
            if (!g_GameManager.practice)
            {
                g_GameManager.difficulty = g_Supervisor.cfg.defaultDifficulty;
                if (g_GameManager.difficulty < DIFF_EXTRA)
                {
                    g_GameManager.currentStage = 0;
                }
                else
                {
                    g_GameManager.currentStage = g_GameManager.difficulty + DIFF_HARD;
                }
                g_Supervisor.curState = 2;
                g_GameManager.SetReplay(0);
                g_Supervisor.StopAudio();
                while (g_SoundPlayer.ProcessQueues())
                    ;
                return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
            }
            this->cursor = 0;
            SetGameState(STATE_SELECT_PRACTICE_STAGE);
            return CHAIN_CALLBACK_RESULT_EXECUTE_AGAIN;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            g_GameManager.shotType = this->cursor;
            if (this->gameState != STATE_EXTRA_SELECT_SHOTTYPE)
            {
                if (!g_GameManager.practice)
                {
                    SetGameState(STATE_NORMAL_SELECT_CHARACTER);
                }
                else
                {
                    SetGameState(STATE_PRACTICE_SELECT_CHARACTER);
                }
            }
            else
            {
                SetGameState(STATE_EXTRA_SELECT_CHARACTER);
            }
            this->vmHead[72].active = 1;
            this->vmHead[73].active = 1;
            this->vmHead[71].active = 1;
            this->vmHead[80].active = 1;
            this->vmHead[83].active = 1;
            this->vmHead[75].active = 1;
            this->vmHead[76].active = 1;
            this->vmHead[74].active = 1;
            this->vmHead[81].active = 1;
            this->vmHead[84].active = 1;
            this->vmHead[78].active = 1;
            this->vmHead[79].active = 1;
            this->vmHead[77].active = 1;
            this->vmHead[82].active = 1;
            this->vmHead[85].active = 1;
            return CHAIN_CALLBACK_RESULT_EXECUTE_AGAIN;
        }
        break;
    }
    this->idleFrames++;
    this->inputDelayTimer++;
    this->stateTimer++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 MainMenu::OnUpdateSelectPracticeStage()
{
    i32 local_8;

    switch (this->menuSubState)
    {
    case 0:
        if (this->stateTimer == 0)
        {
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 18);
            this->vmHead[72].active = 0;
            this->vmHead[73].active = 0;
            this->vmHead[71].active = 0;
            this->vmHead[80].active = 0;
            this->vmHead[83].active = 0;
            this->vmHead[75].active = 0;
            this->vmHead[76].active = 0;
            this->vmHead[74].active = 0;
            this->vmHead[81].active = 0;
            this->vmHead[84].active = 0;
            this->vmHead[78].active = 0;
            this->vmHead[79].active = 0;
            this->vmHead[77].active = 0;
            this->vmHead[82].active = 0;
            this->vmHead[85].active = 0;
            switch (g_GameManager.character)
            {
            case CHAR_REIMU:
                this->vmHead[72].active = 1;
                this->vmHead[73].active = 1;
                this->vmHead[71].active = 1;
                break;
            case CHAR_MARISA:
                this->vmHead[75].active = 1;
                this->vmHead[76].active = 1;
                this->vmHead[74].active = 1;
                break;
            case CHAR_SAKUYA:
                this->vmHead[78].active = 1;
                this->vmHead[79].active = 1;
                this->vmHead[77].active = 1;
                break;
            }
            this->menuSubState = 0;
            this->inputDelayTimer = 0;
            g_GameManager.practice = 1;
        }
        if (this->stateTimer == 30)
        {
            this->menuSubState = 1;
        }
        break;
    case 1:
        local_8 = g_GameManager.clrd[g_GameManager.character * 2 + g_GameManager.shotType]
                      .difficultyClearedWithoutRetries[g_Supervisor.cfg.defaultDifficulty];
        if (local_8 < 0)
        {
            local_8 = 1;
        }
        else if (local_8 >= 99)
        {
            local_8 = 6;
        }
        if (this->cursor >= local_8)
        {
            this->cursor = 0;
        }
        MoveCursorVertical(local_8);
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_GameManager.difficulty = g_Supervisor.cfg.defaultDifficulty;
            g_GameManager.currentStage = this->cursor;
            g_Supervisor.curState = 2;

            i32 idk = 0;
            g_GameManager.replay = idk;
            g_Supervisor.StopAudio();
            while (g_SoundPlayer.ProcessQueues())
                ;
            return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            this->cursor = g_GameManager.shotType;
            SetGameState(STATE_NORMAL_SELECT_SHOTTYPE);
            this->vmHead[72].active = 1;
            this->vmHead[73].active = 1;
            this->vmHead[71].active = 1;
            this->vmHead[80].active = 1;
            this->vmHead[83].active = 1;
            this->vmHead[75].active = 1;
            this->vmHead[76].active = 1;
            this->vmHead[74].active = 1;
            this->vmHead[81].active = 1;
            this->vmHead[84].active = 1;
            this->vmHead[78].active = 1;
            this->vmHead[79].active = 1;
            this->vmHead[77].active = 1;
            this->vmHead[82].active = 1;
            this->vmHead[85].active = 1;
            return CHAIN_CALLBACK_RESULT_EXECUTE_AGAIN;
        }
        break;
    }
    this->idleFrames++;
    this->inputDelayTimer++;
    this->stateTimer++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

bool ReplayFileMatches(const std::string &name)
{
    return name.size() == 14 && name.compare(0, 6, "th7_ud") == 0 &&
           name.compare(10, 4, ".rpy") == 0;
}

u32 MainMenu::OnUpdateSelectReplay()
{
    ReplayFile *file;
    i32 local_10;
    i32 i;

    switch (this->menuSubState)
    {
    case 0:
        if (this->stateTimer == 0)
        {
            if (this->prevGameState != STATE_SELECT_REPLAY &&
                g_AnmManager->LoadSurface(0, "data/title/select00.jpg") != ZUN_SUCCESS)
            {
                return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
            }
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 14);
            this->cursor = 0;
            this->menuSubState = 0;
            this->inputDelayTimer = 0;
            this->cursorVm = NULL;
            local_10 = 0;
            for (i = 0; i < 15; i++)
            {
                char filename[32];
                snprintf(filename, sizeof(filename), "th7_%.2d.rpy", i + 1);
                std::string replayPath =
                    (fs::path(FileSystem::GetPrefPath("replay")) / filename).string();
                file = (ReplayFile *)FileSystem::OpenFile(replayPath.c_str(), 1);
                if (!file)
                {
                    continue;
                }

                file = ReplayManager::ValidateReplayData(file, g_LastFileSize);
                if (file)
                {
                    this->replays[local_10] = *file;
                    SDL_strlcpy(this->replayFilenames[local_10], replayPath.c_str(),
                                sizeof(this->replayFilenames[local_10]));
                    sprintf(this->replayLabels[local_10], "No.%.2d", i + 1);
                    local_10++;
                    ReplayManager::FreeReplay(file);
                }
            }

            const fs::path replay = FileSystem::GetPrefPath("replay");
            fs::create_directory(replay);

            std::vector<fs::directory_entry> entries(fs::directory_iterator(replay),
                                                     fs::directory_iterator{});
            std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
                return a.path().filename() < b.path().filename();
            });
            for (const auto &entry : entries)
            {
                const std::string filename = entry.path().filename().string();
                if (!ReplayFileMatches(filename))
                {
                    continue;
                }
                if (local_10 >= 45)
                {
                    break;
                }
                file = (ReplayFile *)FileSystem::OpenFile(
                    (FileSystem::GetPrefPath("replay") + "/" + filename).c_str(), 1);
                if (!file)
                {
                    continue;
                }
                file = ReplayManager::ValidateReplayData(file, g_LastFileSize);
                if (file)
                {
                    this->replays[local_10] = *file;
                    SDL_strlcpy(this->replayFilenames[local_10],
                                (FileSystem::GetPrefPath("replay") + "/" + filename).c_str(),
                                sizeof(this->replayFilenames[local_10]));
                    sprintf(this->replayLabels[local_10], "User ");
                    ReplayManager::FreeReplay(file);
                    local_10++;
                }
            }
            this->replayFilesNum = local_10;
            this->replayPage = 0;
        }
        if (this->stateTimer >= 30)
        {
            this->menuSubState = 1;
            this->inputDelayTimer = 0;
        }
        break;
    case 1:
        MoveCursorVertical(this->replayFilesNum);
        if (15 < this->replayFilesNum)
        {
            if (WAS_PRESSED_RAW_AND_IS_EIGHTH(TH_BUTTON_LEFT))
            {
                this->cursor = this->cursor - 15;
                if (this->cursor < 0)
                {
                    this->cursor = this->cursor + this->replayFilesNum;
                }
                g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
            }
            if (WAS_PRESSED_RAW_AND_IS_EIGHTH(TH_BUTTON_RIGHT))
            {
                this->cursor = this->cursor + 15;
                if (this->cursor >= this->replayFilesNum)
                {
                    this->cursor = this->cursor - this->replayFilesNum;
                }
                g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
            }
        }
        this->chosenReplay = this->cursor;
        if (this->inputDelayTimer < 10)
        {
            break;
        }

        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            if (this->replayFilesNum == 0)
            {
                break;
            }

            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            this->menuSubState = 2;
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 15);
            this->vmHead[this->chosenReplay % 15 + 135].SetInterrupt(17);
            this->currentReplay =
                (ReplayFile *)FileSystem::OpenFile(this->replayFilenames[this->chosenReplay], 1);
            this->currentReplay =
                ReplayManager::ValidateReplayData(this->currentReplay, g_LastFileSize);
            this->cursor = 0;
            while (!this->currentReplay->head.stageReplayDataOffsets[this->cursor])
            {
                this->cursor++;
                if (this->cursor >= 7)
                {
                    g_GameErrorContext.Fatal("リプレイデータが異常\n");
                    return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
                }
            }
            break;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            this->menuSubState = 4;
            this->inputDelayTimer = 0;
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 16);
        }
        break;
    case 2:
        i = MoveCursorVertical(7);
        if (i < 0)
        {
            while (!this->replays[this->chosenReplay].head.stageReplayDataOffsets[this->cursor])
            {
                this->cursor--;
                if (this->cursor < 0)
                {
                    this->cursor = 6;
                }
            }
        }
        else if (0 < i)
        {
            while (!this->replays[this->chosenReplay].head.stageReplayDataOffsets[this->cursor])
            {
                this->cursor++;
                if (this->cursor >= 7)
                {
                    this->cursor = 0;
                }
            }
        }
        this->selectedStage = this->cursor;
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 19);
            this->vmHead[this->chosenReplay % 15 + 135].SetInterrupt(17);
            this->menuSubState = 3;
            this->cursor = 0;
            this->vmHead[158].pendingInterrupt = 21;
            this->vmHead[159].pendingInterrupt = 21;
            this->vmHead[160].pendingInterrupt = 21;
            this->vmHead[this->cursor + 158].pendingInterrupt = 20;
            break;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            ReplayManager::FreeReplay(this->currentReplay);
            this->currentReplay = NULL;
            this->menuSubState = 1;
            this->stateTimer = 0;
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 14);
            this->cursor = this->chosenReplay;
            break;
        }
        break;
    case 3:
        i = MoveCursorVertical(3);
        if (i != 0)
        {
            this->vmHead[158].pendingInterrupt = 21;
            this->vmHead[159].pendingInterrupt = 21;
            this->vmHead[160].pendingInterrupt = 21;
            this->vmHead[this->cursor + 158].pendingInterrupt = 20;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            g_GameManager.SetReplay(1);
            SDL_strlcpy(g_GameManager.replayFilename, this->replayFilenames[this->chosenReplay],
                        sizeof(g_GameManager.replayFilename));
            g_GameManager.difficulty = this->currentReplay->data.difficulty;
            g_GameManager.character = this->currentReplay->data.shotType / 2;
            g_GameManager.shotType = this->currentReplay->data.shotType % 2;
            g_GameManager.shotTypeAndCharacter = this->currentReplay->data.shotType;
            ReplayManager::FreeReplay(this->currentReplay);
            this->currentReplay = NULL;
            g_GameManager.currentStage = g_GameManager.difficulty >= 5 ? 7 : this->selectedStage;
            g_Supervisor.curState = 2;
            g_GameManager.replayStage = (u8)this->cursor;
            g_Supervisor.StopAudio();
            while (g_SoundPlayer.ProcessQueues())
                ;
            return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            this->menuSubState = 2;
            this->stateTimer = 0;
            this->cursor = this->selectedStage;
            g_AnmManager->SetInterruptActiveVms(this->vmHead, this->vmCount, 15);
            this->vmHead[this->chosenReplay % 15 + 135].SetInterrupt(17);
            break;
        }
        break;
    case 4:
        if (this->inputDelayTimer >= 30)
        {
            SetGameState(STATE_PRE_INPUT);
            this->cursor = 3;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }
        break;
    }
    this->idleFrames++;
    this->inputDelayTimer++;
    this->stateTimer++;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

i32 MainMenu::DrawReplayMenu()
{
    i32 replayAmount;
    i32 i;
    AnmVm *vm;

    vm = &this->vmHead[134];
    AsciiManager::AddFormatText(&g_AsciiManager, &vm->pos, "No.   Name       Date  Player   Rank");
    replayAmount = this->chosenReplay - this->chosenReplay % 15;
    for (i = replayAmount + 15; replayAmount < i; replayAmount++)
    {
        if (replayAmount >= this->replayFilesNum)
        {
            break;
        }
        vm++;
        g_AsciiManager.isSelected = IsReplaySelected(replayAmount);
        if (replayAmount == this->chosenReplay)
        {
            g_AsciiManager.color = 0xffffffff;
        }
        else
        {
            g_AsciiManager.color = 0xff808080;
        }
        AsciiManager::AddFormatText(
            &g_AsciiManager, &vm->pos, "%s %8s  %6s %7s  %8s", this->replayLabels + replayAmount,
            this->replays[replayAmount].data.name, this->replays[replayAmount].data.date,
            g_CharacterAndShottypeReplayStrings[this->replays[replayAmount].data.shotType],
            g_DifficultyStrings[this->replays[replayAmount].data.difficulty]);
    }
    if ((this->menuSubState == 2 || this->menuSubState == 3) && this->currentReplay != NULL)
    {
        g_AsciiManager.color = 0xffffffff;
        g_AsciiManager.isSelected = 0;
        vm = &this->vmHead[133];
        AsciiManager::AddFormatText(&g_AsciiManager, &vm->pos, "       %2.3f%%",
                                    (f64)this->currentReplay->data.slowdownRate);
        vm = &this->vmHead[150];
        AsciiManager::AddFormatText(&g_AsciiManager, &vm->pos, "Stage    LastScore");
        replayAmount = this->chosenReplay - this->chosenReplay % 15;
        for (i = 0; i < 7; i++, replayAmount++)
        {
            vm++;
            if (this->menuSubState != 3)
            {
                g_AsciiManager.isSelected = IsStageSelected(i);
                if (i == this->selectedStage)
                {
                    g_AsciiManager.color = 0xffffffff;
                }
                else
                {
                    g_AsciiManager.color = 0xff808080;
                }
            }
            else
            {
                if (i == this->selectedStage)
                {
                    g_AsciiManager.color = 0x60ffffff;
                }
                else
                {
                    g_AsciiManager.color = 0x60808080;
                }
            }
            if (this->currentReplay->stageReplayData[i])
            {
                if (i < 6 || this->currentReplay->data.difficulty <= 4)
                {
                    AsciiManager::AddFormatText(&g_AsciiManager, &vm->pos, "%s %9d0",
                                                g_StageReplayStrings[i],
                                                this->currentReplay->stageReplayData[i]->score);
                }
                else
                {
                    AsciiManager::AddFormatText(&g_AsciiManager, &vm->pos, "%s %9d0",
                                                g_PhantasmReplayString,
                                                this->currentReplay->stageReplayData[i]->score);
                }
            }
            else
            {
                if (i < 6 || this->currentReplay->data.difficulty <= 4)
                {
                    AsciiManager::AddFormatText(&g_AsciiManager, &vm->pos, "%s ----------",
                                                g_StageReplayStrings[i]);
                }
                else
                {
                    AsciiManager::AddFormatText(&g_AsciiManager, &vm->pos, "%s ----------",
                                                g_PhantasmReplayString);
                }
            }
        }
    }
    g_AsciiManager.color = 0xffffffff;
    g_AsciiManager.isSelected = 0;
    return 1;
}

i32 MainMenu::DrawPracticeMenu()
{
    ZunVec3 local_1c;
    i32 local_10;
    i32 i;
    AnmVm *vm;

    g_AsciiManager.color = 0xffffffff;
    g_AsciiManager.isSelected = 0;
    vm = &this->vmHead[131];
    AsciiManager::AddFormatText(&g_AsciiManager, &vm->pos, "Stage    HI-Score");
    local_1c = vm->pos;
    local_1c.y += 16.0f;
    local_10 = g_GameManager.clrd[g_GameManager.character * 2 + g_GameManager.shotType]
                   .difficultyClearedWithoutRetries[g_Supervisor.cfg.defaultDifficulty];

    for (i = 0; i < 6; i++)
    {
        g_AsciiManager.isSelected = IsSelected(i);
        if (i == this->cursor)
        {
            g_AsciiManager.color = 0xffffffff;
        }
        else if (i < local_10)
        {
            g_AsciiManager.color = 0xffa0a0a0;
        }
        else
        {
            g_AsciiManager.color = 0xff404040;
        }
        AsciiManager::AddFormatText(&g_AsciiManager, &local_1c, "%s %9d0 (%3d)",
                                    g_StagePracticeStrings[i],
                                    g_GameManager
                                        .pscr[g_GameManager.character * 2 + g_GameManager.shotType]
                                             [i][g_Supervisor.cfg.defaultDifficulty]
                                        .score,
                                    g_GameManager
                                        .pscr[g_GameManager.character * 2 + g_GameManager.shotType]
                                             [i][g_Supervisor.cfg.defaultDifficulty]
                                        .playCount);
        local_1c.y += 16.0f;
    }
    g_AsciiManager.color = 0xffffffff;
    g_AsciiManager.isSelected = 0;
    return 1;
}

i32 MainMenu::MoveCursorVertical(i32 max)
{
    if (max == 0)
    {
        return 0;
    }
    if (WAS_PRESSED_RAW_AND_IS_EIGHTH(TH_BUTTON_UP))
    {
        this->cursor--;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
        if (this->cursor < 0)
        {
            this->cursor = max - 1;
        }
        if (this->cursor >= max)
        {
            this->cursor = 0;
        }
        return -1;
    }
    if (WAS_PRESSED_RAW_AND_IS_EIGHTH(TH_BUTTON_DOWN))
    {
        this->cursor++;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
        if (this->cursor < 0)
        {
            this->cursor = max - 1;
        }
        if (this->cursor >= max)
        {
            this->cursor = 0;
        }
        return 1;
    }
    return 0;
}

i32 MainMenu::MoveCursorHorizontal(i32 max)
{
    if (max == 0)
    {
        return 0;
    }
    if (WAS_PRESSED_RAW_AND_IS_EIGHTH(TH_BUTTON_LEFT))
    {
        this->cursor = this->cursor - 1;
        if (this->cursor < 0)
        {
            this->cursor = this->cursor + max;
        }
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
        return -1;
    }
    if (WAS_PRESSED_RAW_AND_IS_EIGHTH(TH_BUTTON_RIGHT))
    {
        this->cursor++;
        if (this->cursor >= max)
        {
            this->cursor = this->cursor - max;
        }
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
        return 1;
    }
    return 0;
}

u32 MainMenu::OnDraw(MainMenu *arg)
{
    ZunVec3 savedPos;
    AnmVm *local_c;
    i32 i;

    g_AnmManager->SetTexture(0);
    g_AnmManager->CopySurfaceToBackBuffer(0, 0, 0, 0, 0);
    switch (arg->gameState)
    {
    case STATE_SELECT_REPLAY:
        arg->DrawReplayMenu();
        break;
    case STATE_SELECT_PRACTICE_STAGE:
        arg->DrawPracticeMenu();
        break;
    }
    local_c = arg->vmHead;
    for (i = 0; i < arg->vmCount; i++, local_c++)
    {
        if (g_AnmManager->ShouldDraw(local_c))
        {
            savedPos = local_c->pos;
            ZunVec3 drawPos = local_c->prevPos.Lerp(local_c->pos, g_RenderAlpha);
            local_c->pos = drawPos + local_c->offset;
            if (local_c->rotation.z != 0.0f)
            {
                g_AnmManager->Draw(local_c);
            }
            else
            {
                g_AnmManager->DrawNoRotation(local_c);
            }
            local_c->pos = savedPos;
        }
    }
    if (arg->cursorVm)
    {
        g_AnmManager->DrawInterpNoRotation(arg->cursorVm);
    }
    if (arg->gameState == STATE_PRE_INPUT && arg->menuSubState == 1)
    {
        // The original title ANM has no ninth baked sprite. Queue the extra
        // item through the native ASCII/title font so it matches the other
        // entries on desktop and iOS alike.
        const bool selected = arg->cursor == 8;
        const u32 previousColor = g_AsciiManager.color;
        const Float2 previousScale = g_AsciiManager.scale;
        const i32 previousSelected = g_AsciiManager.isSelected;
        const i32 previousGui = g_AsciiManager.isGui;
        ZunVec3 onlinePos(472.0f, 425.0f, 0.0f);
        g_AsciiManager.color = selected ? 0xffffffff : 0xffd0d0e0;
        g_AsciiManager.scale.x = 1.0f;
        g_AsciiManager.scale.y = 1.0f;
        g_AsciiManager.isSelected = selected ? 1 : 0;
        g_AsciiManager.isGui = 0;
        g_AsciiManager.AddString(&onlinePos, "Online");
        g_AsciiManager.color = previousColor;
        g_AsciiManager.scale = previousScale;
        g_AsciiManager.isSelected = previousSelected;
        g_AsciiManager.isGui = previousGui;
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

ZunResult MainMenu::ActualAddedCallback()
{
    i32 i;
    ZunRect local_34;
    ZunColor local_24;
    ZunColor local_20;
    ZunRect local_1c;
    i32 frameCount;
    ScoreDat *local_8;

    SAFE_DELETE(g_GameManager.defaultCfg);
    g_GameManager.defaultCfg = new GameConfiguration;
    SAFE_DELETE(g_GameManager.globals);
    g_GameManager.globals = new ZunGlobals;
    g_Supervisor.effectiveFramerateMultiplier = 1.0f;
    if (g_GameManager.replay)
    {
        g_GameManager.shotTypeAndCharacter = SHOT_REIMU_A;
        g_GameManager.character = g_GameManager.shotTypeAndCharacter;
    }
    if (g_GameManager.demo)
    {
        g_GameManager.replay = 0;
    }
    local_8 = ResultScreen::OpenScore(FileSystem::GetPrefPath("score.dat").c_str());
    if (!local_8)
    {
        Supervisor::DebugPrint("error : main menu could not create score state\n");
        return ZUN_ERROR;
    }
    Supervisor::DebugPrint("info : main menu score opened\n");
    ResultScreen::ParseClrd(local_8, g_GameManager.clrd);
    ResultScreen::ParsePscr(local_8, &g_GameManager.pscr[0][0][0]);
    ResultScreen::ParseCatk(local_8, g_GameManager.catk);
    ResultScreen::ReleaseScoreDat(local_8);
    Supervisor::DebugPrint("info : main menu score parsed\n");
    if (g_GameManager.plst.gameHours < 7)
    {
        g_GameManager.maxRetries = 3;
    }
    else if (g_GameManager.plst.gameHours < 14)
    {
        g_GameManager.maxRetries = 4;
    }
    else
    {
        g_GameManager.maxRetries = 5;
    }
    if (!g_GameManager.phantasmUnlocked && g_GameManager.HasUnlockedPhantomAndMaxClears())
    {
        frameCount = 0;
        g_AnmManager->LoadSurface(0, "data/title/phantasm.jpg");
        while (frameCount < 900)
        {
            g_AnmManager->SetVertexShader(255);
            g_AnmManager->SetSprite(NULL);
            g_AnmManager->SetTexture(0);
            g_AnmManager->SetColorOp(255);
            g_AnmManager->SetBlendMode(255);
            g_AnmManager->SetZWriteDisable(255);
            g_AnmManager->ClearFrameState();
            g_AnmManager->SetCameraMode(255);
            g_AnmManager->SetColor(0x80808080);
            g_Supervisor.gfxDevice->BeginFrame();
            g_AnmManager->CopySurfaceToBackBuffer(0, 0, 0, 0, 0);
            if (frameCount < 60)
            {
                local_1c.left = 0.0f;
                local_1c.top = 0.0f;
                local_1c.right = 639.0f;
                local_1c.bottom = 479.0f;
                local_20.bytes.a = (60 - frameCount) * 255 / 60;
                local_20.bytes.r = local_20.bytes.g = local_20.bytes.b = 0;
                ScreenEffect::DrawSquare(&local_1c, local_20.color);
            }
            else if (frameCount > 840)
            {
                local_34.left = 0.0f;
                local_34.top = 0.0f;
                local_34.right = 639.0f;
                local_34.bottom = 479.0f;
                local_24.bytes.a = (frameCount - 840) * 255 / 60;
                local_24.bytes.r = local_24.bytes.g = local_24.bytes.b = 0;
                ScreenEffect::DrawSquare(&local_34, local_24.color);
            }
            g_CurFrameRawInput = Controller::GetInput();
            g_Supervisor.gfxDevice->EndFrame();
            g_Supervisor.gfxDevice->SwapBuffers();
            if (120 <= frameCount && frameCount < 840 &&
                WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU | TH_BUTTON_BOMB))
            {
                frameCount = 840;
                g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            }
            frameCount++;
            g_SoundPlayer.ProcessQueues();
        }
        g_AnmManager->ReleaseSurface(0);
    }
    g_GameManager.phantasmUnlocked = g_GameManager.HasUnlockedPhantomAndMaxClears();
    this->gameState = STATE_PRE_INPUT;
    InitializeTimingVars(&g_Supervisor);
    switch (g_Supervisor.prevState)
    {
    case 2:
    case 3:
    case 6:
        this->cursor = g_GameManager.difficulty >= 4;
        break;
    case 5:
        this->cursor = 4;
        break;
    case 8:
        this->cursor = 5;
        break;
    default:
        this->cursor = 0;
        break;
    }
    this->isPracticeMode = 0;
    if (g_GameManager.practice)
    {
        this->cursor = 2;
        this->isPracticeMode = 1;
    }
    g_GameManager.practice = 0;
    if (g_Supervisor.prevState)
    {
        GameManager::DrawLoadingSprite();
    }
    if (g_AnmManager->LoadAnms(ANM_FILE_TITLE, "data/title01.anm", ANM_OFFSET_TITLE) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    if (!g_GameManager.demo)
    {
        if (g_Supervisor.prevState != 5)
        {
            g_Supervisor.LoadAudio(8, "bgm/th07_01.mid");
        }
        if (g_Supervisor.lastTotalPlayTimeUpdate == 0)
        {
            BombEffects::RegisterChain(0, 70, 0xffffff, 0, 0);
        }
        else
        {
            BombEffects::RegisterChain(0, 70, 0xffffff, 0, 0);
        }
    }
    for (i = 0; i < 14; i++)
    {
        g_AnmManager->SetAnmIdxAndExecuteScript(&this->vms[i], 1798);
        g_AnmManager->SetActiveSprite(&this->vms[i], this->vms[i].activeSpriteIdx + i);
    }
    this->cursorVm = this->vms;
    g_GameManager.demo = 0;
    g_GameManager.demoFrames = 0;
    Online::ResetLoadouts();
    Supervisor::DebugPrint("info : main menu ready\n");
    return ZUN_SUCCESS;
}

static bool MobileVmContainsPoint(const AnmVm &vm, f32 x, f32 y)
{
    if (!vm.active || !vm.visible || !vm.sprite)
    {
        return false;
    }
    const f32 scaleX = fabsf(vm.scale.x) > 0.001f ? fabsf(vm.scale.x) : 1.0f;
    const f32 scaleY = fabsf(vm.scale.y) > 0.001f ? fabsf(vm.scale.y) : 1.0f;
    f32 halfW = std::max(16.0f, vm.sprite->widthPx * scaleX * 0.5f);
    f32 halfH = std::max(10.0f, vm.sprite->heightPx * scaleY * 0.5f);
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
    const f32 padX = std::max(10.0f, halfW * 0.18f);
    const f32 padY = std::max(7.0f, halfH * 0.28f);
    return x >= left - padX && x <= right + padX && y >= top - padY && y <= bottom + padY;
}

static i32 MobileHitVmItems(AnmVm *items, i32 count, i32 stride, f32 x, f32 y)
{
    if (!items || count <= 0 || stride <= 0)
    {
        return -1;
    }
    for (i32 i = 0; i < count; ++i)
    {
        if (MobileVmContainsPoint(items[i * stride], x, y))
        {
            return i;
        }
    }
    return -1;
}

static i32 MobileNearestVerticalItem(AnmVm *items, i32 count, i32 stride, f32 x, f32 y)
{
    const i32 exact = MobileHitVmItems(items, count, stride, x, y);
    if (exact >= 0) return exact;
    if (!items || count <= 0 || stride <= 0 || x < 40.0f || x > 600.0f ||
        y < 80.0f || y > 440.0f)
    {
        return -1;
    }

    i32 nearest = -1;
    f32 nearestDistance = 1000000.0f;
    for (i32 i = 0; i < count; ++i)
    {
        const AnmVm &vm = items[i * stride];
        const f32 itemY = vm.pos.y + vm.offset.y;
        if (!std::isfinite(itemY)) continue;
        const f32 distance = fabsf(y - itemY);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = i;
        }
    }
    return nearestDistance <= 90.0f ? nearest : -1;
}

bool MainMenu::HandleMobileTouch(MobileUi::MenuTouchAction action, f32 gameX, f32 gameY,
                                 f32 delta)
{
    if (!this->vmHead || this->vmCount <= 0 || this->stateTimer < 4)
    {
        return false;
    }

    if (action != MobileUi::MENU_TOUCH_TAP)
    {
        u16 button = 0;
        const bool horizontalSelection =
            this->gameState == STATE_NORMAL_SELECT_CHARACTER ||
            this->gameState == STATE_PRACTICE_SELECT_CHARACTER ||
            this->gameState == STATE_EXTRA_SELECT_CHARACTER;
        const bool verticalSelection =
            this->gameState == STATE_NORMAL_SELECT_SHOTTYPE ||
            this->gameState == STATE_PRACTICE_SELECT_SHOTTYPE ||
            this->gameState == STATE_EXTRA_SELECT_SHOTTYPE;

        if (action == MobileUi::MENU_TOUCH_SWIPE_HORIZONTAL && horizontalSelection)
        {
            button = delta > 0.0f ? TH_BUTTON_RIGHT : TH_BUTTON_LEFT;
        }
        else if (action == MobileUi::MENU_TOUCH_SWIPE_VERTICAL && !horizontalSelection)
        {
            button = delta < 0.0f ? TH_BUTTON_UP : TH_BUTTON_DOWN;
        }
        else if (action == MobileUi::MENU_TOUCH_SWIPE_HORIZONTAL && !verticalSelection)
        {
            button = delta > 0.0f ? TH_BUTTON_RIGHT : TH_BUTTON_LEFT;
        }
        if (button)
        {
            // In a network session the caller queues this pulse for the next
            // lockstep frame. Mutating the current raw-input edge here would
            // make the later synchronized pulse look held and the menu would
            // ignore the tap (especially while a startup barrier is pending).
            if (!Online::IsNetworkSession())
            {
                g_LastFrameRawInput &= ~button;
                g_CurFrameRawInput |= button;
            }
            this->idleFrames = 0;
            MobileDiagnostics::Log("mobile/menu", "swipe state=%d button=0x%x delta=%.1f",
                                   this->gameState, button, delta);
        }
        return button != 0;
    }

    i32 hit = -1;
    switch (this->gameState)
    {
    case STATE_PRE_INPUT:
        if (this->menuSubState == 1)
        {
            // Online is a ninth title item placed alongside Quit because the
            // original title ANM contains only eight baked menu sprites.
            if (gameX >= 470.0f && gameX <= 610.0f &&
                gameY >= 415.0f && gameY <= 460.0f)
            {
                hit = 8;
            }
            else
            {
                hit = MobileHitVmItems(&this->vmHead[1], 8, 1, gameX, gameY);
            }
        }
        break;
    case STATE_OPTIONS:
        if (this->menuSubState == 1)
        {
            hit = MobileHitVmItems(&this->vmHead[9], 9, 1, gameX, gameY);
        }
        break;
    case STATE_KEY_CONFIG:
        if (this->menuSubState == 1)
        {
            hit = MobileHitVmItems(&this->vmHead[35], 12, 1, gameX, gameY);
        }
        break;
    case STATE_NORMAL_SELECT_DIFFICULTY:
    case STATE_PRACTICE_SELECT_DIFFICULTY:
        if (this->menuSubState == 1)
        {
            hit = MobileNearestVerticalItem(&this->vmHead[67], 4, 1, gameX, gameY);
            if (hit < 0 && gameX >= 60.0f && gameX <= 580.0f &&
                gameY >= 120.0f && gameY < 400.0f)
            {
                hit = std::clamp((i32)((gameY - 120.0f) / 70.0f), 0, 3);
            }
        }
        break;
    case STATE_EXTRA_SELECT_DIFFICULTY:
        if (this->menuSubState == 1)
        {
            const i32 count = g_GameManager.HasUnlockedPhantomAndMaxClears() ? 2 : 1;
            hit = MobileNearestVerticalItem(&this->vmHead[162], count, 1, gameX, gameY);
        }
        break;
    case STATE_NORMAL_SELECT_CHARACTER:
    case STATE_PRACTICE_SELECT_CHARACTER:
    case STATE_EXTRA_SELECT_CHARACTER:
        if (this->menuSubState == 1)
        {
            // This page is a carousel: a tap confirms the character currently
            // displayed. Horizontal swipes are the only way to change character.
            if (gameX >= 40.0f && gameX <= 600.0f && gameY >= 80.0f && gameY <= 450.0f)
            {
                hit = std::clamp(this->cursor, 0, 2);
            }
        }
        break;
    case STATE_NORMAL_SELECT_SHOTTYPE:
    case STATE_PRACTICE_SELECT_SHOTTYPE:
    case STATE_EXTRA_SELECT_SHOTTYPE:
        if (this->menuSubState == 1)
        {
            i32 base = 72;
            if (g_GameManager.character == CHAR_MARISA) base = 75;
            if (g_GameManager.character == CHAR_SAKUYA) base = 78;
            hit = MobileNearestVerticalItem(&this->vmHead[base], 2, 1, gameX, gameY);
            if (hit < 0 && gameX >= 50.0f && gameX <= 590.0f &&
                gameY >= 120.0f && gameY <= 440.0f)
            {
                hit = gameY < 280.0f ? 0 : 1;
            }
        }
        break;
    case STATE_SELECT_PRACTICE_STAGE:
        if (this->menuSubState == 1)
        {
            const i32 unlockedValue = (i32)g_GameManager
                                          .clrd[g_GameManager.character * 2 +
                                                g_GameManager.shotType]
                                          .difficultyClearedWithoutRetries
                                              [g_Supervisor.cfg.defaultDifficulty];
            const i32 unlocked = std::clamp(unlockedValue, 1, 6);
            hit = MobileHitVmItems(&this->vmHead[132], unlocked, 1, gameX, gameY);
            if (hit < 0 && gameX >= 90.0f && gameX <= 550.0f && gameY >= 160.0f && gameY < 340.0f)
            {
                hit = std::clamp((i32)((gameY - 176.0f) / 24.0f), 0, unlocked - 1);
            }
        }
        break;
    case STATE_SELECT_REPLAY:
        if (this->menuSubState == 1 && this->replayFilesNum > 0)
        {
            const i32 pageStart = this->cursor - this->cursor % 15;
            if (gameX >= 24.0f && gameX <= 616.0f && gameY >= 120.0f && gameY < 390.0f)
            {
                const i32 row = std::clamp((i32)((gameY - 132.0f) / 16.0f), 0, 14);
                hit = std::min(pageStart + row, this->replayFilesNum - 1);
            }
        }
        else if (this->menuSubState == 2 && this->currentReplay)
        {
            if (gameX >= 120.0f && gameX <= 540.0f && gameY >= 160.0f && gameY < 360.0f)
            {
                hit = std::clamp((i32)((gameY - 176.0f) / 24.0f), 0, 6);
                if (!this->currentReplay->head.stageReplayDataOffsets[hit]) hit = -1;
            }
        }
        else if (this->menuSubState == 3 && gameY >= 160.0f && gameY <= 380.0f)
        {
            hit = std::clamp((i32)((gameY - 190.0f) / 48.0f), 0, 2);
        }
        break;
    }

    if (hit < 0)
    {
        MobileDiagnostics::Log("mobile/menu", "miss state=%d sub=%d at=(%.1f,%.1f)",
                               this->gameState, this->menuSubState, gameX, gameY);
        return false;
    }

    const i32 oldCursor = this->cursor;
    // In a network session the target cursor is applied only when its input
    // frame is consumed on both devices. This keeps page transitions atomic.
    if (Online::IsNetworkSession())
        Online::QueueMenuCursor(hit);
    else
        this->cursor = hit;
    // Direct touch semantics: the item under the finger is selected and
    // confirmed. In a network session the caller has already queued the
    // confirmation for the next synchronized frame; changing the current
    // raw-input edge here would consume that edge before it is transmitted.
    if (!Online::IsNetworkSession())
    {
        g_LastFrameRawInput &= ~TH_BUTTON_SELECTMENU;
        g_CurFrameRawInput |= TH_BUTTON_SELECTMENU;
    }
    MobileDiagnostics::Log("mobile/menu", "tap-confirm state=%d sub=%d item=%d old=%d",
                           this->gameState, this->menuSubState, hit, oldCursor);
    this->idleFrames = 0;
    return true;
}

ZunResult MainMenu::AddedCallback(MainMenu *arg)
{
    MobileUi::SetMainMenuActive(true);
    const ZunResult result = arg->ActualAddedCallback();
    if (result != ZUN_SUCCESS)
    {
        MobileUi::SetMainMenuActive(false);
    }
    return result;
}

ZunResult MainMenu::Release()
{
    SAFE_FREE(this->currentReplay);
    if (this->vmHead)
    {
        delete[] this->vmHead;
        this->vmHead = NULL;
    }
    return ZUN_SUCCESS;
}

ZunResult MainMenu::DeletedCallback(MainMenu *arg)
{
    MobileUi::SetMainMenuActive(false);
    for (i32 i = 32; i <= 41; i++)
    {
        g_AnmManager->ReleaseAnm(i);
    }
    g_AnmManager->ReleaseSurface(0);
    g_Chain.Cut(arg->drawChain);
    arg->drawChain = NULL;
    arg->Release();
    delete arg;
    arg = NULL;

    return ZUN_SUCCESS;
}

ZunResult MainMenu::RegisterChain()
{
    MainMenu *mgr = new MainMenu;

    g_GameManager.isInPauseMenu = 0;
    mgr->calcChain = g_Chain.CreateElem((ChainCallback)OnUpdate);
    mgr->calcChain->arg = mgr;
    mgr->calcChain->addedCallback = (ChainLifecycleCallback)AddedCallback;
    mgr->calcChain->deletedCallback = (ChainLifecycleCallback)DeletedCallback;
    if (g_Chain.AddToCalcChain(mgr->calcChain, 3))
    {
        return ZUN_ERROR;
    }

    mgr->drawChain = g_Chain.CreateElem((ChainCallback)OnDraw);
    mgr->drawChain->arg = mgr;
    g_Chain.AddToDrawChain(mgr->drawChain, 0);

    return ZUN_SUCCESS;
}
