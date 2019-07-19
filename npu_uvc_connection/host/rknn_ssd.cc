/*
 * Copyright (C) 2019 Hertz Wang 1989wanghang@163.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 *
 * Any non-GPL usage of this software or parts of this software is strictly
 * forbidden.
 *
 */

#define IN_RKNN_SSD_CC

#include <assert.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define FONT_FILE_PATH "/usr/lib/fonts/DejaVuSansMono.ttf"
static SDL_Color white = {0xFF, 0xFF, 0xFF, 0x00};
static SDL_Color red = {0x00, 0x00, 0xFF, 0xFF};
// static SDL_Color title_color = {0x06, 0xEB, 0xFF, 0xFF};
class SDLTTF {
public:
  SDLTTF() {
    if (TTF_Init() < 0)
      fprintf(stderr, "Couldn't initialize TTF: %s\n", SDL_GetError());
  }
  ~SDLTTF() { TTF_Quit(); }
};
static SDLTTF sdl_ttf;

class SDLFont {
public:
  SDLFont(SDL_Color forecol, int ptsize);
  ~SDLFont();
  SDL_Surface *DrawString(char *str, int str_length);
  SDL_Surface *GetFontPicture(char *str, int str_length, int bpp, int *w,
                              int *h);
  SDL_Color fore_col;
  SDL_Color back_col;
  int renderstyle;
  enum { RENDER_LATIN1, RENDER_UTF8, RENDER_UNICODE } rendertype;
  int pt_size;
  TTF_Font *font;
};

SDLFont::SDLFont(SDL_Color forecol, int ptsize)
    : fore_col(forecol), back_col(white), renderstyle(TTF_STYLE_NORMAL),
      rendertype(RENDER_UTF8), pt_size(ptsize), font(NULL) {
  font = TTF_OpenFont(FONT_FILE_PATH, ptsize);
  if (font == NULL) {
    fprintf(stderr, "hehe Couldn't load %d pt font from %s: %s\n", ptsize,
            FONT_FILE_PATH, SDL_GetError());
    return;
  }
  TTF_SetFontStyle(font, renderstyle);
}

SDLFont::~SDLFont() {
  if (font) {
    TTF_CloseFont(font);
  }
}

SDL_Surface *SDLFont::DrawString(char *str, int str_length) {
  SDL_Surface *text = NULL;
  switch (rendertype) {
  case RENDER_LATIN1:
    text = TTF_RenderText_Blended(font, str, fore_col);
    break;

  case RENDER_UTF8:
    text = TTF_RenderUTF8_Blended(font, str, fore_col);
    break;

  case RENDER_UNICODE: {
    Uint16 *unicode_text = (Uint16 *)malloc(2 * str_length + 1);
    if (!unicode_text)
      break;
    int index;
    for (index = 0; (str[0] || str[1]); ++index) {
      unicode_text[index] = ((Uint8 *)str)[0];
      unicode_text[index] <<= 8;
      unicode_text[index] |= ((Uint8 *)str)[1];
      str += 2;
    }
    unicode_text[index] = 0;
    text = TTF_RenderUNICODE_Blended(font, unicode_text, fore_col);
    free(unicode_text);
  } break;
  default:
    /* This shouldn't happen */
    break;
  }
  return text;
}

// static int power_of_two(int input)
// {
//     int value = 1;

//     while ( value < input ) {
//         value <<= 1;
//     }
//     return value;
// }

