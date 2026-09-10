/* main.c -- Bloons Supermonkey Switch port: loader + lifecycle + input + frame
 * loop.
 *
 * The lifecycle below mirrors the callback set the engine registers via
 * RegisterNatives, extracted from libnative.so by tools/gen_natives.py and
 * frozen in nk_natives.h. Signatures are ground truth, not guesses:
 *
 *   load:     nativeLoad -> nativeSurfaceCreated -> nativeResize -> nativeResume
 *   frame:    nativeTick   (this engine updates AND renders, then presents)
 *   input:    nativeTouchStarted / nativeTouchHeld / nativeTouchEnded /
 *             nativeTouchCancelled ; nativeBackPressed ; nativeInputKey{Down,Up}
 *   suspend:  nativePause / nativeResume ; nativeLost/GainedAudioFocus
 *   teardown: nativeSurfaceDestroyed -> nativeUnload
 *
 * DIFFERENCES FROM THE BTD5 DONOR -- these are the things that will bite:
 *
 *   1. nativeBackPressed()V EXISTS here. BTD5 had no such callback and routed
 *      Back purely as AKEYCODE_BACK through nativeInputKeyDown. Supermonkey
 *      registers both, so we drive the dedicated one and keep the key path for
 *      anything that reads raw keys.
 *   2. There is a GDPR CONSENT FLOW (Usercentrics). The engine calls up into
 *      NKUserCentrics.configureOptions / showConsentUI and then WAITS for
 *      nativeConsentReadySuccess + nativeConsentCollectedSuccess to come back
 *      down. Nothing in BTD5 corresponds to this. nk_bootstrap.c answers it.
 *   3. The store must be told it initialised (_native_initCallback(Z)) or the
 *      engine sits waiting on billing setup.
 *   4. nativeGetPublicKey()Ljava/lang/String; is a native the ENGINE provides
 *      (Java calls down for the LVL key), not one we call.
 *
 * MIT license -- see LICENSE.
 */

#include <switch.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "config.h"
#include "nx_data_root.h"
#include "so_util.h"
#include "jni_fake.h"
#include "ninjakiwi.h"
#include "nk_bootstrap.h"
#include "opensles.h"
#include "util.h"
#include "error.h"
#include "platform.h"
#include "imports.h"
#include "libc_shim.h"   /* set_asset_base */
#include "music.h"       /* music_shutdown */
#include "nx_portrait.h"
#include "smk_savetool.h"

/* What the ENGINE renders into: portrait, because Supermonkey is portrait-only. */
int screen_width  = GAME_WIDTH;    /* 1080 */
int screen_height = GAME_HEIGHT;   /* 1920 */

/* The Switch's landscape panel. In the default `fit` mode the window is this
 * size and the portrait image is scaled into a pillarboxed rect inside it. */
int display_width  = 1920;
int display_height = 1080;

Config config;

static so_module game_mod;

/* ---- resolved engine entry points ------------------------------------- *
 * Signatures from nk_natives.h. Note the argument ORDER on the touch calls:
 * (x, y, pointerId) with the floats FIRST, and nativeTouchHeld carries a
 * trailing 'moved' bool. Getting these backwards produces a game that boots,
 * renders, and ignores every tap -- which reads like a broken input layer
 * rather than a swapped argument, so it is worth stating plainly. */
typedef void (*fn_v)    (JNIEnv env, void *thiz);
typedef void (*fn_i)    (JNIEnv env, void *thiz, int a);
typedef void (*fn_wh)   (JNIEnv env, void *thiz, int w, int h);
typedef void (*fn_surf) (JNIEnv env, void *thiz, void *surface, int w, int h);
typedef void (*fn_load) (JNIEnv env, void *thiz, void *activity, void *obj);
typedef void (*fn_tch)  (JNIEnv env, void *thiz, float x, float y, int id);
typedef void (*fn_tchh) (JNIEnv env, void *thiz, float x, float y, int id,
                         unsigned char moved);
typedef void (*fn_key)  (JNIEnv env, void *thiz, int keycode, int meta);
typedef void (*fn_b)    (JNIEnv env, void *thiz, unsigned char b);

