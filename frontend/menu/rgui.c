/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2013 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2013 - Daniel De Matteis
 *  Copyright (C) 2012-2013 - Michael Lelli
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "rgui.h"
#include "utils/file_list.h"
#include "../../general.h"
#include "../../gfx/gfx_common.h"
#include "../../config.def.h"
#include "../../file.h"
#include "../../dynamic.h"
#include "../../compat/posix_string.h"
#include "../../gfx/shader_parse.h"

#ifdef HAVE_OPENGL
#include "../../gfx/gl_common.h"
#endif

#include "../../gfx/fonts/bitmap.h"
#include "../../screenshot.h"

#define TERM_START_X 15
#define TERM_START_Y 27
#define TERM_WIDTH (((RGUI_WIDTH - TERM_START_X - 15) / (FONT_WIDTH_STRIDE)))
#define TERM_HEIGHT (((RGUI_HEIGHT - TERM_START_Y - 15) / (FONT_HEIGHT_STRIDE)) - 1)

#ifdef GEKKO
enum
{
   GX_RESOLUTIONS_512_192 = 0,
   GX_RESOLUTIONS_598_200,
   GX_RESOLUTIONS_640_200,
   GX_RESOLUTIONS_384_224,
   GX_RESOLUTIONS_448_224,
   GX_RESOLUTIONS_480_224,
   GX_RESOLUTIONS_512_224,
   GX_RESOLUTIONS_340_232,
   GX_RESOLUTIONS_512_232,
   GX_RESOLUTIONS_512_236,
   GX_RESOLUTIONS_336_240,
   GX_RESOLUTIONS_384_240,
   GX_RESOLUTIONS_512_240,
   GX_RESOLUTIONS_576_224,
   GX_RESOLUTIONS_608_224,
   GX_RESOLUTIONS_640_224,
   GX_RESOLUTIONS_530_240,
   GX_RESOLUTIONS_640_240,
   GX_RESOLUTIONS_512_448,
   GX_RESOLUTIONS_640_448, 
   GX_RESOLUTIONS_640_480,
   GX_RESOLUTIONS_LAST,
};

unsigned rgui_gx_resolutions[GX_RESOLUTIONS_LAST][2] = {
   { 512, 192 },
   { 598, 200 },
   { 640, 200 },
   { 384, 224 },
   { 448, 224 },
   { 480, 224 },
   { 512, 224 },
   { 340, 232 },
   { 512, 232 },
   { 512, 236 },
   { 336, 240 },
   { 384, 240 },
   { 512, 240 },
   { 576, 224 },
   { 608, 224 },
   { 640, 224 },
   { 530, 240 },
   { 640, 240 },
   { 512, 448 },
   { 640, 448 },
   { 640, 480 },
};

unsigned rgui_current_gx_resolution = GX_RESOLUTIONS_640_480;
#endif

#if defined(HAVE_CG) || defined(HAVE_GLSL) || defined(HAVE_HLSL)
#define HAVE_SHADER_MANAGER
#endif

unsigned RGUI_WIDTH = 320;
unsigned RGUI_HEIGHT = 240;
uint16_t menu_framebuf[400 * 240];
rgui_handle_t *rgui;

struct rgui_handle
{
   uint16_t *frame_buf;
   size_t frame_buf_pitch;

   void *userdata;

   rgui_list_t *menu_stack;
   rgui_list_t *selection_buf;
   size_t selection_ptr;
   bool need_refresh;
   bool msg_force;

   char base_path[PATH_MAX];

   const uint8_t *font;
   bool alloc_font;

#ifdef HAVE_DYNAMIC
   char libretro_dir[PATH_MAX];
#endif
   struct retro_system_info info;

#ifdef HAVE_SHADER_MANAGER
   struct gfx_shader shader;
#endif
};

#ifdef HAVE_SHADER_MANAGER
static void shader_manager_get_str(struct gfx_shader *shader,
      char *type_str, size_t type_str_size, unsigned type);
static int shader_manager_toggle_setting(rgui_handle_t *rgui, unsigned setting, rgui_action_t action);
static void shader_manager_init(rgui_handle_t *rgui);
#endif

static const unsigned rgui_controller_lut[] = {
   RETRO_DEVICE_ID_JOYPAD_UP,
   RETRO_DEVICE_ID_JOYPAD_DOWN,
   RETRO_DEVICE_ID_JOYPAD_LEFT,
   RETRO_DEVICE_ID_JOYPAD_RIGHT,
   RETRO_DEVICE_ID_JOYPAD_A,
   RETRO_DEVICE_ID_JOYPAD_B,
   RETRO_DEVICE_ID_JOYPAD_X,
   RETRO_DEVICE_ID_JOYPAD_Y,
   RETRO_DEVICE_ID_JOYPAD_START,
   RETRO_DEVICE_ID_JOYPAD_SELECT,
   RETRO_DEVICE_ID_JOYPAD_L,
   RETRO_DEVICE_ID_JOYPAD_R,
   RETRO_DEVICE_ID_JOYPAD_L2,
   RETRO_DEVICE_ID_JOYPAD_R2,
   RETRO_DEVICE_ID_JOYPAD_L3,
   RETRO_DEVICE_ID_JOYPAD_R3,
};

static void rgui_copy_glyph(uint8_t *glyph, const uint8_t *buf)
{
   for (int y = 0; y < FONT_HEIGHT; y++)
   {
      for (int x = 0; x < FONT_WIDTH; x++)
      {
         uint32_t col =
            ((uint32_t)buf[3 * (-y * 256 + x) + 0] << 0) |
            ((uint32_t)buf[3 * (-y * 256 + x) + 1] << 8) |
            ((uint32_t)buf[3 * (-y * 256 + x) + 2] << 16);

         uint8_t rem = 1 << ((x + y * FONT_WIDTH) & 7);
         unsigned offset = (x + y * FONT_WIDTH) >> 3;

         if (col != 0xff)
            glyph[offset] |= rem;
      }
   }
}

static void init_font(rgui_handle_t *rgui, const uint8_t *font_bmp_buf)
{
   uint8_t *font = (uint8_t *) calloc(1, FONT_OFFSET(256));
   rgui->alloc_font = true;
   for (unsigned i = 0; i < 256; i++)
   {
      unsigned y = i / 16;
      unsigned x = i % 16;
      rgui_copy_glyph(&font[FONT_OFFSET(i)],
            font_bmp_buf + 54 + 3 * (256 * (255 - 16 * y) + 16 * x));
   }

   rgui->font = font;
}

static bool menu_type_is_settings(unsigned type)
{
   return type == RGUI_SETTINGS ||
      type == RGUI_SETTINGS_CORE_OPTIONS ||
      type == RGUI_SETTINGS_SHADER_MANAGER ||
      (type >= RGUI_SETTINGS_CONTROLLER_1 && type <= RGUI_SETTINGS_CONTROLLER_4);
}

static bool menu_type_is_shader_browser(unsigned type)
{
   return (type >= RGUI_SETTINGS_SHADER_0 &&
         type <= RGUI_SETTINGS_SHADER_LAST &&
         ((type - RGUI_SETTINGS_SHADER_0) % 3) == 0) ||
      type == RGUI_SETTINGS_SHADER_PRESET;
}

rgui_handle_t *rgui_init(const char *base_path,
      uint16_t *framebuf, size_t framebuf_pitch,
      const uint8_t *font_bmp_buf, const uint8_t *font_bin_buf) 
{
   rgui_handle_t *rgui = (rgui_handle_t*)calloc(1, sizeof(*rgui));

   rgui->frame_buf = framebuf;
   rgui->frame_buf_pitch = framebuf_pitch;
   strlcpy(rgui->base_path, base_path, sizeof(rgui->base_path));

   rgui->menu_stack = (rgui_list_t*)calloc(1, sizeof(rgui_list_t));
   rgui->selection_buf = (rgui_list_t*)calloc(1, sizeof(rgui_list_t));
   rgui_list_push(rgui->menu_stack, base_path, RGUI_FILE_DIRECTORY, 0);
   rgui_list_push(rgui->menu_stack, "", RGUI_SETTINGS, 0);

   if (font_bmp_buf)
      init_font(rgui, font_bmp_buf);
   else if (font_bin_buf)
      rgui->font = font_bin_buf;
   else
   {
      RARCH_ERR("no font bmp or bin, abort");
      g_extern.lifecycle_mode_state &= ~((1ULL << MODE_MENU) | (1ULL << MODE_MENU_INGAME) | (1ULL << MODE_GAME));
      g_extern.lifecycle_mode_state |= (1ULL << MODE_EXIT);
   }

#ifdef HAVE_SHADER_MANAGER
   shader_manager_init(rgui);
#endif

   return rgui;
}

void rgui_free(rgui_handle_t *rgui)
{
   rgui_list_free(rgui->menu_stack);
   rgui_list_free(rgui->selection_buf);
   if (rgui->alloc_font)
      free((uint8_t *) rgui->font);

#ifdef HAVE_DYNAMIC
   libretro_free_system_info(&rgui->info);
#endif

   free(rgui);
}

static uint16_t gray_filler(unsigned x, unsigned y)
{
   x >>= 1;
   y >>= 1;
   unsigned col = ((x + y) & 1) + 1;
#ifdef GEKKO
   return (6 << 12) | (col << 8) | (col << 4) | (col << 0);
#else
   return (col << 13) | (col << 9) | (col << 5) | (12 << 0);
#endif
}

static uint16_t green_filler(unsigned x, unsigned y)
{
   x >>= 1;
   y >>= 1;
   unsigned col = ((x + y) & 1) + 1;
#ifdef GEKKO
   return (6 << 12) | (col << 8) | (col << 5) | (col << 0);
#else
   return (col << 13) | (col << 10) | (col << 5) | (12 << 0);
#endif
}

static void fill_rect(uint16_t *buf, unsigned pitch,
      unsigned x, unsigned y,
      unsigned width, unsigned height,
      uint16_t (*col)(unsigned x, unsigned y))
{
   for (unsigned j = y; j < y + height; j++)
      for (unsigned i = x; i < x + width; i++)
         buf[j * (pitch >> 1) + i] = col(i, j);
}

static void blit_line(rgui_handle_t *rgui,
      int x, int y, const char *message, bool green)
{
   while (*message)
   {
      for (int j = 0; j < FONT_HEIGHT; j++)
      {
         for (int i = 0; i < FONT_WIDTH; i++)
         {
            uint8_t rem = 1 << ((i + j * FONT_WIDTH) & 7);
            int offset = (i + j * FONT_WIDTH) >> 3;
            bool col = (rgui->font[FONT_OFFSET((unsigned char)*message) + offset] & rem);

            if (col)
            {
               rgui->frame_buf[(y + j) * (rgui->frame_buf_pitch >> 1) + (x + i)] = green ?
#ifdef GEKKO
               (3 << 0) | (10 << 4) | (3 << 8) | (7 << 12) : 0x7FFF;
#else
               (15 << 0) | (7 << 4) | (15 << 8) | (7 << 12) : 0xFFFF;
#endif
            }
         }
      }

      x += FONT_WIDTH_STRIDE;
      message++;
   }
}

static void render_background(rgui_handle_t *rgui)
{
   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         0, 0, RGUI_WIDTH, RGUI_HEIGHT, gray_filler);

   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         5, 5, RGUI_WIDTH - 10, 5, green_filler);

   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         5, RGUI_HEIGHT - 10, RGUI_WIDTH - 10, 5, green_filler);

   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         5, 5, 5, RGUI_HEIGHT - 10, green_filler);

   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         RGUI_WIDTH - 10, 5, 5, RGUI_HEIGHT - 10, green_filler);
}

