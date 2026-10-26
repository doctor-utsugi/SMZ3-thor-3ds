#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pc_font.h"
#include "pc_gfx.h"

static SDL_Renderer *R;
static SDL_Texture *font;

static const SDL_Color PALETTE[] = {
    {0x10, 0x14, 0x1C, 255}, {0x1D, 0x24, 0x30, 255}, {0x2E, 0x4A, 0x6B, 255}, {0xE8, 0xEC, 0xF2, 255}, {0x87, 0x93, 0xA6, 255},
    {0xE9, 0xA2, 0x3B, 255}, {0xE0, 0x5A, 0x4F, 255}, {0x4C, 0xC3, 0x8A, 255}, {0x4F, 0xC3, 0xF7, 255}, {0x14, 0x50, 0x4A, 255},
    {0x1B, 0x2F, 0x6B, 255}, {0x4B, 0x2A, 0x7A, 255}, {0x8A, 0x64, 0x14, 255},
};

SDL_Color gfx_ui_color(int i) { return PALETTE[i < 0 ? 0 : i > 12 ? 12 : i]; }

void gfx_init(SDL_Renderer *r)
{
    R = r;
    int w = PC_FONT_W, h = PC_FONT_H;
    uint32_t *px = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) px[i] = 0x00FFFFFFu | ((uint32_t)PC_FONT_ALPHA[i] << 24);
    font = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(font, NULL, px, w * 4);
    SDL_SetTextureBlendMode(font, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(font, SDL_ScaleModeLinear);
    free(px);
}

/* the atlas glyphs are drawn at 24 px with the baseline 24 px below the cell top; y is the cell top */
float gfx_text_width(float px, const char *s) { return (float)strlen(s) * PC_FONT_CW * px / 24.0f; }

void gfx_text(float x, float y, float px, SDL_Color c, int align, const char *s)
{
    float f = px / 24.0f, adv = PC_FONT_CW * f, w = (float)strlen(s) * adv;
    if (align == 1) x -= w / 2; else if (align == 2) x -= w;
    SDL_SetTextureColorMod(font, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(font, c.a);
    for (; *s; s++, x += adv) {
        int ch = (unsigned char)*s;
        if (ch < 33 || ch > 126) { if (ch == 32) continue; ch = '?'; }
        int i = ch - 32;
        SDL_Rect src = {(i % PC_FONT_COLS) * PC_FONT_CW, (i / PC_FONT_COLS) * PC_FONT_CH, PC_FONT_CW, PC_FONT_CH};
        SDL_FRect dst = {x, y, PC_FONT_CW * f, PC_FONT_CH * f};
        SDL_RenderCopyF(R, font, &src, &dst);
    }
}

void gfx_rect(float x, float y, float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(R, c.a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_FRect r = {x, y, w, h};
    SDL_RenderFillRectF(R, &r);
}

float gfx_ui(const SmzUi *u, int win_w, int win_h, float *ox, float *oy)
{
    float k = (float)win_w / UI_W < (float)win_h / UI_H ? (float)win_w / UI_W : (float)win_h / UI_H;
    *ox = (win_w - UI_W * k) / 2; *oy = (win_h - UI_H * k) / 2;
    gfx_rect(0, 0, (float)win_w, (float)win_h, PALETTE[0]);
    for (int i = 0; i < u->cmd_count; i++) {
        const UiCmd *c = &u->cmd[i];
        float x = *ox + c->x * k, y = *oy + c->y * k;
        SDL_Color col = gfx_ui_color(c->color);
        if (c->kind == 0) {
            float l = roundf(x), t = roundf(y);
            gfx_rect(l, t, roundf(x + c->w * k) - l, roundf(y + c->h * k) - t, col);
        } else if (c->kind == 3) {
            SDL_Color end = gfx_ui_color(c->align);
            float l = roundf(x), t = roundf(y), r = roundf(x + c->w * k), b = roundf(y + c->h * k);
            SDL_Color a = col, e = end; a.a = e.a = 191;
            SDL_Vertex v[4] = {{{l, t}, a, {0, 0}}, {{r, t}, e, {0, 0}}, {{r, b}, e, {0, 0}}, {{l, b}, a, {0, 0}}};
            int idx[6] = {0, 1, 2, 0, 2, 3};
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(R, NULL, v, 4, idx, 6);
        } else if (c->kind == 1) {
            gfx_text(x, y - 2 * k, 12 * k, col, c->align, c->text);
        }
    }
    return k;
}
