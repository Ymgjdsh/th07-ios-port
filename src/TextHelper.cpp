#include "TextHelper.hpp"

#include <SDL3/SDL_surface.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <string>
#include <vector>

#include "AnmManager.hpp"
#include "GameErrorContext.hpp"
#include "Localization.hpp"
#include "Supervisor.hpp"
#include "graphics/ZunGraphics.hpp"
#include "inttypes.hpp"
#include "sj2utf8/sj2utf8.h"

static TTF_Font *g_StockFont = nullptr;
static TTF_Font *g_ChineseFont = nullptr;
static bool g_TtfInitialized = false;

struct LocalizedRegion
{
    u32 textureId;
    i32 x;
    i32 y;
    i32 width;
    i32 height;
    bool localizedFont;
    bool centerHorizontal;
    std::string text;
};

static std::vector<LocalizedRegion> g_LocalizedRegions;

struct ScreenTextTexture
{
    std::string text;
    i32 fontSize;
    bool localizedFont;
    GfxTextureHandle texture;
    i32 width;
    i32 height;
};

static std::vector<ScreenTextTexture> g_ScreenTextTextures;

static i32 GetRenderFontSize(i32 fontHeight, bool localizedFont)
{
    const i32 stockSize = fontHeight * 2 - 2;
    if (!localizedFont)
        return stockSize;
    // Noto Sans SC has a smaller apparent glyph body than the stock font.
    return (stockSize * 118 + 50) / 100;
}

static void FitFontToRegion(TTF_Font *font, const char *text, i32 &fontSize,
                            i32 maxWidth, i32 maxHeight)
{
    for (; fontSize > 10; --fontSize)
    {
        TTF_SetFontSize(font, fontSize);
        i32 width = 0;
        i32 height = 0;
        if (!TTF_GetStringSize(font, text, 0, &width, &height) ||
            (width <= maxWidth && height <= maxHeight))
            return;
    }
    TTF_SetFontSize(font, fontSize);
}

static void ClearScreenTextTextures()
{
    if (g_Supervisor.gfxDevice)
    {
        for (const ScreenTextTexture &entry : g_ScreenTextTextures)
        {
            if (entry.texture)
                g_Supervisor.gfxDevice->DeleteTexture(entry.texture);
        }
    }
    g_ScreenTextTextures.clear();
}

static bool IsLocalizedRegionCurrent(GfxTextureHandle texture, i32 x, i32 y, i32 width,
                                     i32 height, bool localizedFont,
                                     bool centerHorizontal, const char *text)
{
    for (const LocalizedRegion &region : g_LocalizedRegions)
    {
        if (region.textureId == texture.id && region.x == x && region.y == y &&
            region.width == width && region.height == height &&
            region.localizedFont == localizedFont &&
            region.centerHorizontal == centerHorizontal &&
            region.text == (text ? text : ""))
            return true;
    }
    return false;
}

static TTF_Font *OpenTextFont(bool localizedFont)
{
    if (!g_TtfInitialized)
    {
        if (!TTF_Init())
        {
            g_GameErrorContext.Log("TTF_Init fail : %s\n", SDL_GetError());
            return nullptr;
        }
        g_TtfInitialized = true;
    }

    const bool useChineseFont = localizedFont &&
        Localization::GetLanguage() == Localization::Language::Chinese;
    TTF_Font **font = useChineseFont ? &g_ChineseFont : &g_StockFont;
    if (*font) return *font;

    const char *fontName = useChineseFont ? "NotoSansSC.ttf" : "msgothic.ttc";
    *font = TTF_OpenFont(FileSystem::GetBasePath(fontName).c_str(), 10);
    if (!*font)
    {
        g_GameErrorContext.Log("TTF_OpenFont fail (%s) : %s\n", fontName, SDL_GetError());
        return nullptr;
    }
    TTF_SetFontStyle(*font, TTF_STYLE_BOLD);
    return *font;
}