static void render_messagebox(rgui_handle_t *rgui, const char *message)
{
   if (!message || !*message)
      return;

   char *msg = strdup(message);
   if (strlen(msg) > TERM_WIDTH)
   {
      msg[TERM_WIDTH - 2] = '.';
      msg[TERM_WIDTH - 1] = '.';
      msg[TERM_WIDTH - 0] = '.';
      msg[TERM_WIDTH + 1] = '\0';
   }

   unsigned width = strlen(msg) * FONT_WIDTH_STRIDE - 1 + 6 + 10;
   unsigned height = FONT_HEIGHT + 6 + 10;
   int x = (RGUI_WIDTH - width) / 2;
   int y = (RGUI_HEIGHT - height) / 2;
   
   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         x + 5, y + 5, width - 10, height - 10, gray_filler);

   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         x, y, width - 5, 5, green_filler);

   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         x + width - 5, y, 5, height - 5, green_filler);

   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         x + 5, y + height - 5, width - 5, 5, green_filler);

   fill_rect(rgui->frame_buf, rgui->frame_buf_pitch,
         x, y + 5, 5, height - 5, green_filler);

   blit_line(rgui, x + 8, y + 8, msg, false);
   free(msg);
}

static void render_text(rgui_handle_t *rgui)
{
   if (rgui->need_refresh && 
         (g_extern.lifecycle_mode_state & (1ULL << MODE_MENU))
         && !rgui->msg_force)
      return;

   size_t begin = rgui->selection_ptr >= TERM_HEIGHT / 2 ?
      rgui->selection_ptr - TERM_HEIGHT / 2 : 0;
   size_t end = rgui->selection_ptr + TERM_HEIGHT <= rgui->selection_buf->size ?
      rgui->selection_ptr + TERM_HEIGHT : rgui->selection_buf->size;

   if (end - begin > TERM_HEIGHT)
      end = begin + TERM_HEIGHT;

   render_background(rgui);

   char title[256];
   const char *dir = NULL;
   unsigned menu_type = 0;
   rgui_list_get_last(rgui->menu_stack, &dir, &menu_type);

   if (menu_type == RGUI_SETTINGS_CORE)
      strlcpy(title, "CORE SELECTION", sizeof(title));
   else if (menu_type == RGUI_SETTINGS_SHADER_MANAGER)
      strlcpy(title, "SHADER MANAGER", sizeof(title));
   else if (menu_type == RGUI_SETTINGS_CORE_OPTIONS)
      strlcpy(title, "CORE OPTIONS", sizeof(title));
   else if (menu_type_is_shader_browser(menu_type))
      snprintf(title, sizeof(title), "SHADER %s", dir);
   else if ((menu_type >= RGUI_SETTINGS_CONTROLLER_1 && menu_type <= RGUI_SETTINGS_CONTROLLER_4) ||
         (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT || menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT_2) ||
         menu_type == RGUI_SETTINGS)
      snprintf(title, sizeof(title), "SETTINGS %s", dir);
   else
      snprintf(title, sizeof(title), "FILE BROWSER %s", dir);

   // Ensure that directory doesn't overflow terminal.
   size_t title_len = strlen(title);
   if (title_len > TERM_WIDTH - 3)
   {
      size_t start = title_len - (TERM_WIDTH - 7);
      memmove(title + 4, title + start, title_len - start + 1);
      memcpy(title, "... ", 4);
   }

   blit_line(rgui, TERM_START_X + 15, 15, title, true);

   blit_line(rgui, TERM_START_X + 15, (TERM_HEIGHT * FONT_HEIGHT_STRIDE) + TERM_START_Y + 2, g_extern.title_buf, true);
#ifndef __BLACKBERRY_QNX__
   blit_line(rgui, TERM_HEIGHT - 80, (TERM_HEIGHT * FONT_HEIGHT_STRIDE) + TERM_START_Y + 2, PACKAGE_VERSION, true);
#endif

   unsigned x = TERM_START_X;
   unsigned y = TERM_START_Y;

   for (size_t i = begin; i < end; i++, y += FONT_HEIGHT_STRIDE)
   {
      const char *path = 0;
      unsigned type = 0;
      rgui_list_get_at_offset(rgui->selection_buf, i, &path, &type);
      char message[256];
      char type_str[256];
      int w = (menu_type >= RGUI_SETTINGS_CONTROLLER_1 && menu_type <= RGUI_SETTINGS_CONTROLLER_4) ? 26 : 19;
      unsigned port = menu_type - RGUI_SETTINGS_CONTROLLER_1;
      
#ifdef HAVE_SHADER_MANAGER
      if (type >= RGUI_SETTINGS_SHADER_FILTER &&
            type <= RGUI_SETTINGS_SHADER_LAST)
      {
         // HACK. Work around that we're using the menu_type as dir type to propagate state correctly.
         if (menu_type_is_shader_browser(menu_type) && menu_type_is_shader_browser(type))
         {
            type = RGUI_FILE_DIRECTORY;
            strlcpy(type_str, "(DIR)", sizeof(type_str));
            w = 5;
         }
         else if (type == RGUI_SETTINGS_SHADER_FILTER)
            snprintf(type_str, sizeof(type_str), "%s",
                  g_settings.video.smooth ? "Linear" : "Nearest");
         else if (type == RGUI_SETTINGS_SHADER_PRESET)
            strlcpy(type_str, "...", sizeof(type_str));
         else
            shader_manager_get_str(&rgui->shader, type_str, sizeof(type_str), type);
      }
      else
#endif
      if (type >= RGUI_SETTINGS_CORE_OPTION_START)
         strlcpy(type_str, core_option_get_val(g_extern.system.core_options, type - RGUI_SETTINGS_CORE_OPTION_START), sizeof(type_str));
      else
      {
         switch (type)
         {
            case RGUI_FILE_PLAIN:
               strlcpy(type_str, "(FILE)", sizeof(type_str));
               w = 6;
               break;
            case RGUI_FILE_DIRECTORY:
               strlcpy(type_str, "(DIR)", sizeof(type_str));
               w = 5;
               break;
            case RGUI_FILE_DEVICE:
               strlcpy(type_str, "(DEV)", sizeof(type_str));
               w = 5;
               break;
            case RGUI_SETTINGS_REWIND_ENABLE:
               if (g_settings.rewind_enable)
                  strlcpy(type_str, "ON", sizeof(type_str));
               else
                  strlcpy(type_str, "OFF", sizeof(type_str));
               break;
            case RGUI_SETTINGS_REWIND_GRANULARITY:
               snprintf(type_str, sizeof(type_str), "%d", g_settings.rewind_granularity);
               break;
            case RGUI_SETTINGS_SAVESTATE_SAVE:
            case RGUI_SETTINGS_SAVESTATE_LOAD:
               snprintf(type_str, sizeof(type_str), "%d", g_extern.state_slot);
               break;
            case RGUI_SETTINGS_VIDEO_FILTER:
               if (g_settings.video.smooth)
                  strlcpy(type_str, "Bilinear filtering", sizeof(type_str));
               else
                  strlcpy(type_str, "Point filtering", sizeof(type_str));
               break;
            case RGUI_SETTINGS_VIDEO_SOFT_FILTER:
               snprintf(type_str, sizeof(type_str), (g_extern.lifecycle_mode_state & (1ULL << MODE_VIDEO_SOFT_FILTER_ENABLE)) ? "ON" : "OFF");
               break;
   #ifdef GEKKO
            case RGUI_SETTINGS_VIDEO_RESOLUTION:
               strlcpy(type_str, gx_get_video_mode(), sizeof(type_str));
               break;
   #endif
            case RGUI_SETTINGS_VIDEO_GAMMA:
               snprintf(type_str, sizeof(type_str), "%d", g_extern.console.screen.gamma_correction);
               break;
            case RGUI_SETTINGS_VIDEO_ASPECT_RATIO:
               strlcpy(type_str, aspectratio_lut[g_settings.video.aspect_ratio_idx].name, sizeof(type_str));
               break;
            case RGUI_SETTINGS_VIDEO_ROTATION:
               snprintf(type_str, sizeof(type_str), "Rotation: %s",
                     rotation_lut[g_extern.console.screen.orientation]);
               break;
            case RGUI_SETTINGS_AUDIO_MUTE:
               if (g_extern.audio_data.mute)
                  strlcpy(type_str, "ON", sizeof(type_str));
               else
                  strlcpy(type_str, "OFF", sizeof(type_str));
               break;
            case RGUI_SETTINGS_AUDIO_CONTROL_RATE:
               snprintf(type_str, sizeof(type_str), "%.3f", g_settings.audio.rate_control_delta);
               break;
            case RGUI_SETTINGS_SRAM_DIR:
               snprintf(type_str, sizeof(type_str), (g_extern.lifecycle_mode_state & (1ULL << MODE_LOAD_GAME_SRAM_DIR_ENABLE)) ? "ON" : "OFF");
               break;
            case RGUI_SETTINGS_STATE_DIR:
               snprintf(type_str, sizeof(type_str), (g_extern.lifecycle_mode_state & (1ULL << MODE_LOAD_GAME_STATE_DIR_ENABLE)) ? "ON" : "OFF");
               break;
            case RGUI_SETTINGS_DEBUG_TEXT:
               snprintf(type_str, sizeof(type_str), (g_extern.lifecycle_mode_state & (1ULL << MODE_FPS_DRAW)) ? "ON" : "OFF");
               break;
            case RGUI_SETTINGS_OPEN_FILEBROWSER:
            case RGUI_SETTINGS_CORE_OPTIONS:
            case RGUI_SETTINGS_SHADER_MANAGER:
            case RGUI_SETTINGS_SHADER_PRESET:
            case RGUI_SETTINGS_CUSTOM_VIEWPORT:
            case RGUI_SETTINGS_CORE:
            case RGUI_SETTINGS_CONTROLLER_1:
            case RGUI_SETTINGS_CONTROLLER_2:
            case RGUI_SETTINGS_CONTROLLER_3:
            case RGUI_SETTINGS_CONTROLLER_4:
               strlcpy(type_str, "...", sizeof(type_str));
               break;
            case RGUI_SETTINGS_BIND_DEVICE:
               strlcpy(type_str, g_settings.input.device_names[port], sizeof(type_str));
               break;
            case RGUI_SETTINGS_BIND_DPAD_EMULATION:
               switch(g_settings.input.dpad_emulation[port])
               {
                  case ANALOG_DPAD_NONE:
                     strlcpy(type_str, "None", sizeof(type_str));
                     break;
                  case ANALOG_DPAD_LSTICK:
                     strlcpy(type_str, "Left Stick", sizeof(type_str));
                     break;
                  case ANALOG_DPAD_RSTICK:
                     strlcpy(type_str, "Right Stick", sizeof(type_str));
                     break;
               }
               break;
            case RGUI_SETTINGS_BIND_UP:
            case RGUI_SETTINGS_BIND_DOWN:
            case RGUI_SETTINGS_BIND_LEFT:
            case RGUI_SETTINGS_BIND_RIGHT:
            case RGUI_SETTINGS_BIND_A:
            case RGUI_SETTINGS_BIND_B:
            case RGUI_SETTINGS_BIND_X:
            case RGUI_SETTINGS_BIND_Y:
            case RGUI_SETTINGS_BIND_START:
            case RGUI_SETTINGS_BIND_SELECT:
            case RGUI_SETTINGS_BIND_L:
            case RGUI_SETTINGS_BIND_R:
            case RGUI_SETTINGS_BIND_L2:
            case RGUI_SETTINGS_BIND_R2:
            case RGUI_SETTINGS_BIND_L3:
            case RGUI_SETTINGS_BIND_R3:
               {
                  unsigned id = rgui_controller_lut[type - RGUI_SETTINGS_BIND_UP];
                  struct platform_bind key_label;
                  strlcpy(key_label.desc, "Unknown", sizeof(key_label.desc));
                  key_label.joykey = g_settings.input.binds[port][id].joykey;

                  if (driver.input->set_keybinds)
                     driver.input->set_keybinds(&key_label, 0, 0, 0, (1ULL << KEYBINDS_ACTION_GET_BIND_LABEL));

                  strlcpy(type_str, key_label.desc, sizeof(type_str));
               }
               break;
            default:
               type_str[0] = 0;
               w = 0;
               break;
         }
      }

      const char *entry_title;
      char tmp[256];
      size_t path_len = strlen(path);
      // trim long filenames
      if ((type == RGUI_FILE_PLAIN || type == RGUI_FILE_DIRECTORY) && path_len > TERM_WIDTH - (w + 1 + 2))
      {
         snprintf(tmp, sizeof(tmp), "%.*s...%s", TERM_WIDTH - (w + 1 + 2) - 8, path, &path[path_len - 5]);
         entry_title = tmp;
      }
      else
         entry_title = path;

      snprintf(message, sizeof(message), "%c %-*.*s %-*s\n",
            i == rgui->selection_ptr ? '>' : ' ',
            TERM_WIDTH - (w + 1 + 2), TERM_WIDTH - (w + 1 + 2),
            entry_title,
            w,
            type_str);

      blit_line(rgui, x, y, message, i == rgui->selection_ptr);
   }

#ifdef GEKKO
   const char *message_queue;

   if (rgui->msg_force)
   {
      message_queue = msg_queue_pull(g_extern.msg_queue);
      rgui->msg_force = false;
   }
   else
      message_queue = driver.current_msg;

   render_messagebox(rgui, message_queue);
#endif
}

