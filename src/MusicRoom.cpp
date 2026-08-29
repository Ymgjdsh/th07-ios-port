#include "MusicRoom.hpp"

#include "AnmIdx.hpp"
#include "AnmManager.hpp"
#include "AsciiManager.hpp"
#include "Chain.hpp"
#include "Controller.hpp"
#include "FileSystem.hpp"
#include "Localization.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"

static void DrawMusicText(AnmVm *vm, u32 textColor, u32 outlineType,
                          const char *text)
{
    if (Localization::GetLanguage() == Localization::Language::Japanese)
    {
        AnmManager::DrawVmTextFmt(g_AnmManager, vm, textColor, outlineType,
                                  "%s", text ? text : "");
    }
    else
    {
        g_AnmManager->DrawTextToSpriteRegion(vm, textColor, outlineType,
                                             text ? text : "", true, false);
    }
}

ZunResult MusicRoom::CheckInputEnable()
{
    i32 i;

    if (this->waitFramesCounter == 0)
    {
        for (i = 0; i < 31; i++)
        {
            if (this->cursor == i)
            {
                this->titleSprites[i].pendingInterrupt = 1;
            }
            else
            {
                this->titleSprites[i].pendingInterrupt = 2;
            }
        }
        for (i = 0; i < 8; i++)
        {
            this->descriptionSprites[i].pendingInterrupt = 1;
        }
    }
    if (this->waitFramesCounter >= 8)
    {
        this->enableInput = 1;
    }
    return ZUN_SUCCESS;
}