// stolen from
// https://stackoverflow.com/questions/3404199/how-to-find-out-the-encoding-of-a-file-c-sharp/3404317#3404317
bool IsUtf8(const char *string)
{
    i32 charByteCounter = 1;
    unsigned char curByte;

    size_t len = strlen(string);
    for (size_t i = 0; i < len; i++)
    {
        curByte = string[i];
        if (charByteCounter == 1)
        {
            if (curByte >= 0x80)
            {
                while (((curByte <<= 1) & 0x80) != 0)
                {
                    charByteCounter++;
                }
                if (charByteCounter == 1 || charByteCounter > 6)
                {
                    return false;
                }
            }
        }
        else
        {
            if ((curByte & 0xC0) != 0x80)
            {
                return false;
            }
            charByteCounter--;
        }
    }
    if (charByteCounter > 1)
    {
        return false;
    }

    return true;
}

TextHelper::TextHelper()
{
    this->buffer = NULL;
    this->width = 0;
    this->height = 0;
}

TextHelper::~TextHelper()
{
    ReleaseBuffer();
}

bool TextHelper::ReleaseBuffer()
{
    if (this->buffer)
    {
        SDL_DestroySurface(this->buffer);
        this->buffer = NULL;
        this->width = 0;
        this->height = 0;
        return true;
    }
    return false;
}

bool TextHelper::AllocateBuffer(i32 width, i32 height)
{
    ReleaseBuffer();
    this->buffer = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!this->buffer)
    {
        return false;
    }
    SDL_FillSurfaceRect(this->buffer, NULL, 0);
    this->width = width;
    this->height = height;
    return true;
}

bool TextHelper::InvertAlpha(i32 x, i32 y, i32 spriteWidth, i32 fontHeight, i32 param5)
{
    i32 doubleArea = spriteWidth * fontHeight * 2;
    if (doubleArea == 0 || !this->buffer)
    {
        return false;
    }

    SDL_LockSurface(this->buffer);
    u8 *pixels = (u8 *)this->buffer->pixels;
    i32 pitch = this->buffer->pitch;

    for (i32 py = 0; py < fontHeight; py++)
    {
        for (i32 px = 0; px < spriteWidth; px++)
        {
            u8 *p = &pixels[(py + y) * pitch + (px + x) * 4];
            u8 r = p[0];
            u8 g = p[1];
            u8 b = p[2];
            u8 a = p[3];

            if (a > 0)
            {
                i32 i = (py * spriteWidth + px) * 2;

                if (!param5)
                {
                    if (r >= b)
                    {
                        r = r - (r * i * 2) / doubleArea / 3;
                        g = g - (g * i * 2) / doubleArea / 3;
                    }
                    else
                    {
                        b = b - (b * i) / doubleArea / 2;
                        g = g - (g * i) / doubleArea / 2;
                    }
                }
                else
                {
                    if (r >= b)
                    {
                        r = r - (r * i) / doubleArea / 4;
                        g = g - (g * i) / doubleArea / 4;
                    }
                    else
                    {
                        b = b - (b * i) / doubleArea / 4;
                        g = g - (g * i) / doubleArea / 4;
                    }
                }

                p[0] = r;
                p[1] = g;
                p[2] = b;
                p[3] = a;
            }
            else
            {
                p[0] = 0;
                p[1] = 0;
                p[2] = 0;
                p[3] = 0;
            }
        }
    }

    SDL_UnlockSurface(this->buffer);
    return true;
}