#ifdef GEKKO
#define MAX_GAMMA_SETTING 2
#else
#define MAX_GAMMA_SETTING 1
#endif

static int rgui_core_setting_toggle(unsigned setting, rgui_action_t action)
{
   unsigned index = setting - RGUI_SETTINGS_CORE_OPTION_START;
   switch (action)
   {
      case RGUI_ACTION_LEFT:
         core_option_prev(g_extern.system.core_options, index);
         break;

      case RGUI_ACTION_RIGHT:
      case RGUI_ACTION_OK:
         core_option_next(g_extern.system.core_options, index);
         break;

      case RGUI_ACTION_START:
         core_option_set_default(g_extern.system.core_options, index);
         break;

      default:
         break;
   }

   return 0;
}

static int rgui_settings_toggle_setting(rgui_handle_t *rgui, unsigned setting, rgui_action_t action, unsigned menu_type)
{
   unsigned port = menu_type - RGUI_SETTINGS_CONTROLLER_1;

   (void)rgui;

#ifdef HAVE_SHADER_MANAGER
   if (setting >= RGUI_SETTINGS_SHADER_FILTER && setting <= RGUI_SETTINGS_SHADER_LAST)
      return shader_manager_toggle_setting(rgui, setting, action);
#endif
   if (setting >= RGUI_SETTINGS_CORE_OPTION_START)
      return rgui_core_setting_toggle(setting, action);

   switch (setting)
   {
      case RGUI_SETTINGS_REWIND_ENABLE:
         if (action == RGUI_ACTION_OK ||
               action == RGUI_ACTION_LEFT ||
               action == RGUI_ACTION_RIGHT)
         {
            settings_set(1ULL << S_REWIND);
            if (g_settings.rewind_enable)
               rarch_init_rewind();
            else
               rarch_deinit_rewind();
         }
         else if (action == RGUI_ACTION_START)
         {
            g_settings.rewind_enable = false;
            rarch_deinit_rewind();
         }
         break;
      case RGUI_SETTINGS_REWIND_GRANULARITY:
         if (action == RGUI_ACTION_OK || action == RGUI_ACTION_RIGHT)
            g_settings.rewind_granularity++;
         else if (action == RGUI_ACTION_LEFT)
         {
            if (g_settings.rewind_granularity > 1)
               g_settings.rewind_granularity--;
         }
         else if (action == RGUI_ACTION_START)
            g_settings.rewind_granularity = 1;
         break;
      case RGUI_SETTINGS_SAVESTATE_SAVE:
      case RGUI_SETTINGS_SAVESTATE_LOAD:
         if (action == RGUI_ACTION_OK)
         {
            if (setting == RGUI_SETTINGS_SAVESTATE_SAVE)
               rarch_save_state();
            else
               rarch_load_state();
            g_extern.lifecycle_mode_state |= (1ULL << MODE_GAME);
            return -1;
         }
         else if (action == RGUI_ACTION_START)
            settings_set(1ULL << S_DEF_SAVE_STATE);
         else if (action == RGUI_ACTION_LEFT)
            settings_set(1ULL << S_SAVESTATE_DECREMENT);
         else if (action == RGUI_ACTION_RIGHT)
            settings_set(1ULL << S_SAVESTATE_INCREMENT);
         break;
#ifdef HAVE_SCREENSHOTS
      case RGUI_SETTINGS_SCREENSHOT:
         if (action == RGUI_ACTION_OK)
         {
            const void *data = g_extern.frame_cache.data;
            unsigned width   = g_extern.frame_cache.width;
            unsigned height  = g_extern.frame_cache.height;
            int pitch        = g_extern.frame_cache.pitch;

#ifdef RARCH_CONSOLE
            const char *screenshot_dir = default_paths.port_dir;
#else
            const char *screenshot_dir = g_settings.screenshot_directory;
#endif

            // Negative pitch is needed as screenshot takes bottom-up,
            // but we use top-down.
            bool r = screenshot_dump(screenshot_dir,
                  (const uint8_t*)data + (height - 1) * pitch, 
                  width, height, -pitch, false);

            msg_queue_push(g_extern.msg_queue,
                  r ? "Screenshot saved" : "Screenshot failed to save", 1, 90);
         }
         break;
#endif
      case RGUI_SETTINGS_RESTART_GAME:
         if (action == RGUI_ACTION_OK)
         {
            rarch_game_reset();
            g_extern.lifecycle_mode_state |= (1ULL << MODE_GAME);
            return -1;
         }
         break;
      case RGUI_SETTINGS_VIDEO_FILTER:
         if (action == RGUI_ACTION_START)
            settings_set(1ULL << S_DEF_HW_TEXTURE_FILTER);
         else
            settings_set(1ULL << S_HW_TEXTURE_FILTER);

         if (driver.video_poke->set_filtering)
            driver.video_poke->set_filtering(driver.video_data, 1, g_settings.video.smooth);
         break;
#ifdef HW_RVL
      case RGUI_SETTINGS_VIDEO_SOFT_FILTER:
         {
            if (g_extern.lifecycle_mode_state & (1ULL << MODE_VIDEO_SOFT_FILTER_ENABLE))
               g_extern.lifecycle_mode_state &= ~(1ULL << MODE_VIDEO_SOFT_FILTER_ENABLE);
            else
               g_extern.lifecycle_mode_state |= (1ULL << MODE_VIDEO_SOFT_FILTER_ENABLE);

            if (driver.video_poke->apply_state_changes)
               driver.video_poke->apply_state_changes(driver.video_data);
         }
         break;
#endif
#ifdef GEKKO
      case RGUI_SETTINGS_VIDEO_RESOLUTION:
         if (action == RGUI_ACTION_LEFT)
         {
            if(rgui_current_gx_resolution > 0)
            {
               rgui_current_gx_resolution--;
               gx_set_video_mode(rgui_gx_resolutions[rgui_current_gx_resolution][0], rgui_gx_resolutions[rgui_current_gx_resolution][1]);
            }
         }
         else if (action == RGUI_ACTION_RIGHT)
         {
            if(rgui_current_gx_resolution < GX_RESOLUTIONS_LAST - 1)
            {
#ifdef HW_RVL
               if ((rgui_current_gx_resolution + 1) > GX_RESOLUTIONS_640_480)
                  if (CONF_GetVideo() != CONF_VIDEO_PAL)
                     return 0;
#endif

               rgui_current_gx_resolution++;
               gx_set_video_mode(rgui_gx_resolutions[rgui_current_gx_resolution][0], rgui_gx_resolutions[rgui_current_gx_resolution][1]);
            }
         }
         break;
#endif
      case RGUI_SETTINGS_VIDEO_GAMMA:
         if (action == RGUI_ACTION_START)
         {
            g_extern.console.screen.gamma_correction = 0;
            if (driver.video_poke->apply_state_changes)
               driver.video_poke->apply_state_changes(driver.video_data);
         }
         else if (action == RGUI_ACTION_LEFT)
         {
            if(g_extern.console.screen.gamma_correction > 0)
            {
               g_extern.console.screen.gamma_correction--;
               if (driver.video_poke->apply_state_changes)
                  driver.video_poke->apply_state_changes(driver.video_data);
            }
         }
         else if (action == RGUI_ACTION_RIGHT)
         {
            if(g_extern.console.screen.gamma_correction < MAX_GAMMA_SETTING)
            {
               g_extern.console.screen.gamma_correction++;
               if (driver.video_poke->apply_state_changes)
                  driver.video_poke->apply_state_changes(driver.video_data);
            }
         }
         break;
      case RGUI_SETTINGS_VIDEO_ASPECT_RATIO:
         if (action == RGUI_ACTION_START)
            settings_set(1ULL << S_DEF_ASPECT_RATIO);
         else if (action == RGUI_ACTION_LEFT)
            settings_set(1ULL << S_ASPECT_RATIO_DECREMENT);
         else if (action == RGUI_ACTION_RIGHT)
            settings_set(1ULL << S_ASPECT_RATIO_INCREMENT);

         if (driver.video_poke->set_aspect_ratio)
            driver.video_poke->set_aspect_ratio(driver.video_data, g_settings.video.aspect_ratio_idx);
         break;
      case RGUI_SETTINGS_VIDEO_ROTATION:
         if (action == RGUI_ACTION_START)
         {
            settings_set(1ULL << S_DEF_AUDIO_CONTROL_RATE);
            video_set_rotation_func(g_extern.console.screen.orientation);
         }
         else if (action == RGUI_ACTION_LEFT)
         {
            settings_set(1ULL << S_ROTATION_DECREMENT);
            video_set_rotation_func(g_extern.console.screen.orientation);
         }
         else if (action == RGUI_ACTION_RIGHT)
         {
            settings_set(1ULL << S_ROTATION_INCREMENT);
            video_set_rotation_func(g_extern.console.screen.orientation);
         }
         break;
      case RGUI_SETTINGS_AUDIO_MUTE:
         if (action == RGUI_ACTION_START)
            settings_set(1ULL << S_DEF_AUDIO_MUTE);
         else
            settings_set(1ULL << S_AUDIO_MUTE);
         break;
      case RGUI_SETTINGS_AUDIO_CONTROL_RATE:
         if (action == RGUI_ACTION_START)
            settings_set(1ULL << S_DEF_AUDIO_CONTROL_RATE);
         else if (action == RGUI_ACTION_LEFT)
            settings_set(1ULL << S_AUDIO_CONTROL_RATE_DECREMENT);
         else if (action == RGUI_ACTION_RIGHT)
            settings_set(1ULL << S_AUDIO_CONTROL_RATE_INCREMENT);
         break;
      case RGUI_SETTINGS_SRAM_DIR:
         if (action == RGUI_ACTION_START || action == RGUI_ACTION_LEFT)
            g_extern.lifecycle_mode_state &= ~(1ULL << MODE_LOAD_GAME_SRAM_DIR_ENABLE);
         else if (action == RGUI_ACTION_RIGHT)
            g_extern.lifecycle_mode_state |= (1ULL << MODE_LOAD_GAME_SRAM_DIR_ENABLE);
         break;
      case RGUI_SETTINGS_STATE_DIR:
         if (action == RGUI_ACTION_START || action == RGUI_ACTION_LEFT)
            g_extern.lifecycle_mode_state &= ~(1ULL << MODE_LOAD_GAME_STATE_DIR_ENABLE);
         else if (action == RGUI_ACTION_RIGHT)
            g_extern.lifecycle_mode_state |= (1ULL << MODE_LOAD_GAME_STATE_DIR_ENABLE);
         break;
      case RGUI_SETTINGS_DEBUG_TEXT:
         if (action == RGUI_ACTION_START || action == RGUI_ACTION_LEFT)
            g_extern.lifecycle_mode_state &= ~(1ULL << MODE_FPS_DRAW);
         else if (action == RGUI_ACTION_RIGHT)
            g_extern.lifecycle_mode_state |= (1ULL << MODE_FPS_DRAW);
         break;
      case RGUI_SETTINGS_RESTART_EMULATOR:
         if (action == RGUI_ACTION_OK)
         {
#ifdef GEKKO
            fill_pathname_join(g_extern.fullpath, default_paths.core_dir, SALAMANDER_FILE,
                  sizeof(g_extern.fullpath));
#endif
            g_extern.lifecycle_mode_state &= ~(1ULL << MODE_GAME);
            g_extern.lifecycle_mode_state |= (1ULL << MODE_EXIT);
            g_extern.lifecycle_mode_state |= (1ULL << MODE_EXITSPAWN);
            return -1;
         }
         break;
      case RGUI_SETTINGS_RESUME_GAME:
         if (action == RGUI_ACTION_OK && (g_extern.main_is_init))
         {
            g_extern.lifecycle_mode_state |= (1ULL << MODE_GAME);
            return -1;
         }
         break;
      case RGUI_SETTINGS_QUIT_RARCH:
         if (action == RGUI_ACTION_OK)
         {
            g_extern.lifecycle_mode_state &= ~(1ULL << MODE_GAME);
            g_extern.lifecycle_mode_state |= (1ULL << MODE_EXIT);
            return -1;
         }
         break;
      // controllers
      case RGUI_SETTINGS_BIND_DEVICE:
         g_settings.input.device[port] += DEVICE_LAST;
         if (action == RGUI_ACTION_START)
            g_settings.input.device[port] = 0;
         else if (action == RGUI_ACTION_LEFT)
            g_settings.input.device[port]--;
         else if (action == RGUI_ACTION_RIGHT)
            g_settings.input.device[port]++;

         // DEVICE_LAST can be 0, avoid modulo.
         if (g_settings.input.device[port] >= DEVICE_LAST)
            g_settings.input.device[port] -= DEVICE_LAST;

         if (driver.input->set_keybinds)
         {
            unsigned keybind_action = (1ULL << KEYBINDS_ACTION_SET_DEFAULT_BINDS);

            switch (g_settings.input.dpad_emulation[port])
            {
               case ANALOG_DPAD_LSTICK:
                  keybind_action |= (1ULL << KEYBINDS_ACTION_SET_ANALOG_DPAD_LSTICK);
                  break;
               case ANALOG_DPAD_RSTICK:
                  keybind_action |= (1ULL << KEYBINDS_ACTION_SET_ANALOG_DPAD_RSTICK);
                  break;
               case ANALOG_DPAD_NONE:
                  keybind_action |= (1ULL << KEYBINDS_ACTION_SET_ANALOG_DPAD_NONE);
                  break;
               default:
                  break;
            }

            driver.input->set_keybinds(driver.input_data, g_settings.input.device[port], port, 0,
                  keybind_action);
         }
         break;
      case RGUI_SETTINGS_BIND_DPAD_EMULATION:
         g_settings.input.dpad_emulation[port] += ANALOG_DPAD_LAST;
         if (action == RGUI_ACTION_START)
            g_settings.input.dpad_emulation[port] = ANALOG_DPAD_LSTICK;
         else if (action == RGUI_ACTION_LEFT)
            g_settings.input.dpad_emulation[port]--;
         else if (action == RGUI_ACTION_RIGHT)
            g_settings.input.dpad_emulation[port]++;
         g_settings.input.dpad_emulation[port] %= ANALOG_DPAD_LAST;

         if (driver.input->set_keybinds)
         {
            unsigned keybind_action = 0;

            switch (g_settings.input.dpad_emulation[port])
            {
               case ANALOG_DPAD_LSTICK:
                  keybind_action = (1ULL << KEYBINDS_ACTION_SET_ANALOG_DPAD_LSTICK);
                  break;
               case ANALOG_DPAD_RSTICK:
                  keybind_action = (1ULL << KEYBINDS_ACTION_SET_ANALOG_DPAD_RSTICK);
                  break;
               case ANALOG_DPAD_NONE:
                  keybind_action = (1ULL << KEYBINDS_ACTION_SET_ANALOG_DPAD_NONE);
                  break;
               default:
                  break;
            }

            if (keybind_action)
               driver.input->set_keybinds(driver.input_data, g_settings.input.device[port], port, 0,
                     keybind_action);
         }
         break;
      case RGUI_SETTINGS_BIND_UP:
      case RGUI_SETTINGS_BIND_DOWN:
      case RGUI_SETTINGS_BIND_LEFT:
      case RGUI_SETTINGS_BIND_RIGHT:
      case RGUI_SETTINGS_BIND_A:
      case RGUI_SETTINGS_BIND_B:
      case RGUI_SETTINGS_BIND_X:
      case RGUI_SETTINGS_BIND_Y:
      case RGUI_SETTINGS_BIND_START:
      case RGUI_SETTINGS_BIND_SELECT:
      case RGUI_SETTINGS_BIND_L:
      case RGUI_SETTINGS_BIND_R:
      case RGUI_SETTINGS_BIND_L2:
      case RGUI_SETTINGS_BIND_R2:
      case RGUI_SETTINGS_BIND_L3:
      case RGUI_SETTINGS_BIND_R3:
         if (driver.input->set_keybinds)
         {
            unsigned keybind_action = KEYBINDS_ACTION_NONE;

            if (action == RGUI_ACTION_START)
               keybind_action = (1ULL << KEYBINDS_ACTION_SET_DEFAULT_BIND);
            else if (action == RGUI_ACTION_LEFT)
               keybind_action = (1ULL << KEYBINDS_ACTION_DECREMENT_BIND);
            else if (action == RGUI_ACTION_RIGHT)
               keybind_action = (1ULL << KEYBINDS_ACTION_INCREMENT_BIND);

            if (keybind_action != KEYBINDS_ACTION_NONE)
               driver.input->set_keybinds(driver.input_data, g_settings.input.device[setting - RGUI_SETTINGS_BIND_UP], port,
                     rgui_controller_lut[setting - RGUI_SETTINGS_BIND_UP], keybind_action); 
         }
      default:
         break;
   }

   return 0;
}