static fn_load e_nativeLoad;
static fn_v    e_nativeUnload;
static fn_surf e_nativeSurfaceCreated;
static fn_v    e_nativeSurfaceDestroyed;
static fn_wh   e_nativeResize;
static fn_v    e_nativeTick;
static fn_v    e_nativePause, e_nativeResume;
static fn_tch  e_nativeTouchStarted, e_nativeTouchEnded, e_nativeTouchCancelled;
static fn_tchh e_nativeTouchHeld;
static fn_key  e_nativeInputKeyDown, e_nativeInputKeyUp;
static fn_v    e_nativeBackPressed;
static fn_v    e_nativeGainedAudioFocus;
static fn_b    e_nativeLostAudioFocus;
static fn_i    e_nativeOrientationChanged;
static fn_i    e_nativeLowMemory;

#define AKEYCODE_BACK 4

/* fake_env / fake_vm are the globals jni_init() sets up (jni_fake.c). We hand
 * fake_env to every engine callback, exactly as ART would hand a real one. */
extern JNIEnv fake_env;
extern JavaVM fake_vm;
static void *thiz;   /* stand-in MainActivity the engine holds a global ref to */

static void resolve_entry_points(void)
{
  /* Resolve by the name the engine passed to RegisterNatives, captured in
   * jf_RegisterNatives. This binds obfuscated and static callbacks uniformly,
   * and is how Android binds them too. */
  #define BIND(v, name) v = (void *)jni_registered(name)

  BIND(e_nativeLoad,              "nativeLoad");
  BIND(e_nativeSurfaceCreated,    "nativeSurfaceCreated");
  BIND(e_nativeResize,            "nativeResize");
  BIND(e_nativeTick,              "nativeTick");
  BIND(e_nativeUnload,            "nativeUnload");
  BIND(e_nativeSurfaceDestroyed,  "nativeSurfaceDestroyed");
  BIND(e_nativePause,             "nativePause");
  BIND(e_nativeResume,            "nativeResume");
  BIND(e_nativeTouchStarted,      "nativeTouchStarted");
  BIND(e_nativeTouchHeld,         "nativeTouchHeld");
  BIND(e_nativeTouchEnded,        "nativeTouchEnded");
  BIND(e_nativeTouchCancelled,    "nativeTouchCancelled");
  BIND(e_nativeInputKeyDown,      "nativeInputKeyDown");
  BIND(e_nativeInputKeyUp,        "nativeInputKeyUp");
  BIND(e_nativeBackPressed,       "nativeBackPressed");
  BIND(e_nativeGainedAudioFocus,  "nativeGainedAudioFocus");
  BIND(e_nativeLostAudioFocus,    "nativeLostAudioFocus");
  BIND(e_nativeOrientationChanged,"nativeOrientationChanged");
  BIND(e_nativeLowMemory,         "nativeLowMemory");

  /* nk_bootstrap owns the async result callbacks (consent, store, license,
   * ads) -- it resolves them itself from the same registry. */
  nk_bootstrap_resolve();

  /* NOTE: the natives audit is deliberately NOT run here. At this point only
   * JNI_OnLoad has happened, which registers MainActivity's 24 natives; the
   * other 41 -- LicenseChecker, Store, PlayServices, NKUserCentrics -- are
   * registered later, from inside nativeSurfaceCreated. Auditing here printed
   * 41 false "MISSING" lines on the first on-device run. nk_bootstrap_pump()
   * runs it once the set has settled instead. */

  if (!e_nativeTick || !e_nativeLoad)
    fatal_error("Could not resolve the engine entry points.\n\n"
                "nativeLoad and/or nativeTick were never registered. Your "
                "libnative.so is probably from a different build of the game "
                "-- check the RegisterNatives trace in smk_nx.log against "
                "source/nk_natives.h.");
}

/* ---- input: platform pointer events -> Supermonkey touch callbacks ------ *
 * nx_pointer.c unifies the handheld touchscreen, a USB mouse, gyro and the
 * stick-driven virtual cursor into one PtrEvent stream. Each maps to one
 * touch callback. */
