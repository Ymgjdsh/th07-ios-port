#pragma once

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "ZunResult.hpp"
#include "graphics/ZunGraphics.hpp"
#include "inttypes.hpp"

struct TextHelper
{
    TextHelper();
    ~TextHelper();

    bool AllocateBuffer(i32 width, i32 height);
    bool ReleaseBuffer();
    bool InvertAlpha(i32 x, i32 y, i32 spriteWidth, i32 fontHeight, i32 param5);
    bool CopyTextToTexture(i32 yPos, i32 spriteWidth, i32 spriteHeight, i32 fontHeight,
                           i32 fontWidth, GfxTextureHandle outTexture);

    static ZunResult CreateTextBuffer();
    static void ReloadFont();
    static void ReleaseTextBuffer();
    static void RenderTextToTextureBold(i32 xPos, i32 yPos, i32 spriteWidth, i32 spriteHeight,
                                        i32 fontHeight, i32 fontWidth, u32 textColor,
                                        u32 outlineType, char *string, GfxTextureHandle outTexture);
    // Render into an isolated sprite rectangle and upload exactly that
    // rectangle. This is used for localized menu labels stored in ANM
    // texture atlases; it must not touch neighbouring atlas sprites.
    static void RenderTextToTextureRegion(i32 xPos, i32 yPos, i32 regionWidth,
                                          i32 regionHeight, i32 fontHeight, i32 fontWidth,
                                          u32 textColor, u32 outlineType, const char *string,
                                          GfxTextureHandle outTexture);
    static i32 GetLogicalStringWidth(const char* str);

    SDL_Surface *buffer;
    i32 width;
    i32 height;
};