static void rgui_settings_populate_entries(rgui_handle_t *rgui)
{
   rgui_list_clear(rgui->selection_buf);

#if defined(HAVE_DYNAMIC) || defined(HAVE_LIBRETRO_MANAGEMENT)
   rgui_list_push(rgui->selection_buf, "Core", RGUI_SETTINGS_CORE, 0);
#endif
   rgui_list_push(rgui->selection_buf, "Core Options", RGUI_SETTINGS_CORE_OPTIONS, 0);
#ifdef HAVE_SHADER_MANAGER
   rgui_list_push(rgui->selection_buf, "Shader Manager", RGUI_SETTINGS_SHADER_MANAGER, 0);
#endif
   rgui_list_push(rgui->selection_buf, "Rewind", RGUI_SETTINGS_REWIND_ENABLE, 0);
   rgui_list_push(rgui->selection_buf, "Rewind granularity", RGUI_SETTINGS_REWIND_GRANULARITY, 0);
   if (g_extern.main_is_init)
   {
      rgui_list_push(rgui->selection_buf, "Save State", RGUI_SETTINGS_SAVESTATE_SAVE, 0);
      rgui_list_push(rgui->selection_buf, "Load State", RGUI_SETTINGS_SAVESTATE_LOAD, 0);
#ifdef HAVE_SCREENSHOTS
      rgui_list_push(rgui->selection_buf, "Take Screenshot", RGUI_SETTINGS_SCREENSHOT, 0);
#endif
      rgui_list_push(rgui->selection_buf, "Resume Game", RGUI_SETTINGS_RESUME_GAME, 0);
      rgui_list_push(rgui->selection_buf, "Change Game", RGUI_SETTINGS_OPEN_FILEBROWSER, 0);
      rgui_list_push(rgui->selection_buf, "Restart Game", RGUI_SETTINGS_RESTART_GAME, 0);
   }
#ifdef HW_RVL
   rgui_list_push(rgui->selection_buf, "VI Trap filtering", RGUI_SETTINGS_VIDEO_SOFT_FILTER, 0);
#endif
#ifdef GEKKO
   rgui_list_push(rgui->selection_buf, "Hardware filtering", RGUI_SETTINGS_VIDEO_FILTER, 0);
   rgui_list_push(rgui->selection_buf, "Screen Resolution", RGUI_SETTINGS_VIDEO_RESOLUTION, 0);
   rgui_list_push(rgui->selection_buf, "Gamma", RGUI_SETTINGS_VIDEO_GAMMA, 0);
#endif
   rgui_list_push(rgui->selection_buf, "Aspect Ratio", RGUI_SETTINGS_VIDEO_ASPECT_RATIO, 0);
   rgui_list_push(rgui->selection_buf, "Custom Ratio", RGUI_SETTINGS_CUSTOM_VIEWPORT, 0);
   rgui_list_push(rgui->selection_buf, "Rotation", RGUI_SETTINGS_VIDEO_ROTATION, 0);
   rgui_list_push(rgui->selection_buf, "Mute Audio", RGUI_SETTINGS_AUDIO_MUTE, 0);
   rgui_list_push(rgui->selection_buf, "Audio Control Rate", RGUI_SETTINGS_AUDIO_CONTROL_RATE, 0);
#ifdef GEKKO
   rgui_list_push(rgui->selection_buf, "SRAM Saves in \"sram\" Dir", RGUI_SETTINGS_SRAM_DIR, 0);
   rgui_list_push(rgui->selection_buf, "State Saves in \"state\" Dir", RGUI_SETTINGS_STATE_DIR, 0);
#endif
   rgui_list_push(rgui->selection_buf, "Controller #1 Config", RGUI_SETTINGS_CONTROLLER_1, 0);
   rgui_list_push(rgui->selection_buf, "Controller #2 Config", RGUI_SETTINGS_CONTROLLER_2, 0);
   rgui_list_push(rgui->selection_buf, "Controller #3 Config", RGUI_SETTINGS_CONTROLLER_3, 0);
   rgui_list_push(rgui->selection_buf, "Controller #4 Config", RGUI_SETTINGS_CONTROLLER_4, 0);
   rgui_list_push(rgui->selection_buf, "Debug Text", RGUI_SETTINGS_DEBUG_TEXT, 0);
#ifndef HAVE_DYNAMIC
   rgui_list_push(rgui->selection_buf, "Restart RetroArch", RGUI_SETTINGS_RESTART_EMULATOR, 0);
#endif
   rgui_list_push(rgui->selection_buf, "Quit RetroArch", RGUI_SETTINGS_QUIT_RARCH, 0);
}

