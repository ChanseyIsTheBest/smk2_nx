/* obb.c -- inert OBB reader. Bloons Supermonkey ships no expansion file, and
 * needs no archive reader at all.
 *
 * That is worth stating, because config.h names JET_ARCHIVE = "Assets/data.jet"
 * and it would be reasonable to assume something has to unpack it. It does not.
 * data.jet is 1.85 MB against ~78 MB of loose assets -- it is an index, not a
 * bulk container, and the engine opens it through AAssetManager_open like any
 * other file. Our AAsset emulation resolves that to <root>/Assets/data.jet on
 * disk and reads it directly. The 1434 model files, 239 textures and 137 audio
 * files it points at are all loose on the card already.
 *
 * So every entry point below reports "no archive / empty" and the AAsset path
 * falls through to loose files, which is the correct behaviour here rather
 * than a stub awaiting implementation.
 *
 * This software may be modified and distributed under the terms of the MIT
 * license. See the LICENSE file for details.
 */

#include <stddef.h>
#include "obb.h"

int obb_open(const char *path) { (void)path; return -1; }

void obb_close(void) {}

int obb_exists(const char *name) { (void)name; return 0; }

void *obb_read(const char *name, size_t *out_size) {
  (void)name;
  if (out_size) *out_size = 0;
  return NULL; // fall through to loose-file lookups
}
