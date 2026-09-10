/* config.h -- build-time constants for the Bloons Supermonkey Switch port
 *
 * Bloons Supermonkey (com.ninjakiwi.supermonkey) runs on the SAME in-house
 * Ninja Kiwi C++ engine as Bloons TD 5 -- class prefixes CSuperMonkey /
 * CSuperMonkeyModel / CBloonsManager, source paths under
 * Game/Objects/Game/InGameObjects/. Established by static analysis of
 * lib/arm64-v8a/libnative.so (see tools/ and JNI_MAP.md):
 *
 *   - App model:  Java GLSurfaceView + JNI (NOT a NativeActivity). The Java
 *                 shell (com/ninjakiwi/MainActivity) creates the GL surface and
 *                 drives the engine through nativeLoad / nativeSurfaceCreated /
 *                 nativeResize / nativeTick / nativeTouch*, all bound via
 *                 RegisterNatives inside JNI_OnLoad. 65 natives, 11 tables.
 *   - Renderer:   GLES2 + EGL (NEEDED: libGLESv2, libEGL).
 *   - Audio:      OpenSL ES (NEEDED: libOpenSLES) -> emulated in opensles.c.
 *                 Music is a separate path: MainActivity.loadMusic() hands back
 *                 a CustomMediaPlayer, driven here by music.c (minimp3+SDL).
 *   - Assets:     Android AssetManager (AAssetManager_open) over a packed
 *                 archive "Assets/data.jet" plus loose Assets/Models,
 *                 Assets/Textures, Assets/Audio, Assets/JSON, Assets/GameData,
 *                 Assets/Shaders, Assets/Fonts, Assets/Lab trees.
 *   - C++ runtime is STATICALLY linked (only __cxa_atexit / __cxa_finalize are
 *                 external), so there is NO libc++_shared.so to ship.
 *   - Imports:    380 symbols. 370 are already covered by the BTD5 shim table
 *                 this port inherits; the 10 new ones are marked
 *                 "Supermonkey delta" in imports.c.
 *   - Network / ads / IAP / licensing / GDPR consent (IronSource, Google Play
 *                 Billing + Licensing, Usercentrics, Adjust, Crashlytics) are
 *                 stubbed to a fail-safe offline shape -- see ninjakiwi.c and
 *                 nk_bootstrap.c. You must own the game; supply libnative.so
 *                 and the Assets/ tree from YOUR OWN copy.
 *
 * You do NOT need libd4df6a.so or any libcrashlytics*.so from the APK: the
 * first exports only JNI_OnLoad and is loaded by Java, never by the engine,
 * and the Crashlytics libraries are never referenced by libnative.so.
 *
 * MIT license -- see LICENSE.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// The single native module from the APK's lib/arm64-v8a/.
#define SO_NAME  "libnative.so"

/* Compile-time DEFAULT only. The real root is resolved at runtime by
 * nx_data_root.c, which finds whichever folder actually holds libnative.so.
 * Never build a path from this directly -- use nx_path("/thing"). */
#define GAME_FOLDER "supermonkey"
#define DATA_DIR    "sdmc:/switch/" GAME_FOLDER

/* Address-space split (see __libnx_initheap in platform.c). Supermonkey's
 * libnative.so is ~16 MB of code+data -- a third larger than BTD5's ~12 MB --
 * so the reservation is raised to match, with headroom for relocation
 * scratch. The rest goes to newlib's heap (textures, audio, the parsed
 * jet/JSON game state). */
#define SO_ZONE_MB 200

// Logging. On during bring-up; off for release -- it costs real CPU and
// writes to the SD card every frame. Set to 1 if you need a boot log.
#define DEBUG_LOG 0

/* Upper bound on a blocking condvar/futex wait, in ms.  0 = NO CAP.
 *
 *   0   -> real, indefinite blocking waits. Idle engine threads sleep until
 *          they are actually signalled and use ZERO cpu. This is how a normal
 *          pthread implementation behaves, and it is what we want.
 *   >0  -> every wait wakes at least this often, whether signalled or not.
 *
 * BTD5 shipped this at 0 after finding the bug it was masking: newlib's
 * ETIMEDOUT (116) was being returned where the engine's bundled boost expected
 * bionic's (110), so every timed wait threw an uncaught exception. That fix is
 * inherited here (os_shims.c). If Supermonkey ever hangs on a lost wakeup, put
 * 100 back here -- it restores the safety net with no other change. */
#define COND_WAIT_CAP_MS 0

/* Pin every thread (main + engine workers + audio) to one core. Does not remove
 * races, but removes true parallelism, so two threads can no longer be inside
 * the same critical section on two CPUs at once. Diagnostic:
 *   crashes STOP with this on  -> it is a data race
 *   crashes persist unchanged  -> it is not; look elsewhere
 * Costs performance. Off by default. */
#define SINGLE_CORE 0
#define SINGLE_CORE_ID 0

/* Asset lookup roots, tried in order under the game dir. On Android,
 * AAssetManager_open("Assets/x") means assets/Assets/x, so "assets" comes
 * first -- that is where an unpacked APK puts them. Supermonkey's engine asks
 * for paths that already begin with "Assets/", so "." also resolves them if
 * you drop the Assets/ folder straight next to the .nro. */
#define ASSET_ROOTS { ".", "assets", "romfs:" }

/* Packed archive the engine memory-maps first; loose files override it.
 * BTD5 called this Assets/BTD5.jet; Supermonkey's is Assets/data.jet. */
#define JET_ARCHIVE "Assets/data.jet"

/* Audio format handed to the engine when it asks MainActivity for the device's
 * native parameters (getNativeAudioSampleRate / getNativeAudioFramesPerBuffer).
 * Returning 0 here makes the engine compute a zero-length mix buffer and
 * divide by it, so these must be real values. 48 kHz is Horizon's native rate;
 * 256 frames is a 5.3 ms period, which OpenSL-over-SDL services comfortably. */
#define NATIVE_AUDIO_RATE           48000
#define NATIVE_AUDIO_FRAMES_PER_BUF 256

/* The surface the ENGINE renders into. Bloons Supermonkey is portrait-only, so
 * this is 1080x1920 (9:16), not the Switch's landscape panel size. Everything
 * the engine sees -- its viewport, its UI layout, its touch coordinates -- is
 * in this space. */
extern int screen_width;
extern int screen_height;

/* The Switch's landscape panel, which the portrait image is fitted into. Keep
 * this distinct from screen_width/height -- conflating the two is what makes a
 * portrait port stretch. */
extern int display_width;
extern int display_height;

/* Default for the `portrait` setting in config.txt. The value written there is
 * used directly as an NxptMode, so these numbers must stay in step with the
 * enum in nx_portrait.h:
 *   0 = no rotation, upright, 16:9 pillar
 *   1 = rotate clockwise, right Joy-Con up   <- default
 *   2 = rotate anticlockwise, left Joy-Con up */
#define PORTRAIT_DEFAULT 1

/* The engine's native portrait resolution. */
#define GAME_WIDTH  1080
#define GAME_HEIGHT 1920

typedef struct {
  int screen_width;    // 0 = automatic (per dock state)
  int screen_height;   // 0 = automatic
  int docked_width;    // default 1920
  int docked_height;   // default 1080
  char language[8];    // "auto" or 2-letter code
  int  portrait;       // 0 no rotation, 1 rotate CW (default), 2 rotate CCW
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