static void rgui_settings_core_options_populate_entries(rgui_handle_t *rgui)
{
   rgui_list_clear(rgui->selection_buf);

   if (g_extern.system.core_options)
   {
      size_t opts = core_option_size(g_extern.system.core_options);
      for (size_t i = 0; i < opts; i++)
         rgui_list_push(rgui->selection_buf,
               core_option_get_desc(g_extern.system.core_options, i), RGUI_SETTINGS_CORE_OPTION_START + i, 0);
   }
   else
      rgui_list_push(rgui->selection_buf, "No options available.", RGUI_SETTINGS_CORE_OPTION_NONE, 0);
}

#ifdef HAVE_SHADER_MANAGER
static void rgui_settings_shader_manager_populate_entries(rgui_handle_t *rgui)
{
   rgui_list_clear(rgui->selection_buf);
   rgui_list_push(rgui->selection_buf, "Apply changes",
         RGUI_SETTINGS_SHADER_APPLY, 0);
   rgui_list_push(rgui->selection_buf, "Default filter",
         RGUI_SETTINGS_SHADER_FILTER, 0);
   rgui_list_push(rgui->selection_buf, "Load shader preset",
         RGUI_SETTINGS_SHADER_PRESET, 0);
   rgui_list_push(rgui->selection_buf, "Shader passes",
         RGUI_SETTINGS_SHADER_PASSES, 0);

   for (unsigned i = 0; i < rgui->shader.passes; i++)
   {
      char buf[64];

      snprintf(buf, sizeof(buf), "Shader #%u", i);
      rgui_list_push(rgui->selection_buf, buf,
            RGUI_SETTINGS_SHADER_0 + 3 * i, 0);

      snprintf(buf, sizeof(buf), "Shader #%u filter", i);
      rgui_list_push(rgui->selection_buf, buf,
            RGUI_SETTINGS_SHADER_0_FILTER + 3 * i, 0);

      snprintf(buf, sizeof(buf), "Shader #%u scale", i);
      rgui_list_push(rgui->selection_buf, buf,
            RGUI_SETTINGS_SHADER_0_SCALE + 3 * i, 0);
   }
}

static enum rarch_shader_type shader_manager_get_type(const struct gfx_shader *shader)
{
   // All shader types must be the same, or we cannot use it.
   enum rarch_shader_type type = RARCH_SHADER_NONE;

   for (unsigned i = 0; i < shader->passes; i++)
   {
      enum rarch_shader_type pass_type = gfx_shader_parse_type(shader->pass[i].source.cg,
            RARCH_SHADER_NONE);

      switch (pass_type)
      {
         case RARCH_SHADER_CG:
         case RARCH_SHADER_GLSL:
            if (type == RARCH_SHADER_NONE)
               type = pass_type;
            else if (type != pass_type)
               return RARCH_SHADER_NONE;
            break;

         default:
            return RARCH_SHADER_NONE;
      }
   }

   return type;
}

static void shader_manager_set_preset(enum rarch_shader_type type, const char *path)
{
   RARCH_LOG("Setting RGUI shader: %s.\n", path ? path : "N/A (stock)");
   bool ret = video_set_shader_func(type, path);
   if (ret)
   {
      // Makes sure that we use RGUI CGP shader on driver reinit.
      // Only do this when the cgp actually works to avoid potential errors.
      strlcpy(g_settings.video.shader_path, path ? path : "",
            sizeof(g_settings.video.shader_path));
      g_settings.video.shader_enable = true;
   }
   else
   {
      RARCH_ERR("Setting RGUI CGP failed.\n");
      g_settings.video.shader_enable = false;
   }
}

static int shader_manager_toggle_setting(rgui_handle_t *rgui, unsigned setting, rgui_action_t action)
{
   unsigned dist_shader = setting - RGUI_SETTINGS_SHADER_0;
   unsigned dist_filter = setting - RGUI_SETTINGS_SHADER_0_FILTER;
   unsigned dist_scale  = setting - RGUI_SETTINGS_SHADER_0_SCALE;

   if (setting == RGUI_SETTINGS_SHADER_FILTER)
   {
      switch (action)
      {
         case RGUI_ACTION_START:
            g_settings.video.smooth = true;
            break;

         case RGUI_ACTION_LEFT:
         case RGUI_ACTION_RIGHT:
         case RGUI_ACTION_OK:
            g_settings.video.smooth = !g_settings.video.smooth;
            break;

         default:
            break;
      }
   }
   else if (setting == RGUI_SETTINGS_SHADER_APPLY)
   {
      if (!driver.video->set_shader || action != RGUI_ACTION_OK)
         return 0;

      RARCH_LOG("Applying shader ...\n");

      enum rarch_shader_type type = shader_manager_get_type(&rgui->shader);

      if (rgui->shader.passes && type != RARCH_SHADER_NONE)
      {
         const char *conf_path = type == RARCH_SHADER_GLSL ? "rgui.glslp" : "rgui.cgp";

         char cgp_path[PATH_MAX];
         const char *shader_dir = *g_settings.video.shader_dir ?
            g_settings.video.shader_dir : g_settings.system_directory;
         fill_pathname_join(cgp_path, shader_dir, conf_path, sizeof(cgp_path));
         config_file_t *conf = config_file_new(NULL);
         if (!conf)
            return 0;
         gfx_shader_write_conf_cgp(conf, &rgui->shader);
         config_file_write(conf, cgp_path);
         config_file_free(conf);

         shader_manager_set_preset(type, cgp_path); 
      }
      else
      {
         type = gfx_shader_parse_type("", DEFAULT_SHADER_TYPE);
         if (type == RARCH_SHADER_NONE)
         {
#if defined(HAVE_GLSL)
            type = RARCH_SHADER_GLSL;
#elif defined(HAVE_CG) || defined(HAVE_HLSL)
            type = RARCH_SHADER_CG;
#endif
         }
         shader_manager_set_preset(type, NULL);
      }
   }
   else if (setting == RGUI_SETTINGS_SHADER_PASSES)
   {
      switch (action)
      {
         case RGUI_ACTION_START:
            rgui->shader.passes = 0;
            break;

         case RGUI_ACTION_LEFT:
            if (rgui->shader.passes)
               rgui->shader.passes--;
            break;

         case RGUI_ACTION_RIGHT:
         case RGUI_ACTION_OK:
            if (rgui->shader.passes < RGUI_MAX_SHADERS)
               rgui->shader.passes++;
            break;

         default:
            break;
      }

      rgui->need_refresh = true;
   }
   else if ((dist_shader % 3) == 0 || setting == RGUI_SETTINGS_SHADER_PRESET)
   {
      dist_shader /= 3;
      struct gfx_shader_pass *pass = setting == RGUI_SETTINGS_SHADER_PRESET ? 
         &rgui->shader.pass[dist_shader] : NULL;
      switch (action)
      {
         case RGUI_ACTION_LEFT:
         case RGUI_ACTION_RIGHT:
         case RGUI_ACTION_OK:
            rgui_list_push(rgui->menu_stack, g_settings.video.shader_dir, setting, rgui->selection_ptr);
            rgui->selection_ptr = 0;
            rgui->need_refresh = true;
            break;

         case RGUI_ACTION_START:
            if (pass)
               *pass->source.cg = '\0';
            break;

         default:
            break;
      }
   }
   else if ((dist_filter % 3) == 0)
   {
      dist_filter /= 3;
      struct gfx_shader_pass *pass = &rgui->shader.pass[dist_filter];
      switch (action)
      {
         case RGUI_ACTION_START:
            rgui->shader.pass[dist_filter].filter = RARCH_FILTER_UNSPEC;
            break;

         case RGUI_ACTION_LEFT:
         case RGUI_ACTION_RIGHT:
         case RGUI_ACTION_OK:
         {
            unsigned delta = action == RGUI_ACTION_LEFT ? 2 : 1;
            pass->filter = (enum gfx_filter_type)((pass->filter + delta) % 3);
            break;
         }

         default:
         break;
      }
   }
   else if ((dist_scale % 3) == 0)
   {
      dist_scale /= 3;
      struct gfx_shader_pass *pass = &rgui->shader.pass[dist_scale];
      switch (action)
      {
         case RGUI_ACTION_START:
            pass->fbo.scale_x = pass->fbo.scale_y = 0;
            pass->fbo.valid = false;
            break;

         case RGUI_ACTION_LEFT:
         case RGUI_ACTION_RIGHT:
         case RGUI_ACTION_OK:
         {
            unsigned current_scale = pass->fbo.scale_x;
            unsigned delta = action == RGUI_ACTION_LEFT ? 5 : 1;
            current_scale = (current_scale + delta) % 6;
            pass->fbo.valid = current_scale;
            pass->fbo.scale_x = pass->fbo.scale_y = current_scale;
            break;
         }

         default:
         break;
      }
   }

   return 0;
}

static void shader_manager_get_str(struct gfx_shader *shader,
      char *type_str, size_t type_str_size, unsigned type)
{
   if (type == RGUI_SETTINGS_SHADER_APPLY)
      *type_str = '\0';
   else if (type == RGUI_SETTINGS_SHADER_PASSES)
      snprintf(type_str, type_str_size, "%u", shader->passes);
   else
   {
      unsigned pass = (type - RGUI_SETTINGS_SHADER_0) / 3;
      switch ((type - RGUI_SETTINGS_SHADER_0) % 3)
      {
         case 0:
            if (*shader->pass[pass].source.cg)
               fill_pathname_base(type_str,
                     shader->pass[pass].source.cg, type_str_size);
            else
               strlcpy(type_str, "N/A", type_str_size);
            break;

         case 1:
            switch (shader->pass[pass].filter)
            {
               case RARCH_FILTER_LINEAR:
                  strlcpy(type_str, "Linear", type_str_size);
                  break;

               case RARCH_FILTER_NEAREST:
                  strlcpy(type_str, "Nearest", type_str_size);
                  break;

               case RARCH_FILTER_UNSPEC:
                  strlcpy(type_str, "Don't care", type_str_size);
                  break;
            }
            break;

         case 2:
         {
            unsigned scale = shader->pass[pass].fbo.scale_x;
            if (!scale)
               strlcpy(type_str, "Don't care", type_str_size);
            else
               snprintf(type_str, type_str_size, "%ux", scale);
            break;
         }
      }
   }
}

static void shader_manager_init(rgui_handle_t *rgui)
{
   config_file_t *conf = NULL;
   char cgp_path[PATH_MAX];

   const char *ext = path_get_extension(g_settings.video.shader_path);
   if (strcmp(ext, "glslp") == 0 || strcmp(ext, "cgp") == 0)
   {
      conf = config_file_new(g_settings.video.shader_path);
      if (conf)
      {
         if (gfx_shader_read_conf_cgp(conf, &rgui->shader))
            gfx_shader_resolve_relative(&rgui->shader, g_settings.video.shader_path);
         config_file_free(conf);
      }
   }
   else if (strcmp(ext, "glsl") == 0 || strcmp(ext, "cg") == 0)
   {
      strlcpy(rgui->shader.pass[0].source.cg, g_settings.video.shader_path,
            sizeof(rgui->shader.pass[0].source.cg));
      rgui->shader.passes = 1;
   }
   else
   {
      const char *shader_dir = *g_settings.video.shader_dir ?
         g_settings.video.shader_dir : g_settings.system_directory;

      fill_pathname_join(cgp_path, shader_dir, "rgui.glslp", sizeof(cgp_path));
      conf = config_file_new(cgp_path);

      if (!conf)
      {
         fill_pathname_join(cgp_path, shader_dir, "rgui.cgp", sizeof(cgp_path));
         conf = config_file_new(cgp_path);
      }

      if (conf)
      {
         if (gfx_shader_read_conf_cgp(conf, &rgui->shader))
            gfx_shader_resolve_relative(&rgui->shader, cgp_path);
         config_file_free(conf);
      }
   }
}
#endif