bool TextHelper::CopyTextToTexture(i32 yPos, i32 spriteWidth, i32 spriteHeight, i32 fontHeight,
                                   i32 fontWidth, GfxTextureHandle outTexture)
{
    SDL_Surface *outSurface = SDL_CreateSurface(spriteWidth, spriteHeight, SDL_PIXELFORMAT_RGBA32);
    if (!outSurface)
    {
        return false;
    }
    SDL_FillSurfaceRect(outSurface, NULL, 0);

    SDL_Rect srcRect;
    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.w = spriteWidth * 2;
    srcRect.h = fontHeight * 2;
    if (srcRect.w > this->width)
    {
        srcRect.w = this->width;
    }
    if (srcRect.h > this->height)
    {
        srcRect.h = this->height;
    }

    SDL_Rect dstRect;
    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.w = spriteWidth;
    dstRect.h = fontWidth;

    SDL_StretchSurface(this->buffer, &srcRect, outSurface, &dstRect, SDL_SCALEMODE_LINEAR);

    g_Supervisor.gfxDevice->BindTexture(outTexture);
    g_Supervisor.gfxDevice->SetTextureSubImage(0, yPos, outSurface->w, fontWidth,
                                               outSurface->pixels);
    SDL_DestroySurface(outSurface);
    return true;
}

ZunResult TextHelper::CreateTextBuffer()
{
    return OpenTextFont(false) ? ZUN_SUCCESS : ZUN_ERROR;
}

void TextHelper::ReloadFont()
{
    ClearScreenTextTextures();
    if (g_StockFont) TTF_CloseFont(g_StockFont);
    if (g_ChineseFont) TTF_CloseFont(g_ChineseFont);
    g_StockFont = nullptr;
    g_ChineseFont = nullptr;
    CreateTextBuffer();
}

void TextHelper::ReleaseTextBuffer()
{
    ClearScreenTextTextures();
    if (g_StockFont) TTF_CloseFont(g_StockFont);
    if (g_ChineseFont) TTF_CloseFont(g_ChineseFont);
    g_StockFont = nullptr;
    g_ChineseFont = nullptr;
    if (g_TtfInitialized) TTF_Quit();
    g_TtfInitialized = false;
    g_LocalizedRegions.clear();
}

void TextHelper::InvalidateTexture(GfxTextureHandle texture)
{
    if (!texture) return;
    g_LocalizedRegions.erase(
        std::remove_if(g_LocalizedRegions.begin(), g_LocalizedRegions.end(),
                       [texture](const LocalizedRegion &region) {
                           return region.textureId == texture.id;
                       }),
        g_LocalizedRegions.end());
}

