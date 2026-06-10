/* jni_fake.h -- fake JNI environment for the 2.1.131 oswrapper layer
 *
 * The new Max Payne build is driven from Java through JNI: the game calls
 * back into a GamePlatformServices object for movies, music, vibration,
 * splash screens, HTTP and app control, and reads device properties from a
 * DeviceInfo object. We emulate just enough of a JavaVM/JNIEnv for those
 * calls to resolve and no-op (or map to Switch equivalents).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __JNI_FAKE_H__
#define __JNI_FAKE_H__

#include <stdint.h>

// passed to JNI_OnLoad / NVThreadInit and to every impl* entry point
extern void *fake_vm;  // JavaVM *
extern void *fake_env; // JNIEnv *

// set when the game calls GamePlatformServices.quit()
extern volatile int jni_quit_requested;

void jni_init(void);

typedef enum {
  JNI_RS_INITIAL_COMPLETE = 1,
  JNI_RS_SIGN_IN_COMPLETE,
  JNI_RS_SIGN_OUT_COMPLETE,
  JNI_RS_CLOUD_DISABLED_COMPLETE,
  JNI_RS_DELETE_ACCOUNT_COMPLETE,
  JNI_RS_GATE_COMPLETE,
  JNI_RS_STATE_CHANGED,
  JNI_RS_ID_CHANGED,
  JNI_RS_TICKET_CHANGED,
  JNI_RS_SETUP,
} JniRockstarEventType;

typedef struct {
  JniRockstarEventType type;
  int gate;
  int accepted;
  int signed_in;
  void *value;
  void *environment;
  void *title_id;
} JniRockstarEvent;

// constructors for fake Java objects to pass into the game's JNI entry points
void *jni_make_string(const char *utf);
void *jni_make_string_array(int n, const char **strs);
void *jni_make_int_array(int n, const int *vals);
void *jni_make_object(const char *label);

// http requests the game queued via the services object; the main loop
// should fail them with implOnHttpRequestError. returns -1 when empty.
int jni_pop_pending_http(void);

// Rockstar service completions queued by fake Java callbacks. returns 0 when empty.
int jni_pop_pending_rockstar(JniRockstarEvent *event);

#endif