static void rgui_settings_controller_populate_entries(rgui_handle_t *rgui)
{
   rgui_list_clear(rgui->selection_buf);

   rgui_list_push(rgui->selection_buf, "Device", RGUI_SETTINGS_BIND_DEVICE, 0);
   rgui_list_push(rgui->selection_buf, "DPad Emulation", RGUI_SETTINGS_BIND_DPAD_EMULATION, 0);
   rgui_list_push(rgui->selection_buf, "Up", RGUI_SETTINGS_BIND_UP, 0);
   rgui_list_push(rgui->selection_buf, "Down", RGUI_SETTINGS_BIND_DOWN, 0);
   rgui_list_push(rgui->selection_buf, "Left", RGUI_SETTINGS_BIND_LEFT, 0);
   rgui_list_push(rgui->selection_buf, "Right", RGUI_SETTINGS_BIND_RIGHT, 0);
   rgui_list_push(rgui->selection_buf, "A", RGUI_SETTINGS_BIND_A, 0);
   rgui_list_push(rgui->selection_buf, "B", RGUI_SETTINGS_BIND_B, 0);
   rgui_list_push(rgui->selection_buf, "X", RGUI_SETTINGS_BIND_X, 0);
   rgui_list_push(rgui->selection_buf, "Y", RGUI_SETTINGS_BIND_Y, 0);
   rgui_list_push(rgui->selection_buf, "Start", RGUI_SETTINGS_BIND_START, 0);
   rgui_list_push(rgui->selection_buf, "Select", RGUI_SETTINGS_BIND_SELECT, 0);
   rgui_list_push(rgui->selection_buf, "L", RGUI_SETTINGS_BIND_L, 0);
   rgui_list_push(rgui->selection_buf, "R", RGUI_SETTINGS_BIND_R, 0);
   rgui_list_push(rgui->selection_buf, "L2", RGUI_SETTINGS_BIND_L2, 0);
   rgui_list_push(rgui->selection_buf, "R2", RGUI_SETTINGS_BIND_R2, 0);
   rgui_list_push(rgui->selection_buf, "L3", RGUI_SETTINGS_BIND_L3, 0);
   rgui_list_push(rgui->selection_buf, "R3", RGUI_SETTINGS_BIND_R3, 0);
}

static int rgui_viewport_iterate(rgui_handle_t *rgui, rgui_action_t action)
{
   rarch_viewport_t vp;
   driver.video->viewport_info(driver.video_data, &vp);
   unsigned win_width = vp.full_width;
   unsigned win_height = vp.full_height;
   unsigned menu_type = 0;
   rgui_list_get_last(rgui->menu_stack, NULL, &menu_type);

   (void)win_width;
   (void)win_height;

   switch (action)
   {
      case RGUI_ACTION_UP:
         if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT)
         {
            g_extern.console.screen.viewports.custom_vp.y -= 1;
            g_extern.console.screen.viewports.custom_vp.height += 1;
         }
         else
            g_extern.console.screen.viewports.custom_vp.height -= 1;
         if (driver.video_poke->apply_state_changes)
            driver.video_poke->apply_state_changes(driver.video_data);
         break;

      case RGUI_ACTION_DOWN:
         if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT)
         {
            g_extern.console.screen.viewports.custom_vp.y += 1;
            g_extern.console.screen.viewports.custom_vp.height -= 1;
         }
         else
            g_extern.console.screen.viewports.custom_vp.height += 1;
         if (driver.video_poke->apply_state_changes)
            driver.video_poke->apply_state_changes(driver.video_data);
         break;

      case RGUI_ACTION_LEFT:
         if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT)
         {
            g_extern.console.screen.viewports.custom_vp.x -= 1;
            g_extern.console.screen.viewports.custom_vp.width += 1;
         }
         else
            g_extern.console.screen.viewports.custom_vp.width -= 1;
         if (driver.video_poke->apply_state_changes)
            driver.video_poke->apply_state_changes(driver.video_data);
         break;

      case RGUI_ACTION_RIGHT:
         if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT)
         {
            g_extern.console.screen.viewports.custom_vp.x += 1;
            g_extern.console.screen.viewports.custom_vp.width -= 1;
         }
         else
            g_extern.console.screen.viewports.custom_vp.width += 1;
         if (driver.video_poke->apply_state_changes)
            driver.video_poke->apply_state_changes(driver.video_data);
         break;

      case RGUI_ACTION_CANCEL:
         rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);
         if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT_2)
            rgui_list_push(rgui->menu_stack, "", RGUI_SETTINGS_CUSTOM_VIEWPORT, 0);
         break;

      case RGUI_ACTION_OK:
         rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);
         if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT)
            rgui_list_push(rgui->menu_stack, "", RGUI_SETTINGS_CUSTOM_VIEWPORT_2, 0);
         break;

      case RGUI_ACTION_START:
#ifdef GEKKO
         if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT)
         {
            g_extern.console.screen.viewports.custom_vp.width += g_extern.console.screen.viewports.custom_vp.x;
            g_extern.console.screen.viewports.custom_vp.height += g_extern.console.screen.viewports.custom_vp.y;
            g_extern.console.screen.viewports.custom_vp.x = 0;
            g_extern.console.screen.viewports.custom_vp.y = 0;
         }
         else
         {
            g_extern.console.screen.viewports.custom_vp.width = win_width - g_extern.console.screen.viewports.custom_vp.x;
            g_extern.console.screen.viewports.custom_vp.height = win_height - g_extern.console.screen.viewports.custom_vp.y;
         }
#endif
         if (driver.video_poke->apply_state_changes)
            driver.video_poke->apply_state_changes(driver.video_data);
         break;

      case RGUI_ACTION_SETTINGS:
         rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);
         break;

      case RGUI_ACTION_MESSAGE:
         rgui->msg_force = true;
         break;

      default:
         break;
   }

   rgui_list_get_last(rgui->menu_stack, NULL, &menu_type);

   render_text(rgui);

   if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT)
      render_messagebox(rgui, "Set Upper-Left Corner");
   else if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT_2)
      render_messagebox(rgui, "Set Bottom-Right Corner");

   return 0;
}

static int rgui_settings_iterate(rgui_handle_t *rgui, rgui_action_t action)
{
   rgui->frame_buf_pitch = RGUI_WIDTH * 2;
   unsigned type = 0;
   const char *label = NULL;
   if (action != RGUI_ACTION_REFRESH)
      rgui_list_get_at_offset(rgui->selection_buf, rgui->selection_ptr, &label, &type);

#if defined(HAVE_DYNAMIC)
   if (type == RGUI_SETTINGS_CORE)
   {
      if (path_is_directory(g_settings.libretro))
         strlcpy(rgui->libretro_dir, g_settings.libretro, sizeof(rgui->libretro_dir));
      else if (*g_settings.libretro)
         fill_pathname_basedir(rgui->libretro_dir, g_settings.libretro, sizeof(rgui->libretro_dir));
      else
         *rgui->libretro_dir = '\0';
      label = rgui->libretro_dir;
   }
#elif defined(HAVE_LIBRETRO_MANAGEMENT)
   if (type == RGUI_SETTINGS_CORE)
      label = default_paths.core_dir;
#endif

   const char *dir = NULL;
   unsigned menu_type = 0;
   rgui_list_get_last(rgui->menu_stack, &dir, &menu_type);

   if (rgui->need_refresh)
      action = RGUI_ACTION_NOOP;

   switch (action)
   {
      case RGUI_ACTION_UP:
         if (rgui->selection_ptr > 0)
            rgui->selection_ptr--;
         else
            rgui->selection_ptr = rgui->selection_buf->size - 1;
         break;

      case RGUI_ACTION_DOWN:
         if (rgui->selection_ptr + 1 < rgui->selection_buf->size)
            rgui->selection_ptr++;
         else
            rgui->selection_ptr = 0;
         break;

      case RGUI_ACTION_CANCEL:
      case RGUI_ACTION_SETTINGS:
         if (rgui->menu_stack->size > 1)
         {
            rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);
            rgui->need_refresh = true;
         }
         g_extern.lifecycle_mode_state &= ~(1ULL << MODE_MENU_INGAME);
         break;

      case RGUI_ACTION_LEFT:
      case RGUI_ACTION_RIGHT:
      case RGUI_ACTION_OK:
      case RGUI_ACTION_START:
         if (type == RGUI_SETTINGS_OPEN_FILEBROWSER && action == RGUI_ACTION_OK)
         {
            rgui_list_push(rgui->menu_stack, rgui->base_path, RGUI_FILE_DIRECTORY, rgui->selection_ptr);
            rgui->selection_ptr = 0;
            rgui->need_refresh = true;
         }
         else if ((menu_type_is_settings(type) || type == RGUI_SETTINGS_CORE) && action == RGUI_ACTION_OK)
         {
            rgui_list_push(rgui->menu_stack, label, type, rgui->selection_ptr);
            rgui->selection_ptr = 0;
            rgui->need_refresh = true;
         }
         else if (type == RGUI_SETTINGS_CUSTOM_VIEWPORT && action == RGUI_ACTION_OK)
         {
            rgui_list_push(rgui->menu_stack, "", type, rgui->selection_ptr);
            g_settings.video.aspect_ratio_idx = ASPECT_RATIO_CUSTOM;

            if (driver.video_poke->set_aspect_ratio)
               driver.video_poke->set_aspect_ratio(driver.video_data, g_settings.video.aspect_ratio_idx);
         }
         else
         {
            int ret = rgui_settings_toggle_setting(rgui, type, action, menu_type);

            if (ret != 0)
               return ret;
         }
         break;

      case RGUI_ACTION_REFRESH:
         rgui->selection_ptr = 0;
         rgui->need_refresh = true;
         break;

      case RGUI_ACTION_MESSAGE:
         rgui->msg_force = true;
         break;

      default:
         break;
   }

   rgui_list_get_last(rgui->menu_stack, &dir, &menu_type);

   if (rgui->need_refresh && !(menu_type == RGUI_FILE_DIRECTORY || menu_type_is_shader_browser(menu_type) ||
            menu_type == RGUI_FILE_DEVICE || menu_type == RGUI_SETTINGS_CORE))
   {
      rgui->need_refresh = false;
      if ((menu_type >= RGUI_SETTINGS_CONTROLLER_1 && menu_type <= RGUI_SETTINGS_CONTROLLER_4))
         rgui_settings_controller_populate_entries(rgui);
      else if (menu_type == RGUI_SETTINGS_CORE_OPTIONS)
         rgui_settings_core_options_populate_entries(rgui);
#ifdef HAVE_SHADER_MANAGER
      else if (menu_type == RGUI_SETTINGS_SHADER_MANAGER)
         rgui_settings_shader_manager_populate_entries(rgui);
#endif
      else
         rgui_settings_populate_entries(rgui);
   }

   // If we go back to file browser, we must refresh.
   // The file browser state is stale.
   if (rgui->menu_stack->size > 1)
      render_text(rgui);

   return 0;
}

