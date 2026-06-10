/* game.c -- hooks and patches for everything other than AL and GL
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 *
 * 2.1.131 update: most of the old NVIDIA-era hooks are gone because their
 * targets no longer exist. The platform layer is now serviced through the
 * fake JNI environment (jni_fake.c); only engine-level patches remain here.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <threads.h>
#include <switch.h>

#include "../config.h"
#include "../util.h"
#include "../so_util.h"
#include "../hooks.h"
#include "../jni_fake.h"

extern so_module game_mod; // defined in main.c

typedef struct {
  int (*func)(void *);
  void *arg;
  uint8_t tls[0x100];
} OSThreadStart;

typedef struct {
  void *(*func)(void *);
  void *arg;
  uint8_t tls[0x100];
} NVThreadStart;

static uint8_t main_fake_tls[0x100];

static void init_fake_tls(uint8_t *tls) {
  memset(tls, 0, 0x100);
  armSetTlsRw(tls);
}

// control binding array
typedef struct {
  int unk[14];
} MaxPayne_InputControl;
static MaxPayne_InputControl *sm_control = NULL; // [32]

int OS_ScreenGetHeight(void) {
  return screen_height;
}

int OS_ScreenGetWidth(void) {
  return screen_width;
}

static int os_thread_trampoline(void *arg) {
  OSThreadStart *start = arg;
  int (*func)(void *) = start->func;
  void *user_arg = start->arg;
  init_fake_tls(start->tls);
  const int rc = func(user_arg);
  free(start);
  return rc;
}

static int nv_thread_trampoline(void *arg) {
  NVThreadStart *start = arg;
  void *(*func)(void *) = start->func;
  void *user_arg = start->arg;
  init_fake_tls(start->tls);
  void *rc = func(user_arg);
  free(start);
  return (int)(intptr_t)rc;
}

// this is supposed to allocate and return a thread handle struct, but the game
// only uses it as an opaque handle. Keep a small static placeholder, and run the
// target through a trampoline so TPIDR_EL0 is valid on game-created threads.
void *OS_ThreadLaunch(int (* func)(void *), void *arg, int r2, char *name, int r4, int priority) {
  (void)r2; (void)r4; (void)priority;
  static char buf[0x80];
  debugPrintf("OS_ThreadLaunch: %s\n", name ? name : "(unnamed)");
  OSThreadStart *start = calloc(1, sizeof(*start));
  if (!start)
    return NULL;
  start->func = func;
  start->arg = arg;
  thrd_t thrd;
  if (thrd_create(&thrd, os_thread_trampoline, start) != thrd_success) {
    free(start);
    return NULL;
  }
  return buf;
}

// the game spawns its loader/sound threads through this NVThread wrapper now
int NVThreadSpawnJNIThread(long *tid, const void *attr, const char *name, void *(*fn)(void *), void *arg) {
  (void)attr;
  debugPrintf("NVThreadSpawnJNIThread: %s\n", name ? name : "(unnamed)");
  NVThreadStart *start = calloc(1, sizeof(*start));
  if (!start)
    return -1;
  start->func = fn;
  start->arg = arg;
  thrd_t thrd;
  if (thrd_create(&thrd, nv_thread_trampoline, start) != thrd_success) {
    free(start);
    return -1;
  }
  if (tid)
    *tid = (long)thrd;
  return 0;
}

// always hand out our fake JNIEnv; the real one TLS-caches a env pointer
// that we can't provide
void *NVThreadGetCurrentJNIEnv(void) {
  return fake_env;
}

static int (* MaxPayne_InputControl_getButton)(MaxPayne_InputControl *, int);

int MaxPayne_ConfiguredInput_readCrouch(void *this) {
  static int prev = 0;
  static int latch = 0;
  // crouch is control #5
  const int new = MaxPayne_InputControl_getButton(&sm_control[5], 0);
  if (prev != new) {
    prev = new;
    if (new) latch = !latch;
  }
  return latch;
}

int X_DetailLevel_getCharacterShadows(void) {
  return config.character_shadows;
}

int X_DetailLevel_getDropHighestLOD(void) {
  return config.drop_highest_lod;
}

float X_DetailLevel_getDecalLimitMultiplier(void) {
  return config.decal_limit;
}

float X_DetailLevel_getDebrisProjectileLimitMultiplier(void) {
  return config.debris_limit;
}

void patch_game(void) {
  // route JNIEnv access through the fake environment
  hook_arm64(so_find_addr(&game_mod, "_Z24NVThreadGetCurrentJNIEnvv"), (uintptr_t)NVThreadGetCurrentJNIEnv);
  hook_arm64(so_find_addr(&game_mod, "_Z22NVThreadSpawnJNIThreadPlPK14pthread_attr_tPKcPFPvS5_ES5_"), (uintptr_t)NVThreadSpawnJNIThread);

  hook_arm64(so_find_addr(&game_mod, "_Z15OS_ThreadLaunchPFjPvES_jPKci16OSThreadPriority"), (uintptr_t)OS_ThreadLaunch);

  hook_arm64(so_find_addr(&game_mod, "_Z17OS_ScreenGetWidthv"), (uintptr_t)OS_ScreenGetWidth);
  hook_arm64(so_find_addr(&game_mod, "_Z18OS_ScreenGetHeightv"), (uintptr_t)OS_ScreenGetHeight);

  // hook detail level getters to our own settings
  hook_arm64(so_find_addr(&game_mod, "_ZN13X_DetailLevel19getCharacterShadowsEv"), (uintptr_t)X_DetailLevel_getCharacterShadows);
  hook_arm64(so_find_addr(&game_mod, "_ZN13X_DetailLevel34getDebrisProjectileLimitMultiplierEv"), (uintptr_t)X_DetailLevel_getDebrisProjectileLimitMultiplier);
  hook_arm64(so_find_addr(&game_mod, "_ZN13X_DetailLevel23getDecalLimitMultiplierEv"), (uintptr_t)X_DetailLevel_getDecalLimitMultiplier);
  hook_arm64(so_find_addr(&game_mod, "_ZN13X_DetailLevel13dropHighesLODEv"), (uintptr_t)X_DetailLevel_getDropHighestLOD);

  // UseBloom() gates the whole post-process pipeline. Stock 2.1.131
  // hardcodes it to "return 1" (pipeline always on) and the in-game
  // "Enhanced Contrast" toggle (Get/SetPostProcessToggle) only picks the
  // contrast math in the composite pass. Forcing it to 0 makes the toggle
  // render the scene into bloom buffers that were never created -> black
  // screen with only the HUD visible.
  //
  // use_bloom=1: keep stock behavior (pipeline always on, like android).
  // use_bloom=0 (default): tie the pipeline to the in-game toggle, so it
  // only costs anything when Enhanced Contrast is actually enabled.
  // PostProcessTick polls UseBloom() every frame and creates/deletes the
  // buffers on a state change, so redirecting the getter is enough.
  //
  // NOTE: UseBloom is only 8 bytes and is immediately followed by
  // PostProcessTick, so a normal 16-byte hook would corrupt the next
  // function; a single branch instruction fits.
  if (!config.use_bloom) {
    uint32_t *use_bloom = (uint32_t *)so_find_addr(&game_mod, "_Z8UseBloomv");
    const uint32_t *get_toggle = (uint32_t *)so_find_addr(&game_mod, "_Z20GetPostProcessTogglev");
    const int64_t off = (get_toggle - use_bloom); // in instructions
    use_bloom[0] = 0x14000000u | ((uint32_t)off & 0x3FFFFFFu); // b GetPostProcessToggle
  }

  // dummy out the weapon menu arrow drawer if it's disabled
  if (!config.show_weapon_menu)
    hook_arm64(so_find_addr(&game_mod, "_ZN12WeaponSwiper4DrawEv"), (uintptr_t)ret0);

  // crouch toggle
  if (config.crouch_toggle) {
    sm_control = (void *)so_find_addr_rx(&game_mod, "_ZN24MaxPayne_ConfiguredInput10sm_controlE");
    MaxPayne_InputControl_getButton = (void *)so_find_addr_rx(&game_mod, "_ZNK21MaxPayne_InputControl9getButtonEi");
    hook_arm64(so_find_addr(&game_mod, "_ZNK24MaxPayne_ConfiguredInput10readCrouchEv"), (uintptr_t)MaxPayne_ConfiguredInput_readCrouch);
  }

  // NOTE: the mod file (priority archive) feature from 1.7 is gone for now:
  // R_File::setFileSystemRoot / enablePriorityArchive don't exist in 2.1.131

  // HACK: THIS IS POSSIBLY VERY BAD
  // the game uses some sort of a stack guard mechanism that reads an offset from TPIDR_EL0,
  // reads a cookie value from that offset and then uses it to check stack frame integrity
  // however on the Switch TPIDR_EL0 seems to just return 0 (armGetTls() uses TPIDRRO_EL0)
  // I don't know whether this will cause any issues or not, but we just write a pointer to
  // a static buffer to TPIDR_EL0 and let the game use that
  init_fake_tls(main_fake_tls);

  // RGB565 FBOs suck
  *(uint8_t *)so_find_addr(&game_mod, "UseRGBA8") = 1;
  // hide the touch joystick overlay
  *(uint8_t *)so_find_addr(&game_mod, "showJoysticks") = 0;
}