void TextHelper::RenderTextToTextureBold(i32 xPos, i32 yPos, i32 spriteWidth, i32 spriteHeight,
                                         i32 fontHeight, i32 fontWidth, u32 textColor,
                                         u32 outlineType, char *string, GfxTextureHandle outTexture,
                                         bool localizedFont)
{
    i32 fontSize = GetRenderFontSize(fontHeight, localizedFont);
    if (fontSize <= 0)
    {
        return;
    }

    TTF_Font *font = OpenTextFont(localizedFont);
    if (!font)
    {
        return;
    }

    char *convStr = string;
    bool needsFree = false;
    if (!IsUtf8(string))
    {
        std::string tmp = sj2utf8(string);
        if (!tmp.empty())
        {
            convStr = SDL_strdup(tmp.c_str());
            needsFree = true;
        }
    }

    const i32 maxWidth = std::max(1, spriteWidth * 2 - xPos * 2 - 6);
    const i32 maxHeight = std::max(1, fontHeight * 2 + 2);
    FitFontToRegion(font, convStr, fontSize, maxWidth, maxHeight);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *textSurf = TTF_RenderText_Blended(font, convStr, 0, white);
    if (needsFree)
    {
        SDL_free(convStr);
    }
    if (!textSurf)
    {
        return;
    }

    i32 dWidth = spriteWidth * 2;
    i32 dHeight = fontHeight * 2 + 6;
    if (dWidth > 1024)
    {
        dWidth = 1024;
    }
    if (dHeight > 64)
    {
        dHeight = 64;
    }
    if (dWidth <= 0 || dHeight <= 0)
    {
        SDL_DestroySurface(textSurf);
        return;
    }

    TextHelper textHelper;
    textHelper.AllocateBuffer(dWidth, dHeight);

    SDL_SetSurfaceBlendMode(textSurf, SDL_BLENDMODE_BLEND);

    SDL_SetSurfaceColorMod(textSurf, 0, 0, 0);
    SDL_Rect dstRect;
    if (outlineType != 0xffffffff)
    {
        i32 dx[4] = {4, 0, 2, 2};
        i32 dy[4] = {2, 2, 0, 4};
        for (i32 i = 0; i < 4; i++)
        {
            dstRect = {xPos * 2 + dx[i], dy[i], textSurf->w, textSurf->h};
            SDL_BlitSurface(textSurf, NULL, textHelper.buffer, &dstRect);
        }
    }
    else
    {
        i32 dx[4] = {3, 1, 2, 2};
        i32 dy[4] = {2, 2, 1, 3};
        for (i32 i = 0; i < 4; i++)
        {
            dstRect = {xPos * 2 + dx[i], dy[i], textSurf->w, textSurf->h};
            SDL_BlitSurface(textSurf, NULL, textHelper.buffer, &dstRect);
        }
    }

    u8 r = (textColor >> 16) & 0xFF;
    u8 g = (textColor >> 8) & 0xFF;
    u8 b_col = textColor & 0xFF;
    SDL_SetSurfaceColorMod(textSurf, r, g, b_col);
    dstRect = {xPos * 2 + 2, 2, textSurf->w, textSurf->h};
    SDL_BlitSurface(textSurf, NULL, textHelper.buffer, &dstRect);

    SDL_DestroySurface(textSurf);

    textHelper.InvertAlpha(0, 0, spriteWidth << 1, fontHeight * 2 + 6,
                           (u32)(outlineType == 0xffffffff));
    textHelper.CopyTextToTexture(yPos, spriteWidth, spriteHeight, fontHeight, fontWidth,
                                 outTexture);
}