static bool directory_parse(rgui_handle_t *rgui, const char *directory, unsigned menu_type, void *ctx)
{
   bool core_chooser = menu_type == RGUI_SETTINGS_CORE;

   if (!*directory)
   {
#if defined(GEKKO)
#ifdef HW_RVL
      rgui_list_push(ctx, "sd:/", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "usb:/", RGUI_FILE_DEVICE, 0);
#endif
      rgui_list_push(ctx, "carda:/", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "cardb:/", RGUI_FILE_DEVICE, 0);
      return true;
#elif defined(_XBOX1)
      rgui_list_push(ctx, "C:\\", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "D:\\", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "E:\\", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "F:\\", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "G:\\", RGUI_FILE_DEVICE, 0);
      return true;
#elif defined(_WIN32)
      unsigned drives = GetLogicalDrives();
      char drive[] = " :\\";
      for (unsigned i = 0; i < 32; i++)
      {
         drive[0] = 'A' + i;
         if (drives & (1 << i))
            rgui_list_push(ctx, drive, RGUI_FILE_DEVICE, 0);
      }
      return true;
#elif defined(__CELLOS_LV2__)
      rgui_list_push(ctx, "app_home:/", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "dev_hdd0:/", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "dev_hdd1:/", RGUI_FILE_DEVICE, 0);
      rgui_list_push(ctx, "host_root:/", RGUI_FILE_DEVICE, 0);
      return true;
#endif
   }

#if defined(GEKKO) && defined(HW_RVL)
   LWP_MutexLock(gx_device_mutex);
   int dev = gx_get_device_from_path(directory);

   if (dev != -1 && !gx_devices[dev].mounted && gx_devices[dev].interface->isInserted())
      fatMountSimple(gx_devices[dev].name, gx_devices[dev].interface);

   LWP_MutexUnlock(gx_device_mutex);
#endif

   const char *exts;
   if (core_chooser)
      exts = EXT_EXECUTABLES;
#ifdef HAVE_SHADER_MANAGER
   else if (menu_type == RGUI_SETTINGS_SHADER_PRESET)
      exts = "cgp|glslp";
   else if (menu_type_is_shader_browser(menu_type))
      exts = "cg|glsl";
#endif
   else if (rgui->info.valid_extensions)
      exts = rgui->info.valid_extensions;
   else
      exts = g_extern.system.valid_extensions;

   char dir[PATH_MAX];
   if (*directory)
      strlcpy(dir, directory, sizeof(dir));
   else
      strlcpy(dir, "/", sizeof(dir));

   struct string_list *list = dir_list_new(dir, exts, true);
   if (!list)
      return false;

   dir_list_sort(list, true);

   // Can only have devices as root.
   if (menu_type == RGUI_FILE_DEVICE)
      menu_type = RGUI_FILE_DIRECTORY;

   for (size_t i = 0; i < list->size; i++)
   {
      bool is_dir = list->elems[i].attr.b;
      if (core_chooser && (is_dir
#ifdef HAVE_LIBRETRO_MANAGEMENT
               || strcasecmp(list->elems[i].data, SALAMANDER_FILE) == 0
#endif
         ))
         continue;

      // Need to preserve slash first time.
      const char *path = list->elems[i].data;
      if (*directory)
         path = path_basename(path);

      // Push menu_type further down in the chain.
      // Needed for shader manager currently.
      rgui_list_push(ctx, path,
            is_dir ? menu_type : RGUI_FILE_PLAIN, 0);
   }

   string_list_free(list);
   return true;
}

int rgui_iterate(rgui_handle_t *rgui, rgui_action_t action)
{
   const char *dir = 0;
   unsigned menu_type = 0;
   rgui_list_get_last(rgui->menu_stack, &dir, &menu_type);
   int ret = 0;

   if (menu_type_is_settings(menu_type))
      return rgui_settings_iterate(rgui, action);
   else if (menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT || menu_type == RGUI_SETTINGS_CUSTOM_VIEWPORT_2)
      return rgui_viewport_iterate(rgui, action);
   if (rgui->need_refresh && action != RGUI_ACTION_MESSAGE)
      action = RGUI_ACTION_NOOP;

   switch (action)
   {
      case RGUI_ACTION_UP:
         if (rgui->selection_ptr > 0)
            rgui->selection_ptr--;
         else
            rgui->selection_ptr = rgui->selection_buf->size - 1;
         break;

      case RGUI_ACTION_DOWN:
         if (rgui->selection_ptr + 1 < rgui->selection_buf->size)
            rgui->selection_ptr++;
         else
            rgui->selection_ptr = 0;
         break;

      case RGUI_ACTION_LEFT:
         if (rgui->selection_ptr > 8)
            rgui->selection_ptr -= 8;
         else
            rgui->selection_ptr = 0;
         break;

      case RGUI_ACTION_RIGHT:
         if (rgui->selection_ptr + 8 < rgui->selection_buf->size)
            rgui->selection_ptr += 8;
         else
            rgui->selection_ptr = rgui->selection_buf->size - 1;
         break;
      
      case RGUI_ACTION_CANCEL:
         if (rgui->menu_stack->size > 1)
         {
            rgui->need_refresh = true;
            rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);
         }
         break;

      case RGUI_ACTION_OK:
      {
         if (rgui->selection_buf->size == 0)
            return 0;

         const char *path = 0;
         unsigned type = 0;
         rgui_list_get_at_offset(rgui->selection_buf, rgui->selection_ptr, &path, &type);

         if (menu_type_is_shader_browser(type) || type == RGUI_FILE_DIRECTORY)
         {
            char cat_path[PATH_MAX];
            fill_pathname_join(cat_path, dir, path, sizeof(cat_path));

            rgui_list_push(rgui->menu_stack, cat_path, type, rgui->selection_ptr);
            rgui->selection_ptr = 0;
            rgui->need_refresh = true;
         }
         else if (type == RGUI_FILE_DEVICE)
         {
            rgui_list_push(rgui->menu_stack, path, RGUI_FILE_DEVICE, rgui->selection_ptr);
            rgui->selection_ptr = 0;
            rgui->need_refresh = true;
         }
         else
         {
#ifdef HAVE_SHADER_MANAGER
            if (menu_type_is_shader_browser(menu_type))
            {
               if (menu_type == RGUI_SETTINGS_SHADER_PRESET)
               {
                  char shader_path[PATH_MAX];
                  fill_pathname_join(shader_path, dir, path, sizeof(shader_path));
                  shader_manager_set_preset(gfx_shader_parse_type(shader_path, RARCH_SHADER_NONE),
                        shader_path);
               }
               else
               {
                  unsigned pass = (menu_type - RGUI_SETTINGS_SHADER_0) / 3;
                  fill_pathname_join(rgui->shader.pass[pass].source.cg,
                        dir, path, sizeof(rgui->shader.pass[pass].source.cg));
               }

               // Pop stack until we hit shader manager again.
               // We don't have to do this in CORE selection because it only
               // uses one directory.
               unsigned type = 0;
               const char *dir = NULL;
               rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);
               rgui_list_get_last(rgui->menu_stack, &dir, &type);
               while (type != RGUI_SETTINGS_SHADER_MANAGER)
               {
                  rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);
                  rgui_list_get_last(rgui->menu_stack, &dir, &type);
               }
               rgui->need_refresh = true;
            }
            else
#endif
            if (menu_type == RGUI_SETTINGS_CORE)
            {
               rgui->need_refresh = true;
               rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);

#if defined(HAVE_DYNAMIC)
               fill_pathname_join(g_settings.libretro, rgui->libretro_dir, path, sizeof(g_settings.libretro));
               libretro_free_system_info(&rgui->info);
               libretro_get_system_info(g_settings.libretro, &rgui->info);
               // Core selection on non-console just updates directory listing.
               // Will take affect on new ROM load.
#elif defined(GEKKO)
               strlcpy(g_settings.libretro, path, sizeof(g_settings.libretro)); // Is this supposed to be here?
               fill_pathname_join(g_extern.fullpath, default_paths.core_dir,
                     SALAMANDER_FILE, sizeof(g_extern.fullpath));
               g_extern.lifecycle_mode_state &= ~(1ULL << MODE_GAME);
               g_extern.lifecycle_mode_state |= (1ULL << MODE_EXIT);
               g_extern.lifecycle_mode_state |= (1ULL << MODE_EXITSPAWN);
               ret = -1;
#endif
            }
            else
            {
               fill_pathname_join(g_extern.fullpath, dir, path, sizeof(g_extern.fullpath));

               g_extern.lifecycle_mode_state |= (1ULL << MODE_LOAD_GAME);

               if (g_extern.lifecycle_mode_state & (1ULL << MODE_INFO_DRAW))
               {
                  char tmp[PATH_MAX];
                  char str[PATH_MAX];

                  fill_pathname_base(tmp, g_extern.fullpath, sizeof(tmp));
                  snprintf(str, sizeof(str), "INFO - Loading %s...", tmp);
                  msg_queue_push(g_extern.msg_queue, str, 1, 1);
               }

               rgui->need_refresh = true; // in case of zip extract
               rgui->msg_force = true;
               ret = -1;
            }
         }
         break;
      }

      case RGUI_ACTION_REFRESH:
         rgui->selection_ptr = 0;
         rgui->need_refresh = true;
         break;

      case RGUI_ACTION_SETTINGS:
         rgui->need_refresh = true;
         while (rgui->menu_stack->size > 1)
            rgui_list_pop(rgui->menu_stack, &rgui->selection_ptr);
         rgui_list_push(rgui->menu_stack, "", RGUI_SETTINGS, rgui->selection_ptr);
         rgui->selection_ptr = 0;
         g_extern.lifecycle_mode_state |= (1ULL << MODE_MENU_INGAME);
         return rgui_settings_iterate(rgui, RGUI_ACTION_REFRESH);

      case RGUI_ACTION_MESSAGE:
         rgui->msg_force = true;
         break;

      default:
         break;
   }

   // refresh values in case the stack changed
   rgui_list_get_last(rgui->menu_stack, &dir, &menu_type);

   if (rgui->need_refresh && (menu_type == RGUI_FILE_DIRECTORY || menu_type_is_shader_browser(menu_type) ||
            menu_type == RGUI_FILE_DEVICE || menu_type == RGUI_SETTINGS_CORE))
   {
      rgui->need_refresh = false;
      rgui_list_clear(rgui->selection_buf);

      directory_parse(rgui, dir, menu_type, rgui->selection_buf);
   }

   render_text(rgui);

   return ret;
}