SDL_Surface *SDLFont::GetFontPicture(char *str, int str_length, int bpp, int *w,
                                     int *h) {
  SDL_Surface *image;
  SDL_Rect area;
  // Uint8  saved_alpha;
  // SDL_BlendMode saved_mode;
  if (str_length <= 0)
    return NULL;
  SDL_Surface *text = DrawString(str, str_length);
  if (!text) {
    fprintf(stderr, "draw %s to picture failed\n", str);
    return NULL;
  }
  *w = text->w; // power_of_two(text->w);
  *h = text->h; // power_of_two(text->h);
  if (bpp == 32)
    return text;
  image = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, *w, *h, bpp,
                                         SDL_PIXELFORMAT_RGB24);
  if (image == NULL) {
    fprintf(stderr, "SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
    return NULL;
  }
  /* Save the alpha blending attributes */
  // SDL_GetSurfaceAlphaMod(text, &saved_alpha);
  // SDL_SetSurfaceAlphaMod(text, 0xFF);
  // SDL_GetSurfaceBlendMode(text, &saved_mode);
  // SDL_SetSurfaceBlendMode(text, SDL_BLENDMODE_NONE);
  /* Copy the text into the GL texture image */
  area.x = 0;
  area.y = 0;
  area.w = text->w;
  area.h = text->h;
  SDL_BlitSurface(text, &area, image, &area);
  /* Restore the alpha blending attributes */
  // SDL_SetSurfaceAlphaMod(text, saved_alpha);
  // SDL_SetSurfaceBlendMode(text, saved_mode);
  SDL_FreeSurface(text);
  return image;
}

static SDL_Texture *load_texture(SDL_Surface *sur, SDL_Renderer *render,
                                 SDL_Rect *texture_dimensions) {
  SDL_Texture *texture = SDL_CreateTextureFromSurface(render, sur);
  assert(texture);
  Uint32 pixelFormat;
  int access;
  texture_dimensions->x = 0;
  texture_dimensions->y = 0;
  SDL_QueryTexture(texture, &pixelFormat, &access, &texture_dimensions->w,
                   &texture_dimensions->h);
  return texture;
}

namespace NPU_UVC_SSD_DEMO {

#include "../../../../../../../external/rknpu/rknn/rknn_api/examples/rknn_ssd_demo/src/ssd.cc"

static SDLFont sdl_font(red, 16);
bool SSDDraw(SDL_Renderer *renderer, const SDL_Rect &render_rect,
             void *pp_output, int npu_w, int npu_h);
bool SSDDraw(SDL_Renderer *renderer, const SDL_Rect &render_rect,
             void *pp_output, int npu_w, int npu_h) {
  auto group = (NPU_UVC_SSD_DEMO::detect_result_group_t *)pp_output;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(renderer, 0xFF, 0x10, 0xEB, 0xFF);
  for (int i = 0; i < group->count; i++) {
    detect_result_t *det_result = &(group->results[i]);
    // printf("%s @ (%d %d %d %d) %f\n",
    //         det_result->name,
    //         det_result->box.left, det_result->box.top, det_result->box.right,
    //         det_result->box.bottom, det_result->prop);
    int x1 = det_result->box.left;
    int y1 = det_result->box.top;
    int x2 = det_result->box.right;
    int y2 = det_result->box.bottom;
    SDL_Rect rect = {x1 * render_rect.w / npu_w + render_rect.x,
                     y1 * render_rect.h / npu_h + render_rect.y,
                     (x2 - x1) * render_rect.w / npu_w,
                     (y2 - y1) * render_rect.h / npu_h};
    SDL_RenderDrawRect(renderer, &rect);
    if (!det_result->name)
      continue;
    int fontw = 0, fonth = 0;
    SDL_Surface *name = sdl_font.GetFontPicture(
        (char *)det_result->name, strlen(det_result->name), 32, &fontw, &fonth);
    if (name) {
      SDL_Rect texture_dimension;
      SDL_Texture *texture = load_texture(name, renderer, &texture_dimension);
      SDL_FreeSurface(name);
      SDL_Rect dst_dimension;
      dst_dimension.x = rect.x;
      dst_dimension.y = rect.y - 18;
      dst_dimension.w = texture_dimension.w;
      dst_dimension.h = texture_dimension.h;
      SDL_RenderCopy(renderer, texture, &texture_dimension, &dst_dimension);
      SDL_DestroyTexture(texture);
    }
  }
  return true;
}

} // namespace NPU_UVC_SSD_DEMO