i32 MusicRoom::ProcessInput()
{
    char local_54[256];
    i32 i;

    if (WAS_PRESSED_RAW(TH_BUTTON_UP))
    {
        this->cursor--;
        if (this->cursor < 0)
        {
            this->cursor = this->numDescriptors - 1;
            this->listingOffset = this->numDescriptors - 10;
            if (this->listingOffset < 0)
            {
                this->listingOffset = 0;
            }
        }
        else if (this->listingOffset > this->cursor)
        {
            this->listingOffset = this->cursor;
        }
        for (i = 0; i < 31; i++)
        {
            if (this->cursor == i)
            {
                this->titleSprites[i].pendingInterrupt = 1;
            }
            else
            {
                this->titleSprites[i].pendingInterrupt = 2;
            }
        }
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_DOWN))
    {
        this->cursor = this->cursor + 1;
        if (this->cursor >= this->numDescriptors)
        {
            this->cursor = 0;
            this->listingOffset = 0;
        }
        else
        {
            if (this->listingOffset <= this->cursor - 10)
            {
                this->listingOffset = this->cursor - 9;
            }
        }
        for (i = 0; i < 31; i++)
        {
            if (this->cursor == i)
            {
                this->titleSprites[i].pendingInterrupt = 1;
            }
            else
            {
                this->titleSprites[i].pendingInterrupt = 2;
            }
        }
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
    {
        this->selectedIdx = this->cursor;
        if (g_Supervisor.cfg.preloadBgm)
        {
            g_SoundPlayer.StartBGM("thbgm.dat");
        }
        g_Supervisor.PlayAudio(this->trackDescriptors[this->selectedIdx].path);
        for (i = 0; i < 8; i++)
        {
            memset(local_54, 0, sizeof(local_54));
            SDL_strlcpy(local_54,
                        this->trackDescriptors[this->selectedIdx].description[i],
                        sizeof(local_54));
            if (local_54[0] != '\0')
            {
                this->descriptionSprites[i].active = 1;
                DrawMusicText(this->descriptionSprites + i, 0xffe0c0, 0x300000,
                              local_54);
            }
            else
            {
                this->descriptionSprites[i].active = 0;
            }
            this->descriptionSprites[i].pendingInterrupt = 1;
        }
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
    {
        g_Supervisor.curState = 1;
        return 1;
    }

    return 0;
}

u32 MusicRoom::OnUpdate(MusicRoom *arg)
{
    i32 iVar1;
    i32 i;

    arg->UpdatePrev();

    iVar1 = arg->enableInput;
recheck:
    switch (arg->enableInput)
    {
    case 0:
        if (!arg->CheckInputEnable())
        {
            break;
        }
        goto recheck;
    case 1:
        if (arg->ProcessInput())
        {
            return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
        }
    default:
        break;
    }
    if (iVar1 != arg->enableInput)
    {
        arg->waitFramesCounter = 0;
    }
    else
    {
        arg->waitFramesCounter++;
    }
    g_AnmManager->ExecuteScript(&arg->vm[0]);
    for (i = 0; i < 31; i++)
    {
        g_AnmManager->ExecuteScript(&arg->titleSprites[i]);
    }
    for (i = 0; i < 8; i++)
    {
        g_AnmManager->ExecuteScript(&arg->descriptionSprites[i]);
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 MusicRoom::OnDraw(MusicRoom *arg)
{
    ZunVec3 local_18;
    char local_c[4];
    i32 i;

    local_c[0] = 127;
    local_c[1] = 0;
    g_AnmManager->SetTexture(0);
    g_AnmManager->CopySurfaceToBackBuffer(0, 0, 0, 0, 0);
    g_AnmManager->DrawInterpNoRotation(&arg->vm[0]);
    for (i = arg->listingOffset; i < arg->listingOffset + 10; i++)
    {
        if (i >= arg->numDescriptors)
        {
            break;
        }
        g_AsciiManager.SetColor(arg->titleSprites[i].color.color);
        arg->titleSprites[i].pos.x = 93.0f;
        arg->titleSprites[i].pos.y = (f32)((i + 1 - arg->listingOffset) * 18) + 104.0f - 20.0f;
        arg->titleSprites[i].pos.z = 0.0f;
        g_AnmManager->DrawInterpNoRotation(arg->titleSprites + i);
        local_18 = arg->titleSprites[i].pos;
        local_18.x -= 60.0f;
        if (arg->cursor == i)
        {
            g_AsciiManager.AddString(&local_18, local_c);
        }
        local_18.x += 15.0f;
        AsciiManager::AddFormatText(&g_AsciiManager, &local_18, "%2d.", i + 1);
    }
    i++;
    for (i = 0; i < 8; i++)
    {
        g_AnmManager->DrawInterpNoRotation(&arg->descriptionSprites[i]);
    }
    g_AsciiManager.color = 0xffffffff;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

ZunResult MusicRoom::AddedCallback(MusicRoom *arg)
{
    char lineCharBuffer[256];
    char *firstChar;
    i32 charIdx;
    char *curChar;
    i32 lineIdx;
    i32 offset;
    if (g_AnmManager->LoadSurface(0, "data/result/music.jpg") != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
    if (g_AnmManager->LoadAnms(ANM_FILE_MUSIC, "data/music00.anm", ANM_OFFSET_MUSIC) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    g_AnmManager->SetAnmIdxAndExecuteScript(&arg->vm[0], 2304);
    arg->waitFramesCounter = 0;
    curChar = (char *)FileSystem::OpenFile("data/musiccmt.txt", 0);
    firstChar = curChar;
    if ((u8 *)curChar == NULL)
    {
        return ZUN_ERROR;
    }

    arg->trackDescriptors = new TrackDescriptor[32];
    offset = -1;
    while (((uintptr_t)curChar - (uintptr_t)firstChar) < g_LastFileSize)
    {
        if (*curChar == '@')
        {
            curChar++;
            offset++;
            charIdx = 0;
            while (*curChar != '\n' && *curChar != '\r')
            {
                if (charIdx < (i32)sizeof(arg->trackDescriptors[offset].path) - 1)
                    arg->trackDescriptors[offset].path[charIdx++] = *curChar;
                curChar++;
                if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                {
                    goto LAB_0043b195;
                }
            }
            while (*curChar == '\n' || *curChar == '\r')
            {
                curChar++;
                if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                {
                    goto LAB_0043b195;
                }
            }
            charIdx = 0;
            while (*curChar != '\n' && *curChar != '\r')
            {
                if (charIdx < (i32)sizeof(arg->trackDescriptors[offset].title) - 1)
                    arg->trackDescriptors[offset].title[charIdx++] = *curChar;
                curChar++;
                if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                {
                    goto LAB_0043b195;
                }
            }
            while (*curChar == '\n' && *curChar == '\r')
            {
                curChar++;
                if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                {
                    goto LAB_0043b195;
                }
            }
            for (lineIdx = 0; lineIdx < 8; lineIdx++)
            {
                if (*curChar == '@')
                {
                    break;
                }

                memset(arg->trackDescriptors[offset].description[lineIdx], 0,
                       sizeof(arg->trackDescriptors[offset].description[lineIdx]));
                charIdx = 0;
                while (*curChar != '\n' && *curChar != '\r')
                {
                    if (charIdx < (i32)sizeof(
                                      arg->trackDescriptors[offset].description[lineIdx]) - 1)
                        arg->trackDescriptors[offset].description[lineIdx][charIdx++] = *curChar;
                    curChar++;
                    if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                    {
                        goto LAB_0043b195;
                    }
                }
                while (*curChar == '\n' || *curChar == '\r')
                {
                    curChar++;
                    if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                    {
                        goto LAB_0043b195;
                    }
                }
            }
        }
        else
        {
            curChar++;
        }
    }
LAB_0043b195:
    arg->numDescriptors = offset + 1;
    if (Localization::GetLanguage() != Localization::Language::Japanese)
    {
        for (i32 track = 0; track < arg->numDescriptors; ++track)
        {
            const std::string title = Localization::TranslateMusicTitle(
                track, arg->trackDescriptors[track].title);
            SDL_strlcpy(arg->trackDescriptors[track].title, title.c_str(),
                        sizeof(arg->trackDescriptors[track].title));
            snprintf(arg->trackDescriptors[track].description[0],
                     sizeof(arg->trackDescriptors[track].description[0]),
                     "No.%d %s", track + 1, title.c_str());
            for (i32 line = 0; line < 7; ++line)
            {
                const std::string comment = Localization::TranslateMusicComment(
                    track, line, "");
                SDL_strlcpy(arg->trackDescriptors[track].description[line + 1],
                            comment.c_str(),
                            sizeof(arg->trackDescriptors[track].description[line + 1]));
            }
        }
    }
    for (offset = 0; offset < arg->numDescriptors; offset++)
    {
        g_AnmManager->SetAnmIdxAndExecuteScript(&arg->titleSprites[offset], offset + 2305);
        DrawMusicText(arg->titleSprites + offset, 0xc0e0ff, 0x302080,
                      arg->trackDescriptors[offset].title);
        arg->titleSprites[offset].pos.x = 93.0f;
        arg->titleSprites[offset].pos.y = (f32)((offset + 1) * 18) + 104.0f - 20.0f;
        arg->titleSprites[offset].pos.z = 0.0f;
        arg->titleSprites[offset].anchor = 3;
    }
    for (offset = 0; offset < 8; offset++)
    {
        g_AnmManager->SetAnmIdxAndExecuteScript(&arg->descriptionSprites[offset], offset + 1799);
        memset(lineCharBuffer, 0, sizeof(lineCharBuffer));
        SDL_strlcpy(lineCharBuffer,
                    arg->trackDescriptors[arg->selectedIdx].description[offset],
                    sizeof(lineCharBuffer));
        if (*lineCharBuffer != '\0')
        {
            arg->descriptionSprites[offset].active = 1;
            DrawMusicText(arg->descriptionSprites + offset, 0xffe0c0, 0x300000,
                          lineCharBuffer);
        }
        else
        {
            arg->descriptionSprites[offset].active = 0;
        }
    }
    free(firstChar);
    return ZUN_SUCCESS;
}

ZunResult MusicRoom::DeletedCallback(MusicRoom *arg)
{
    delete[] arg->trackDescriptors;
    arg->trackDescriptors = NULL;
    g_AnmManager->ReleaseSurface(0);
    g_AnmManager->ReleaseAnm(46);
    g_AnmManager->ReleaseAnm(47);
    g_Chain.Cut(arg->drawChain);
    arg->drawChain = NULL;
    return ZUN_SUCCESS;
}

ZunResult MusicRoom::RegisterChain()
{
    static MusicRoom g_MusicRoom;
    MusicRoom *musicRoom = &g_MusicRoom;

    musicRoom->calcChain = g_Chain.CreateElem((ChainCallback)OnUpdate);
    musicRoom->calcChain->arg = musicRoom;
    musicRoom->calcChain->addedCallback = (ChainLifecycleCallback)AddedCallback;
    musicRoom->calcChain->deletedCallback = (ChainLifecycleCallback)DeletedCallback;
    if (g_Chain.AddToCalcChain(musicRoom->calcChain, 3))
    {
        return ZUN_ERROR;
    }

    musicRoom->drawChain = g_Chain.CreateElem((ChainCallback)OnDraw);
    musicRoom->drawChain->arg = musicRoom;
    g_Chain.AddToDrawChain(musicRoom->drawChain, 0);
    return ZUN_SUCCESS;
}
