/* jni_fake.h -- fake JNI environment for the Ninja Kiwi engine (libnative.so)
 *
 * The engine is native C++ (GLES2 renderer, OpenSL ES audio, its own asset
 * pipeline) with a thin Java shell it calls back into via JNI: MainActivity
 * (device, storage, display, music, keyboard), Store/GoogleStore (IAP),
 * LicenseChecker, IronSourceInterface (ads), PlayServicesInterface,
 * NKUserCentrics (GDPR consent) and a handful of analytics stubs. We recreate
 * just enough JNI for the engine to (a) run JNI_OnLoad, (b) FindClass /
 * GetMethodID the shell classes, and (c) call them -- routing the calls that
 * matter into ninjakiwi.c and logging the rest.
 *
 * This software may be modified and distributed under the terms of the MIT
 * license. See the LICENSE file for details.
 */

#ifndef __JNI_FAKE_H__
#define __JNI_FAKE_H__

#include <stdint.h>
#include "jni.h"

extern JavaVM fake_vm;   // JavaVM* handed to JNI_OnLoad
extern JNIEnv fake_env;  // JNIEnv* handed to every entry point

// Set when the engine (via TextInput) asks for the soft keyboard; main.c
// services it with the Switch software keyboard.
extern volatile int g_kbd_requested;
extern char g_kbd_initial[64];   // current field text, if the engine provided it

// Set if the engine ever asks the activity to finish.
extern volatile int jni_quit_requested;

void jni_init(void);

// the fake activity instance / class handed to the JNI entry points
void *jni_make_thiz(void);

// fake Java object constructors used by main.c when driving the lifecycle
jstring jni_make_string(const char *utf);
const char *jni_cstr(jobject s);   /* C string from a fake jstring, or NULL */
jobject jni_make_object(void);
jobject jni_make_activity(void);
jobject jni_make_surface(void);

// Resolve a native callback by the name the engine passed to RegisterNatives
// (populated during JNI_OnLoad). Handles obfuscated/static symbols uniformly.
void *jni_registered(const char *name);
jclass  jni_find_class_c(const char *name);   // same as env FindClass, for C use

// --- audio: wrap a raw PCM buffer as a jbyteArray for nativeMixData ---
// The engine fills it via GetByteArrayElements/ReleaseByteArrayElements.
jbyteArray jni_wrap_bytearray(void *data, int len_bytes);
void       jni_free_wrapper(jobject o);

#endif
