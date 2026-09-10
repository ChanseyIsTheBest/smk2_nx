/* ninjakiwi.h -- the Java "shell" classes Bloons Supermonkey's engine calls up
 * into, reimplemented natively.
 *
 * jni_fake.c funnels every Call*Method[V] here through nk_upcall().
 *
 * MIT license -- see LICENSE.
 */

#ifndef __NINJAKIWI_H__
#define __NINJAKIWI_H__

#include <stdarg.h>
#include "jni.h"

jvalue nk_upcall(const char *cls, const char *name, const char *sig,
                 jobject self, va_list ap);

/* Software keyboard request, raised from MainActivity.ShowKeyboard. */
void nk_request_keyboard(jobject prompt);

#endif