void TextHelper::RenderTextToTextureRegion(i32 xPos, i32 yPos, i32 regionWidth,
                                           i32 regionHeight, i32 fontHeight, i32 fontWidth,
                                           u32 textColor, u32 outlineType, const char *string,
                                           GfxTextureHandle outTexture, bool localizedFont,
                                           bool centerHorizontal)
{
    if (!outTexture || regionWidth <= 0 || regionHeight <= 0)
        return;

    if (IsLocalizedRegionCurrent(outTexture, xPos, yPos, regionWidth, regionHeight,
                                 localizedFont, centerHorizontal, string))
        return;

    const char *source = string ? string : "";
    if (!*source)
    {
        SDL_Surface *clearSurface = SDL_CreateSurface(regionWidth, regionHeight,
                                                      SDL_PIXELFORMAT_RGBA32);
        if (!clearSurface)
            return;
        SDL_FillSurfaceRect(clearSurface, NULL, 0);
        g_Supervisor.gfxDevice->BindTexture(outTexture);
        g_Supervisor.gfxDevice->SetTextureSubImage(xPos, yPos, regionWidth, regionHeight,
                                                   clearSurface->pixels);
        SDL_DestroySurface(clearSurface);
        g_LocalizedRegions.erase(
            std::remove_if(g_LocalizedRegions.begin(), g_LocalizedRegions.end(),
                           [=](const LocalizedRegion &region) {
                               return region.textureId == outTexture.id && region.x == xPos &&
                                      region.y == yPos && region.width == regionWidth &&
                                      region.height == regionHeight;
                           }),
            g_LocalizedRegions.end());
        g_LocalizedRegions.push_back({outTexture.id, xPos, yPos, regionWidth, regionHeight,
                                      localizedFont, centerHorizontal, ""});
        return;
    }

    (void)fontWidth;

    i32 fontSize = GetRenderFontSize(fontHeight, localizedFont);
    TTF_Font *font = OpenTextFont(localizedFont);
    if (fontSize <= 0 || !font)
        return;

    char *convStr = const_cast<char *>(source);
    bool needsFree = false;
    if (!IsUtf8(source))
    {
        std::string tmp = sj2utf8(source);
        convStr = SDL_strdup(tmp.c_str());
        needsFree = true;
    }

    FitFontToRegion(font, convStr, fontSize, regionWidth * 2 - 8,
                    regionHeight * 2 - 8);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *textSurf = TTF_RenderText_Blended(font, convStr, 0, white);
    if (needsFree)
        SDL_free(convStr);
    if (!textSurf)
        return;

    // Render at 2x resolution to match the existing text path, then scale
    // back into the exact atlas sprite rectangle before uploading it.
    const i32 bufferWidth = regionWidth * 2;
    const i32 bufferHeight = regionHeight * 2;
    if (bufferWidth <= 0 || bufferHeight <= 0 || bufferWidth > 4096 || bufferHeight > 512)
    {
        SDL_DestroySurface(textSurf);
        return;
    }

    TextHelper textHelper;
    if (!textHelper.AllocateBuffer(bufferWidth, bufferHeight))
    {
        SDL_DestroySurface(textSurf);
        return;
    }

    SDL_SetSurfaceBlendMode(textSurf, SDL_BLENDMODE_BLEND);
    const i32 textX = centerHorizontal && textSurf->w < bufferWidth
                          ? (bufferWidth - textSurf->w) / 2
                          : 2;
    const i32 textY = textSurf->h < bufferHeight ? (bufferHeight - textSurf->h) / 2 : 0;

    SDL_SetSurfaceColorMod(textSurf, 0, 0, 0);
    const i32 outlineDx[4] = {4, 0, 2, 2};
    const i32 outlineDy[4] = {2, 2, 0, 4};
    for (i32 i = 0; i < 4; ++i)
    {
        SDL_Rect dst = {textX + outlineDx[i], textY + outlineDy[i], textSurf->w,
                        textSurf->h};
        SDL_BlitSurface(textSurf, NULL, textHelper.buffer, &dst);
    }

    const u8 r = (textColor >> 16) & 0xFF;
    const u8 g = (textColor >> 8) & 0xFF;
    const u8 b = textColor & 0xFF;
    SDL_SetSurfaceColorMod(textSurf, r, g, b);
    SDL_Rect textDst = {textX + 2, textY + 2, textSurf->w, textSurf->h};
    SDL_BlitSurface(textSurf, NULL, textHelper.buffer, &textDst);
    SDL_DestroySurface(textSurf);

    textHelper.InvertAlpha(0, 0, bufferWidth, bufferHeight,
                           (u32)(outlineType == 0xffffffff));
    SDL_Surface *outSurface = SDL_CreateSurface(regionWidth, regionHeight,
                                                SDL_PIXELFORMAT_RGBA32);
    if (!outSurface)
        return;
    SDL_FillSurfaceRect(outSurface, NULL, 0);
    SDL_Rect srcRect = {0, 0, bufferWidth, bufferHeight};
    SDL_Rect dstRect = {0, 0, regionWidth, regionHeight};
    SDL_StretchSurface(textHelper.buffer, &srcRect, outSurface, &dstRect,
                       SDL_SCALEMODE_LINEAR);

    g_Supervisor.gfxDevice->BindTexture(outTexture);
    g_Supervisor.gfxDevice->SetTextureSubImage(xPos, yPos, regionWidth, regionHeight,
                                               outSurface->pixels);
    SDL_DestroySurface(outSurface);
    g_LocalizedRegions.erase(
        std::remove_if(g_LocalizedRegions.begin(), g_LocalizedRegions.end(),
                       [=](const LocalizedRegion &region) {
                           return region.textureId == outTexture.id && region.x == xPos &&
                                  region.y == yPos && region.width == regionWidth &&
                                  region.height == regionHeight;
                       }),
        g_LocalizedRegions.end());
    g_LocalizedRegions.push_back({outTexture.id, xPos, yPos, regionWidth, regionHeight,
                                  localizedFont, centerHorizontal, string ? string : ""});
}