static const struct retro_keybind _menu_nav_binds[] = {
#if defined(HW_RVL)
   { 0, 0, NULL, 0, GX_GC_UP | GX_GC_LSTICK_UP | GX_GC_RSTICK_UP | GX_CLASSIC_UP | GX_CLASSIC_LSTICK_UP | GX_CLASSIC_RSTICK_UP | GX_WIIMOTE_UP | GX_NUNCHUK_UP, 0 },
   { 0, 0, NULL, 0, GX_GC_DOWN | GX_GC_LSTICK_DOWN | GX_GC_RSTICK_DOWN | GX_CLASSIC_DOWN | GX_CLASSIC_LSTICK_DOWN | GX_CLASSIC_RSTICK_DOWN | GX_WIIMOTE_DOWN | GX_NUNCHUK_DOWN, 0 },
   { 0, 0, NULL, 0, GX_GC_LEFT | GX_GC_LSTICK_LEFT | GX_GC_RSTICK_LEFT | GX_CLASSIC_LEFT | GX_CLASSIC_LSTICK_LEFT | GX_CLASSIC_RSTICK_LEFT | GX_WIIMOTE_LEFT | GX_NUNCHUK_LEFT, 0 },
   { 0, 0, NULL, 0, GX_GC_RIGHT | GX_GC_LSTICK_RIGHT | GX_GC_RSTICK_RIGHT | GX_CLASSIC_RIGHT | GX_CLASSIC_LSTICK_RIGHT | GX_CLASSIC_RSTICK_RIGHT | GX_WIIMOTE_RIGHT | GX_NUNCHUK_RIGHT, 0 },
   { 0, 0, NULL, 0, GX_GC_A | GX_CLASSIC_A | GX_WIIMOTE_A | GX_WIIMOTE_2, 0 },
   { 0, 0, NULL, 0, GX_GC_B | GX_CLASSIC_B | GX_WIIMOTE_B | GX_WIIMOTE_1, 0 },
   { 0, 0, NULL, 0, GX_GC_START | GX_CLASSIC_PLUS | GX_WIIMOTE_PLUS, 0 },
   { 0, 0, NULL, 0, GX_GC_Z_TRIGGER | GX_CLASSIC_MINUS | GX_WIIMOTE_MINUS, 0 },
   { 0, 0, NULL, 0, GX_WIIMOTE_HOME | GX_CLASSIC_HOME, 0 },
#elif defined(HW_DOL)
   { 0, 0, NULL, 0, GX_GC_UP | GX_GC_LSTICK_UP | GX_GC_RSTICK_UP, 0 },
   { 0, 0, NULL, 0, GX_GC_DOWN | GX_GC_LSTICK_DOWN | GX_GC_RSTICK_DOWN, 0 },
   { 0, 0, NULL, 0, GX_GC_LEFT | GX_GC_LSTICK_LEFT | GX_GC_RSTICK_LEFT, 0 },
   { 0, 0, NULL, 0, GX_GC_RIGHT | GX_GC_LSTICK_RIGHT | GX_GC_RSTICK_RIGHT, 0 },
   { 0, 0, NULL, 0, GX_GC_A, 0 },
   { 0, 0, NULL, 0, GX_GC_B, 0 },
   { 0, 0, NULL, 0, GX_GC_START, 0 },
   { 0, 0, NULL, 0, GX_GC_Z_TRIGGER, 0 },
   { 0, 0, NULL, 0, GX_WIIMOTE_HOME, 0 },
#else
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RETRO_DEVICE_ID_JOYPAD_UP), 0 },
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RETRO_DEVICE_ID_JOYPAD_DOWN), 0 },
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RETRO_DEVICE_ID_JOYPAD_LEFT), 0 },
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RETRO_DEVICE_ID_JOYPAD_RIGHT), 0 },
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RETRO_DEVICE_ID_JOYPAD_A), 0 },
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RETRO_DEVICE_ID_JOYPAD_B), 0 },
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RETRO_DEVICE_ID_JOYPAD_START), 0 },
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RETRO_DEVICE_ID_JOYPAD_SELECT), 0 },
   { 0, 0, NULL, (enum retro_key)0, (1ULL << RARCH_MENU_TOGGLE), 0 },
#endif
};

static const struct retro_keybind *menu_nav_binds[] = {
   _menu_nav_binds
};

enum
{
   DEVICE_NAV_UP = 0,
   DEVICE_NAV_DOWN,
   DEVICE_NAV_LEFT,
   DEVICE_NAV_RIGHT,
   DEVICE_NAV_A,
   DEVICE_NAV_B,
   DEVICE_NAV_START,
   DEVICE_NAV_SELECT,
   DEVICE_NAV_MENU,
   RMENU_DEVICE_NAV_LAST
};


/*============================================================
RMENU API
============================================================ */

void menu_init(void)
{
   rgui = rgui_init(g_settings.rgui_browser_directory,
         menu_framebuf, RGUI_WIDTH * sizeof(uint16_t),
         NULL, bitmap_bin);

   rgui_iterate(rgui, RGUI_ACTION_REFRESH);
}

void menu_free(void)
{
   rgui_free(rgui);
}

static uint16_t trigger_state = 0;

static int menu_input_process(void *data, void *state)
{
   if (g_extern.lifecycle_mode_state & (1ULL << MODE_LOAD_GAME))
   {
      if (g_extern.fullpath)
         g_extern.lifecycle_mode_state |= (1ULL << MODE_INIT);

      g_extern.lifecycle_mode_state &= ~(1ULL << MODE_LOAD_GAME);
      return -1;
   }

   if (!(g_extern.frame_count < g_extern.delay_timer[0]))
   {
      if ((trigger_state & (1ULL << DEVICE_NAV_MENU)) && g_extern.main_is_init)
      {
         if (g_extern.lifecycle_mode_state & (1ULL << MODE_MENU_INGAME))
            g_extern.lifecycle_mode_state |= (1ULL << MODE_MENU_INGAME_EXIT);
         
         g_extern.lifecycle_mode_state |= (1ULL << MODE_GAME);
         return -1;
      }
   }

   return 0;
}

static uint64_t menu_input_state(void)
{
   uint64_t input_state = 0;

   // FIXME: Very ugly. Should do something more uniform.
#if defined(RARCH_CONSOLE) || defined(ANDROID)
   for (unsigned i = 0; i < RMENU_DEVICE_NAV_LAST; i++)
      input_state |= driver.input->input_state(driver.input_data, menu_nav_binds, 0,
            RETRO_DEVICE_JOYPAD, 0, i) ? (1ULL << i) : 0;

   input_state |= driver.input->key_pressed(driver.input_data, RARCH_MENU_TOGGLE) ? (1ULL << DEVICE_NAV_MENU) : 0;

#ifdef HAVE_OVERLAY
   for (unsigned i = 0; i < RMENU_DEVICE_NAV_LAST; i++)
      input_state |= driver.overlay_state & menu_nav_binds[0][i].joykey ? (1ULL << i) : 0;
#endif
#else
   static const int maps[] = {
      RETRO_DEVICE_ID_JOYPAD_UP,     DEVICE_NAV_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,   DEVICE_NAV_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,   DEVICE_NAV_LEFT,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,  DEVICE_NAV_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_A,      DEVICE_NAV_A,
      RETRO_DEVICE_ID_JOYPAD_B,      DEVICE_NAV_B,
      RETRO_DEVICE_ID_JOYPAD_START,  DEVICE_NAV_START,
      RETRO_DEVICE_ID_JOYPAD_SELECT, DEVICE_NAV_SELECT,
   };

   static const struct retro_keybind *binds[] = { g_settings.input.binds[0] };

   for (unsigned i = 0; i < ARRAY_SIZE(maps); i += 2)
   {
      input_state |= input_input_state_func(binds,
            0, RETRO_DEVICE_JOYPAD, 0, maps[i + 0]) ? (1ULL << maps[i + 1]) : 0;
#ifdef HAVE_OVERLAY
      input_state |= (driver.overlay_state & (UINT64_C(1) << maps[i + 0])) ? (1ULL << maps[i + 1]) : 0;
#endif
   }

   input_state |= input_key_pressed_func(RARCH_MENU_TOGGLE) ? (1ULL << DEVICE_NAV_MENU) : 0;
#endif

   return input_state;
}

bool menu_iterate(void)
{
   static uint64_t old_input_state = 0;
   static bool initial_held = true;
   static bool first_held = false;
   bool do_held;
   int input_entry_ret, input_process_ret;
   rgui_action_t action;
   uint64_t input_state = 0;

   if (g_extern.lifecycle_mode_state & (1ULL << MODE_MENU_PREINIT))
   {
      if (g_extern.lifecycle_mode_state & (1ULL << MODE_MENU_INGAME))
         rgui->need_refresh = true;

      g_extern.lifecycle_mode_state &= ~(1ULL << MODE_MENU_PREINIT);
   }

   if (driver.video_poke->apply_state_changes)
      driver.video_poke->apply_state_changes(driver.video_data);

   rarch_input_poll();
#ifdef HAVE_OVERLAY
   rarch_check_overlay();
#endif
#ifndef RARCH_PERFORMANCE_MODE
   rarch_check_fullscreen();
#endif

#ifndef GEKKO
   /* TODO - not sure if correct regarding RARCH_QUIT_KEY */
   if (input_key_pressed_func(RARCH_QUIT_KEY) || !video_alive_func())
   {
      g_extern.lifecycle_mode_state |= (1ULL << MODE_GAME);
      goto deinit;
   }
#endif

   input_state = menu_input_state();

   trigger_state = input_state & ~old_input_state;
   do_held = (input_state & ((1ULL << DEVICE_NAV_UP) | (1ULL << DEVICE_NAV_DOWN) | (1ULL << DEVICE_NAV_LEFT) | (1ULL << DEVICE_NAV_RIGHT))) && !(input_state & ((1ULL << DEVICE_NAV_MENU)));

   if(do_held)
   {
      if(!first_held)
      {
         first_held = true;
         g_extern.delay_timer[1] = g_extern.frame_count + (initial_held ? 15 : 7);
      }

      if (!(g_extern.frame_count < g_extern.delay_timer[1]))
      {
         first_held = false;
         trigger_state = input_state; //second input frame set as current frame
      }

      initial_held = false;
   }
   else
   {
      first_held = false;
      initial_held = true;
   }

   old_input_state = input_state;
   action = RGUI_ACTION_NOOP;

   // don't run anything first frame, only capture held inputs for old_input_state
   if (trigger_state & (1ULL << DEVICE_NAV_UP))
      action = RGUI_ACTION_UP;
   else if (trigger_state & (1ULL << DEVICE_NAV_DOWN))
      action = RGUI_ACTION_DOWN;
   else if (trigger_state & (1ULL << DEVICE_NAV_LEFT))
      action = RGUI_ACTION_LEFT;
   else if (trigger_state & (1ULL << DEVICE_NAV_RIGHT))
      action = RGUI_ACTION_RIGHT;
   else if (trigger_state & (1ULL << DEVICE_NAV_B))
      action = RGUI_ACTION_CANCEL;
   else if (trigger_state & (1ULL << DEVICE_NAV_A))
      action = RGUI_ACTION_OK;
   else if (trigger_state & (1ULL << DEVICE_NAV_SELECT))
      action = RGUI_ACTION_START;
   else if (trigger_state & (1ULL << DEVICE_NAV_START))
      action = RGUI_ACTION_SETTINGS;

   input_entry_ret = 0;
   input_process_ret = 0;

   input_entry_ret = rgui_iterate(rgui, action);

   // draw last frame for loading messages
   if (driver.video_poke && driver.video_poke->set_texture_enable)
   {
      driver.video_poke->set_texture_frame(driver.video_data, menu_framebuf,
            false, RGUI_WIDTH, RGUI_HEIGHT, 1.0f);
      driver.video_poke->set_texture_enable(driver.video_data, true, false);
   }

   rarch_render_cached_frame();

   if (driver.video_poke && driver.video_poke->set_texture_enable)
      driver.video_poke->set_texture_enable(driver.video_data, false, false);

   input_process_ret = menu_input_process(NULL, NULL);

   if (g_extern.lifecycle_mode_state & (1ULL << MODE_MENU_INGAME_EXIT) &&
         g_extern.lifecycle_mode_state & (1ULL << MODE_MENU_INGAME))
   {
      g_extern.lifecycle_mode_state &= ~((1ULL << MODE_MENU_INGAME) | (1ULL << MODE_MENU_INGAME_EXIT));
   }

   if (input_entry_ret != 0 || input_process_ret != 0)
      goto deinit;

   return true;

deinit:
   // set a timer delay so that we don't instantly switch back to the menu when
   // press and holding QUIT in the emulation loop (lasts for 30 frame ticks)
   if (!(g_extern.lifecycle_state & (1ULL << RARCH_FRAMEADVANCE)))
      g_extern.delay_timer[0] = g_extern.frame_count + 30;

   g_extern.lifecycle_mode_state &= ~(1ULL << MODE_MENU_INGAME);

   return false;
}