static void pump_input(void)
{
  PtrEvent ev[16];
  int n = platform_poll_pointers(ev, 16);
  for (int i = 0; i < n; i++) {
    float x = ev[i].x, y = ev[i].y; int id = ev[i].id;
    switch (ev[i].phase) {
      case PTR_DOWN: if (e_nativeTouchStarted) e_nativeTouchStarted(fake_env, thiz, x, y, id); break;
      case PTR_MOVE: if (e_nativeTouchHeld)    e_nativeTouchHeld(fake_env, thiz, x, y, id, 1); break;
      case PTR_UP:   if (e_nativeTouchEnded)   e_nativeTouchEnded(fake_env, thiz, x, y, id);   break;
    }
  }

  /* Back: Supermonkey registers a dedicated nativeBackPressed()V, unlike BTD5.
   * Drive it on the press edge, and ALSO emit the raw key pair, because some
   * screens in this engine read AKEYCODE_BACK through the key path instead. */
  if (back_edge_pressed()) {
    if (e_nativeBackPressed)  e_nativeBackPressed(fake_env, thiz);
    if (e_nativeInputKeyDown) e_nativeInputKeyDown(fake_env, thiz, AKEYCODE_BACK, 0);
  }
  if (back_edge_released() && e_nativeInputKeyUp)
    e_nativeInputKeyUp(fake_env, thiz, AKEYCODE_BACK, 0);
}

/* Startup breadcrumbs. A crash that leaves no clean backtrace still tells us
 * how far we got. debugPrintf already goes to smk_nx.log; no second unlocked
 * fd-table user on the main thread (that raced the engine's asset opens on
 * worker threads in the BTD5 bring-up and cost a day). */
static void stage(const char *s) { debugPrintf(">>> STAGE %s\n", s); }

static unsigned long long g_frame_count = 0;

