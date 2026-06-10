/* config.h -- global configuration and config file handling
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// should be enough for pretend purposes
#define MEMORY_MB 512

// 2.1.131 ships the game as libGame.so and dynamically links the C++
// runtime, which we load as a second module
#define SO_NAME "libGame.so"
#define CXX_SO_NAME "libc++_shared.so"
#define CONFIG_NAME "config.txt"
#define LOG_NAME "debug.log"

// per-line SD card writes make this cost several seconds of boot time;
// re-enable only when investigating a crash or misbehavior
// #define DEBUG_LOG 1

// actual screen size
extern int screen_width;
extern int screen_height;

typedef struct {
  int screen_width;
  int screen_height;
  int use_bloom;
  int trilinear_filter;
  int disable_mipmaps;
  int language;
  int crouch_toggle;
  int touchscreen;
  int character_shadows;
  int drop_highest_lod;
  int show_weapon_menu;
  float decal_limit;
  float debris_limit;
  char mod_file[0x100];
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