void TextHelper::DrawScreenText(f32 x, f32 y, i32 fontHeight, u32 color,
                                const char *string, f32 scale, bool localizedFont)
{
    if (!string || !*string || scale <= 0.0f || !g_Supervisor.gfxDevice)
        return;

    TTF_Font *font = OpenTextFont(localizedFont);
    if (!font)
        return;
    const i32 fontSize = GetRenderFontSize(fontHeight, localizedFont);

    ScreenTextTexture *cached = nullptr;
    for (ScreenTextTexture &entry : g_ScreenTextTextures)
    {
        if (entry.text == string && entry.fontSize == fontSize &&
            entry.localizedFont == localizedFont)
        {
            cached = &entry;
            break;
        }
    }
    if (!cached)
    {
        TTF_SetFontSize(font, fontSize);
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface *rendered = TTF_RenderText_Blended(font, string, 0, white);
        if (!rendered)
            return;
        SDL_Surface *rgba = SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(rendered);
        if (!rgba)
            return;

        ScreenTextTexture entry = {};
        entry.text = string;
        entry.fontSize = fontSize;
        entry.localizedFont = localizedFont;
        entry.width = rgba->w;
        entry.height = rgba->h;
        entry.texture = g_Supervisor.gfxDevice->CreateTexture();
        g_Supervisor.gfxDevice->BindTexture(entry.texture);
        g_Supervisor.gfxDevice->SetTextureImage(entry.width, entry.height, PIXEL_RGBA,
                                                PIXEL_UNSIGNED_BYTE, rgba->pixels);
        SDL_DestroySurface(rgba);
        g_ScreenTextTextures.push_back(entry);
        cached = &g_ScreenTextTextures.back();
    }

    const f32 width = cached->width * 0.5f * scale;
    const f32 height = cached->height * 0.5f * scale;
    VertexTex1DiffuseXyzrhw vertices[4] = {};
    vertices[0].pos = ZunVec3(x, y, 0.0f);
    vertices[1].pos = ZunVec3(x + width, y, 0.0f);
    vertices[2].pos = ZunVec3(x, y + height, 0.0f);
    vertices[3].pos = ZunVec3(x + width, y + height, 0.0f);
    vertices[0].textureUV = {0.0f, 0.0f};
    vertices[1].textureUV = {1.0f, 0.0f};
    vertices[2].textureUV = {0.0f, 1.0f};
    vertices[3].textureUV = {1.0f, 1.0f};
    for (VertexTex1DiffuseXyzrhw &vertex : vertices)
    {
        vertex.w = 1.0f;
        vertex.color.color = color;
    }

    g_AnmManager->Flush();
    g_Supervisor.gfxDevice->BindTexture(cached->texture);
    g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ALPHA);
    g_Supervisor.gfxDevice->SetTextureArg(TEX_ARG_DIFFUSE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->DrawPrimitiveUP(PRIM_TRIANGLE_STRIP, 2, vertices,
                                            sizeof(VertexTex1DiffuseXyzrhw));
    // Direct drawing bypasses AnmManager's cached texture binding.
    g_AnmManager->currentTexture = GfxTextureHandle();
}

i32 TextHelper::GetLogicalStringWidth(const char *str)
{
    if (!IsUtf8(str))
    {
        return strlen(str);
    }

    i32 width = 0;
    while (*str)
    {
        if ((*str & 0x80) == 0)
        {
            width += 1;
            str += 1;
        }
        else if ((*str & 0xE0) == 0xC0)
        {
            width += 2;
            str += 2;
        }
        else if ((*str & 0xF0) == 0xE0)
        {
            width += 2;
            str += 3;
        }
        else if ((*str & 0xF8) == 0xF0)
        {
            width += 2;
            str += 4;
        }
        else
        {
            str++;
        }
    }
    return width;
}