int main(int argc, char *argv[])
{
  /* MUST be first: everything else, including the log path, hangs off this. */
  nx_resolve_data_root(argc, argv);

  stage("0 enter main");
  /* Create the resolved root and any missing parents. NOT a hardcoded
   * "sdmc:/switch" -- the game can live anywhere on the card, and creating a
   * folder the port does not use is both untidy and misleading when someone
   * goes looking for their files. In the normal case every component already
   * exists (libnative.so was found inside it), so this is a no-op; it matters
   * only when the resolver fell back to the default and nothing is there. */
  nx_make_root_dirs();

  /* Surface the previous run's crash dump into the main log, if there is one.
   *
   * NOTE: the user exception handler in nx_crash_handler.c is deliberately
   * DISABLED (nx_exc_stack_size_DISABLED = 0). With no handler installed, a CPU
   * fault propagates to the kernel and Atmosphere writes a full native crash
   * report showing the ORIGINAL faulting pc/lr/registers -- whereas our own
   * handler captured its own svcBreak state instead, and never wrote to the SD
   * card reliably. So smk_crash.log will normally be EMPTY, and the real report
   * lives in sdmc:/atmosphere/crash_reports/. The file is still opened and
   * replayed here because re-enabling the handler is a one-line change, and
   * when it is enabled this is where its output appears. */
  {
    FILE *pf = fopen(nx_path("/smk_crash.log"), "rb");
    if (pf) {
      char cb[256]; size_t cn; int any = 0;
      while ((cn = fread(cb, 1, sizeof cb - 1, pf)) > 0) {
        if (!any) { debugPrintf("=== PREVIOUS RUN smk_crash.log ===\n"); any = 1; }
        cb[cn] = 0; debugPrintf("%s", cb);
      }
      if (any) debugPrintf("\n=== END PREVIOUS CRASH ===\n");
      fclose(pf);
    }
  }
  extern void crash_log_open(void);
  crash_log_open();

  debugPrintf("crash reports: sdmc:/atmosphere/crash_reports/ "
              "(our own handler is disabled on purpose -- see nx_crash_handler.c)\n");

  debugPrintf("data root: %s\n", g_data_root);
  debugPrintf("  how:     %s\n", g_data_root_how);
  debugPrintf("  assets:  %s\n",
              nx_have_assets() ? "found " JET_ARCHIVE
                               : "MISSING -- " JET_ARCHIVE " not found (see README)");

  /* No CPU boost.
   *
   * appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad) used to be called here to
   * speed up the initial load. On hardware it did not help, so it is gone.
   *
   * FastLoad is not a free win: it raises the CPU clock and LOWERS the GPU
   * clock, on the assumption that a loading screen is CPU-bound. This engine's
   * load is dominated by asset decode and GL uploads, so that trade is at best
   * neutral and can go the wrong way. Removed rather than left switched off, so
   * nobody re-enables it expecting an improvement. */
  tls_setup_guard();
  pin_current_thread();

  /* Bloons Supermonkey is PORTRAIT-ONLY: 1080x1920 (9:16). The engine renders
   * at exactly that size into a window of exactly that size, and the compositor
   * rotates the result onto the landscape panel (see nx_portrait.c). Nothing is
   * scaled and the engine is never told anything but its own native geometry.
   *
   * There is no dock/undock path: the compositor handles both output sizes off
   * the same buffer, which is why handle_dock_change() always reports "no
   * change". */
  screen_width  = GAME_WIDTH;
  screen_height = GAME_HEIGHT;

  read_config(nx_path("/config.txt"));   /* writes defaults on first run */

  /* Decide the rotation BEFORE anything creates the native window: the window's
   * dimensions and transform are set in ANativeWindow_fromSurface_fake, which
   * the engine calls during nativeSurfaceCreated. Pure config, no GL. */
  nxpt_configure(screen_width, screen_height,
                 display_width, display_height, (NxptMode)config.portrait);
  set_asset_base(g_data_root);

  /* Apply saves.txt before the engine has read anything. On first run this
   * writes a commented template and does nothing else. Placed here on purpose:
   * after the data root is known, and well before so_load(), so the engine
   * never sees a save mid-edit. */
  smk_savetool_apply();

  stage("1 config read");

  /* 1. map + relocate + resolve the game library against our shim table */
  stage("2 so_load: heap alloc + open libnative.so");
  if (so_load(&game_mod, nx_path("/" SO_NAME), heap_so_base(), heap_so_limit()) < 0)
    fatal_error("Couldn't load " SO_NAME ".\n\n"
                "Put it -- and the Assets/ folder -- from YOUR OWN copy of "
                "Bloons Supermonkey next to this .nro.\n\n"
                "Looked in: %s", g_data_root);
  stage("3 so_relocate");
  so_relocate(&game_mod);
  stage("4 so_resolve");
  so_resolve(&game_mod, dynlib_functions, dynlib_numfunctions, 1);

  /* CRITICAL: map the image executable (svcMapProcessCodeMemory + Perm_Rx on
   * the code pages) and flush caches, or the first jump into the .so faults
   * with an instruction abort. */
  stage("4b so_finalize (map code RX)");
  so_finalize(&game_mod);
  so_flush_caches(&game_mod);

  /* 2. GLES2/EGL context. Audio needs no init here: the engine creates its own
   *    OpenSL ES device through the imported slCreateEngine (imports.c). */
  stage("5 egl_init_context");
  egl_init_context();                /* 1080p docked / 720p handheld */

  /* 3. run the engine's C++ constructors, then set up the fake JNIEnv/JavaVM
   *    and call JNI_OnLoad so it does its RegisterNatives. init_array MUST run
   *    first -- the globals JNI_OnLoad touches are constructed there. */
  stage("6a so_execute_init_array");
  so_execute_init_array(&game_mod);
  stage("6b jni_init + JNI_OnLoad");
  jni_init();
  thiz = jni_make_activity();
  nk_set_activity(thiz);
  jint (*jni_onload)(JavaVM vm, void *reserved) =
      (void *)so_find_addr_rx(&game_mod, "JNI_OnLoad");
  if (jni_onload) jni_onload(fake_vm, NULL);
  stage("7 resolve_entry_points");
  resolve_entry_points();

  /* 4. lifecycle: load -> surface -> resize -> resume.
   *    nativeLoad(activity, assetManager): arg3 is NewGlobalRef'd and kept as
   *    the MainActivity receiver; arg4 goes to AAssetManager_fromJava. */
  void *surface  = jni_make_surface();     /* android.view.Surface stand-in  */
  void *assetmgr = jni_make_object();      /* AssetManager stand-in          */
  stage("9 nativeLoad");
  e_nativeLoad(fake_env, thiz, thiz, assetmgr);
  stage("10 nativeSurfaceCreated");
  if (e_nativeSurfaceCreated)
    e_nativeSurfaceCreated(fake_env, thiz, surface, screen_width, screen_height);
  stage("11 nativeResize");
  e_nativeResize(fake_env, thiz, screen_width, screen_height);

  /* Portrait. For a portrait-native app, ROTATION_0 IS portrait -- Android
   * reports rotation relative to the device's natural orientation, and for a
   * phone that is portrait. So 0 is correct here and must not be "corrected" to
   * 90 just because the Switch panel is landscape: the engine keys its layout
   * off the value it is given, and telling it 90 would make it lay out for
   * landscape inside our portrait surface. */
  if (e_nativeOrientationChanged) e_nativeOrientationChanged(fake_env, thiz, 0);

  if (e_nativeResume) e_nativeResume(fake_env, thiz);
  if (e_nativeGainedAudioFocus) e_nativeGainedAudioFocus(fake_env, thiz);

  /* 5. Answer everything the engine is now blocking on: GDPR consent, store
   *    init, licence check. On Android these arrive asynchronously from Java
   *    a moment after load; here we deliver them on the same schedule (a few
   *    frames in) rather than instantly, because the engine installs the
   *    handlers during its first ticks and a callback that lands before its
   *    handler exists is dropped silently. See nk_bootstrap.c. */
  stage("12 entering frame loop");

  int first_tick = 1;
  const u64 t0 = armGetSystemTick();

  while (appletMainLoop()) {
    if (first_tick) stage("12a before padUpdate");
    padUpdate_all();
    if (first_tick) stage("12b after padUpdate");
    if (should_quit() || jni_quit_requested) break;

    if (handle_dock_change(&screen_width, &screen_height)) {
      stage("13 dock nativeResize");
      e_nativeResize(fake_env, thiz, screen_width, screen_height);
    }

    /* Don't feed input before the engine has rendered once: on Android, touch
     * and key events only arrive after the first frame, and the input handlers
     * dereference state that the first nativeTick sets up. A touch on frame 0
     * faults. */
    if (!first_tick) pump_input();

    /* Deliver the async platform answers on their scheduled frames. */
    nk_bootstrap_pump(g_frame_count);

    if (first_tick) stage("14 first nativeTick");
    e_nativeTick(fake_env, thiz);    /* engine updates, renders and presents */
    if (first_tick) { stage("15 running"); first_tick = 0; }

    /* Heartbeat: the loop is silent after stage 15, so without this a log
     * ending at "15 running" cannot distinguish a healthy 60fps game from a
     * hang on frame 2. */
    if ((++g_frame_count % 60) == 0) {
      const u64 now = armGetSystemTick();
      const double secs = (double)(now - t0) / 19200000.0;   /* NX tick 19.2MHz */
      debugPrintf("[hb] frame %llu  t=%.1fs  fps=%.1f\n",
                  (unsigned long long)g_frame_count, secs,
                  secs > 0.0 ? (double)g_frame_count / secs : 0.0);
      debugLogFlush();
    }

    egl_swap_buffers();              /* no-op: the engine presents itself */
  }

  /* 6. teardown */
  if (e_nativeLostAudioFocus) e_nativeLostAudioFocus(fake_env, thiz, 1);
  if (e_nativePause) e_nativePause(fake_env, thiz);
  if (e_nativeSurfaceDestroyed) e_nativeSurfaceDestroyed(fake_env, thiz);
  if (e_nativeUnload) e_nativeUnload(fake_env, thiz);
  opensles_shutdown();
  music_shutdown();
  write_config(nx_path("/config.txt"));
  debugLogFlush();
  egl_exit_context();
  return 0;
}

/* Called by ninjakiwi.c when the engine asks the activity to report low
 * memory (it does this from its own allocator pressure path, not only from
 * Android's onLowMemory). Level 80 == TRIM_MEMORY_COMPLETE. */
void nk_report_low_memory(void) {
  if (e_nativeLowMemory) e_nativeLowMemory(fake_env, thiz, 80);
}
