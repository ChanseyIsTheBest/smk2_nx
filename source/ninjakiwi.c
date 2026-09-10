/* ninjakiwi.c -- the Java "shell" classes Bloons Supermonkey's engine calls up
 * into.
 *
 * On Android these are real Java classes in classes*.dex. Here we reimplement,
 * natively, just enough of each for the engine to boot and run offline.
 *
 * The method set below is not guessed and not inherited from the BTD5 donor:
 * it was parsed out of YOUR classes1-5.dex by tools/dexmethods.py, which walks
 * the method_ids table and filters on the com/ninjakiwi package. That yielded
 * 769 methods across ~130 classes; MainActivity alone declares ~130 of them.
 * Everything answered here is a real method that really exists on the Java
 * side of this build. Regenerate the list with:
 *
 *     python3 tools/dexmethods.py ninjakiwi
 *
 * Entry point: nk_upcall(cls, name, sig, self, ap). We parse the Java
 * arguments out of `ap` according to `sig`, match on (class, name), and fill
 * the return jvalue.
 *
 * A NOTE ON CLASS MATCHING. The engine frequently caches a receiver as a bare
 * jobject and later calls methods on it with the class reported as
 * java/lang/Object. So the high-traffic getters are matched by method NAME
 * regardless of class, and only the genuinely ambiguous ones are class-scoped.
 * This is why the file reads as a long name ladder rather than a class switch.
 *
 * MIT license -- see LICENSE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <switch.h>

#include "jni_fake.h"
#include "ninjakiwi.h"
#include "nk_bootstrap.h"
#include "music.h"
#include "util.h"
#include "config.h"
#include "nx_data_root.h"

extern void nk_report_low_memory(void);

#define CLS(x)  (!strcmp(cls, (x)))
#define M(x)    (!strcmp(name, (x)))
#define HAS(x)  (strstr(cls, (x)) != NULL)
#define MHAS(x) (strstr(name, (x)) != NULL)

/* Pull Java args out of the va_list per the JNI signature. Object types become
 * a pointer; Z/B/C/S/I widen to int (JNI vararg promotion); J is jlong; F is
 * promoted to double in the vararg form; D is double. */
static int parse_args(const char *sig, va_list ap, jvalue *argv, int cap) {
  const char *p = sig;
  if (*p == '(') p++;
  int n = 0;
  while (*p && *p != ')' && n < cap) {
    switch (*p) {
      case 'Z': case 'B': case 'C': case 'S': case 'I':
        argv[n].i = va_arg(ap, int); p++; break;
      case 'J':
        argv[n].j = va_arg(ap, long long); p++; break;
      case 'F':
        argv[n].f = (float)va_arg(ap, double); p++; break;
      case 'D':
        argv[n].d = va_arg(ap, double); p++; break;
      case 'L':
        argv[n].l = va_arg(ap, void *);
        while (*p && *p != ';') p++;
        if (*p == ';') p++;
        break;
      case '[':
        argv[n].l = va_arg(ap, void *);
        p++;
        if (*p == 'L') { while (*p && *p != ';') p++; if (*p == ';') p++; }
        else if (*p) p++;
        break;
      default: p++; continue;
    }
    n++;
  }
  return n;
}

/* Tiny persistent key/value store backing MainActivity.storeKeyValuePair /
 * getValueFromKey, which on Android are SharedPreferences.
 *
 * This is NOT where the game saves. Bloons Supermonkey writes its own save
 * files directly through the C library -- local_bsm2.settings, TempProfile.save
 * and so on, straight into the port folder -- and never goes near these two
 * methods: the first on-device run logged ZERO calls to either across a full
 * boot into the title screen. (Keeping progress in a platform key/value bag is
 * a Unity/PlayerPrefs habit, not something this engine does.)
 *
 * The store is kept anyway because both methods really do exist on this build's
 * MainActivity, so some path can reach them. If nothing ever does, prefs.kv is
 * simply never created. */
#define MAX_KV 64
typedef struct { char k[96]; int v; } KV;
static KV  g_kv[MAX_KV];
static int g_nkv;
static int g_kv_loaded;
static Mutex g_kv_lock;

static void kv_load(void) {
  if (g_kv_loaded) return;
  g_kv_loaded = 1;
  mutexInit(&g_kv_lock);
  FILE *f = fopen(nx_path("/prefs.kv"), "r");
  if (!f) return;
  char line[160];
  while (g_nkv < MAX_KV && fgets(line, sizeof line, f)) {
    char *eq = strrchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    snprintf(g_kv[g_nkv].k, sizeof g_kv[g_nkv].k, "%s", line);
    g_kv[g_nkv].v = atoi(eq + 1);
    g_nkv++;
  }
  fclose(f);
}

static void kv_save(void) {
  FILE *f = fopen(nx_path("/prefs.kv"), "w");
  if (!f) return;
  for (int i = 0; i < g_nkv; i++) fprintf(f, "%s=%d\n", g_kv[i].k, g_kv[i].v);
  fclose(f);
}

static int kv_get(const char *k, int dflt) {
  kv_load();
  mutexLock(&g_kv_lock);
  int r = dflt;
  for (int i = 0; i < g_nkv; i++)
    if (!strcmp(g_kv[i].k, k)) { r = g_kv[i].v; break; }
  mutexUnlock(&g_kv_lock);
  return r;
}

static void kv_set(const char *k, int v) {
  kv_load();
  mutexLock(&g_kv_lock);
  for (int i = 0; i < g_nkv; i++)
    if (!strcmp(g_kv[i].k, k)) { g_kv[i].v = v; kv_save(); mutexUnlock(&g_kv_lock); return; }
  if (g_nkv < MAX_KV) {
    snprintf(g_kv[g_nkv].k, sizeof g_kv[g_nkv].k, "%s", k);
    g_kv[g_nkv].v = v;
    g_nkv++;
    kv_save();
  }
  mutexUnlock(&g_kv_lock);
}

jvalue nk_upcall(const char *cls, const char *name, const char *sig,
                 jobject self, va_list ap) {
  (void)self;
  jvalue r; r.j = 0;
  jvalue argv[8]; for (int i = 0; i < 8; i++) argv[i].j = 0;
  parse_args(sig, ap, argv, 8);

  /* ---- storage paths ---------------------------------------------------- *
   * All of MainActivity's path getters point at our writable game dir. Matched
   * by name because the receiver is often a cached bare jobject. The full set
   * is from the dex: getInternalStoragePath, getExternalStoragePath,
   * getCacheStoragePath, getExternalFilesPath, getExecutablePath. */
  if (M("getInternalStoragePath") || M("getExternalStoragePath") ||
      M("getCacheStoragePath")    || M("getExternalFilesPath")   ||
      M("getExecutablePath")      || M("getStorageDirectory")    ||
      M("getFilesDir")            || M("getSaveDirectory")) {
    r.l = jni_make_string(g_data_root);
    return r;
  }

  /* ---- identity / locale / device --------------------------------------- */
  if (M("getUniqueID") || M("getDeviceID") || M("getAdID") ||
      M("getAndroidID") || M("getInstallID") || M("getAppSetId")) {
    /* A stable, obviously-local id. Not a real advertising id: nothing here
     * talks to a network, and a plausible-looking one would only invite the
     * analytics paths to try. */
    r.l = jni_make_string("switch-supermonkey-local");
    return r;
  }
  if (M("getLanguageCode") || M("getDeviceLanguage") || M("getLanguage")) {
    r.l = jni_make_string((config.language[0] && strcmp(config.language, "auto"))
                          ? config.language : "en");
    return r;
  }
  if (M("getCountryCode") || M("getCountry")) { r.l = jni_make_string("US"); return r; }
  if (M("getDeviceModel")  || M("getModel"))  { r.l = jni_make_string("Nintendo Switch"); return r; }
  if (M("getBundleName"))  { r.l = jni_make_string("com.ninjakiwi.supermonkey"); return r; }
  if (M("getVersionName")) { r.l = jni_make_string("1.0"); return r; }
  if (M("getVersionNumber")) { r.i = 1; return r; }

  /* Physical screen size, in inches, for the engine's UI-scale buckets. The
   * Switch's built-in panel is 6.2"; docked output is a TV of unknown size, so
   * we keep reporting the handheld figure rather than inventing one. Returning
   * 0 here puts the engine in its "tiny phone" bucket and shrinks all the HUD
   * art. Note this is a DOUBLE, not a float -- ()D in the dex. */
  if (M("getScreenSizeInches")) { r.d = 6.2; return r; }

  /* Audio device parameters. These MUST be real: the engine sizes its OpenSL
   * mix buffer from them and divides by the frame count. See config.h. */
  if (M("getNativeAudioSampleRate"))      { r.i = NATIVE_AUDIO_RATE; return r; }
  if (M("getNativeAudioFramesPerBuffer")) { r.i = NATIVE_AUDIO_FRAMES_PER_BUF; return r; }

  /* Memory. Handheld Switch gives an application ~3.2 GB; report a
   * conservative 2 GB so the engine picks its mid texture budget rather than
   * its "desktop-class" one. */
  if (M("getTotalSystemMemoryInBytes")) { r.j = 2LL * 1024 * 1024 * 1024; return r; }
  if (M("getMemoryUsage")) { r.i = 0; return r; }
  if (M("onLowMemory")) { nk_report_low_memory(); return r; }

  if (M("getDeviceBootTime") || M("getBootTime") || M("getElapsedRealtime")) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    r.j = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return r;
  }

  /* ---- connectivity: standalone offline build --------------------------- *
   * Answer consistently so the engine takes its offline path instead of
   * waiting on a network that will never arrive. Note isOnline is ()I in this
   * build, not ()Z -- the dex is explicit about it. getNetworkType 0 is
   * "no connection". */
  if (M("isOnline") || M("isNetworkAvailable") || M("hasNetworkConnection") ||
      M("isConnected") || M("isWifiConnected") || M("hasInternet")) {
    r.i = 0; r.z = 0; return r;
  }
  if (M("getNetworkType")) { r.i = 0; return r; }
  if (M("getHttpProxyHostName")) { r.l = jni_make_string(""); return r; }
  if (M("getHttpProxyPort"))     { r.i = 0; return r; }
  if (M("getHttpProxyConfig"))   { r.l = NULL; return r; }  /* Proxy.NO_PROXY is null-ish here */

  /* ---- key/value prefs -------------------------------------------------- *
   * Both are (String, String, int). The pair is almost certainly
   * (section, key) — getValueFromKey's third argument reads as a default and
   * storeKeyValuePair's as the value, which only makes sense that way round.
   * We compose both strings into one key rather than picking one: if the
   * reading is right, that is exactly correct; if the first string is
   * something else entirely (a filename, say), composing still yields a unique
   * stable key. Discarding either string would silently collide two settings
   * that differ only in the part we dropped. */
  if (M("storeKeyValuePair") || M("getValueFromKey")) {
    char key[192];
    const char *a = jni_cstr(argv[0].l);
    const char *b = jni_cstr(argv[1].l);
    snprintf(key, sizeof key, "%s/%s", a ? a : "", b ? b : "");
    if (key[0] == '/' && !key[1]) return r;      /* both empty: nothing to do */
    if (M("storeKeyValuePair")) kv_set(key, argv[2].i);
    else                        r.i = kv_get(key, argv[2].i);
    return r;
  }

  /* ---- storage permission: always granted, nothing to ask for ----------- */
  if (M("getStoragePermissionGranted")) { r.z = 1; return r; }
  if (M("requestStoragePermission") || M("performPermissionRequest")) return r;
  if (M("showStorageRequestExplanation")) { r.z = 0; return r; }

  /* ---- window / display / immersive: all no-ops -------------------------- */
  if (M("useImmersiveMode"))   { r.z = 1; return r; }   /* (FF)Z */
  if (M("setImmersiveFlags") || M("addWindowFlags") || M("clearWindowFlags") ||
      M("setScreenCanTimeout") || M("setNativeViewVisible") ||
      M("setInputDisabled") || M("setAllowNativeKeyDown") ||
      M("runOnGameThread")) return r;
  if (M("getRealScreenSize")) { r.l = jni_make_object(); return r; }
  if (M("getResId")) { r.i = 0; return r; }

  /* ---- clipboard --------------------------------------------------------- */
  if (M("hasClipboardTextEntry")) { r.i = 0; return r; }   /* ()I in this build */
  if (M("copyToClipboard")) return r;
  if (M("pasteFromClipboard")) { r.l = jni_make_string(""); return r; }

  /* ---- shell actions with no meaning on Switch --------------------------- */
  if (M("openURL")) { r.i = 0; return r; }                 /* no browser */
  if (M("showMessageBox") || M("startSendIntent") ||
      M("startActivityForResult")) return r;
  if (M("quitApplication")) { jni_quit_requested = 1; return r; }
  if (M("showRatingPrompt")) { nk_request_rating_dismissed(); return r; }

  /* ---- audio focus ------------------------------------------------------- */
  if (M("gainAudioFocus") || M("lostFocus") || M("onAudioFocusChange")) return r;

  /* ---- soft keyboard ----------------------------------------------------- *
   * MainActivity.ShowKeyboard(Z) / SetKeyboardInputType(I) /
   * SetKeyboardMaxCharacters(I); results come back via nativeInputTextChanged,
   * nativeBackSpace, nativeKeyDown, nativeKeyboardHidden (table 2). editbox.c
   * owns the swkbd. */
  if (M("ShowKeyboard") || M("showKeyboard") || M("showSoftKeyboard")) {
    nk_request_keyboard(argv[0].l); r.z = 1; return r;
  }
  if (M("SetKeyboardInputType") || M("SetKeyboardMaxCharacters") ||
      M("keyboardHidden") || M("textChanged") || M("textDone")) return r;

  /* ---- music: MainActivity -> CustomMediaPlayer -------------------------- *
   * Identical in shape to BTD5's path, so music.c ports across unchanged. On
   * Android these wrap android.media.MediaPlayer; here they drive minimp3
   * decode + SDL mixing. Matched by name: the player is cached as a bare
   * jobject, so the reported class is usually java/lang/Object. */
  if (M("loadMusic")) {
    const char *fn = jni_cstr(argv[0].l);
    r.l = fn ? music_load(fn) : NULL;
    if (!r.l) r.l = jni_make_object();     /* never hand back NULL */
    return r;
  }
  if (M("playMusic"))       { music_play(argv[0].l, 1); return r; }
  if (M("playMusicNoLoop")) { music_play(argv[0].l, 0); return r; }
  if (M("pauseMusic"))      { music_pause(argv[0].l, 1); return r; }
  if (M("resumeMusic"))     { music_pause(argv[0].l, 0); return r; }
  if (M("killMusic") || M("stopMusic")) { music_stop(argv[0].l); return r; }
  if (M("unloadMusic"))     { music_unload(argv[0].l); return r; }
  if (M("setVolume"))       { music_set_volume(argv[0].l, argv[1].f); return r; }

  /* ---- GDPR consent (NKUserCentrics) ------------------------------------ *
   * No BTD5 equivalent. The engine calls these and then waits for the matching
   * native callbacks; nk_bootstrap.c schedules them. Answering inline here
   * would re-enter the engine from inside its own up-call. */
  if (HAS("UserCentrics") || HAS("Usercentrics")) {
    if (M("configureOptions")) { nk_request_consent_config(); return r; }
    if (M("showConsentUI"))    { nk_request_consent_ui();    return r; }
    if (MHAS("Consent") || MHAS("TCF")) return r;
  }

  /* ---- licensing: you own the game -------------------------------------- */
  if (HAS("LicenseChecker")) {
    if (M("check") || M("checkAccess") || M("isLicensed")) {
      nk_request_license_check();
      r.z = 1; return r;
    }
  }

  /* ---- store / IAP: initialises, empty catalogue, nothing purchasable ---- */
  if (HAS("Store")) {
    if (M("Init") || M("init"))          { nk_request_store_init(); return r; }
    if (M("requestProductInfo"))         { nk_request_product_info(); r.i = 0; return r; }
    if (M("requestPurchase"))            { nk_request_purchase(); r.i = 0; return r; }
    if (M("requestRestorePurchases"))    { nk_request_restore(); r.i = 0; return r; }
    if (M("requestSubscriptions"))       { nk_request_restore(); r.i = 0; return r; }
    if (M("refreshPurchases"))           { nk_request_restore(); r.i = 0; return r; }
    if (M("consumeOrders") || M("acknowledgeOrders")) { r.i = 0; return r; }
    if (M("verifyPayload"))              { r.i = 1; return r; }  /* treat as valid */
    if (M("isBillingSupported"))         { r.z = 0; return r; }
    if (M("terminate"))                  return r;
  }

  /* ---- ads (IronSource): never available -------------------------------- */
  if (HAS("IronSource")) {
    if (MHAS("isReady") || MHAS("isAvailable")) { r.z = 0; return r; }
    if (M("init") || M("initAdQuality")) { nk_request_rv_availability(); return r; }
    if (strrchr(sig, ')') && strrchr(sig, ')')[1] == 'V') return r;
    r.z = 0; r.i = 0;
  }

  /* ---- Play Games: signed out, but every getter non-NULL ----------------- *
   * Do NOT blanket-return here: the object-returning getters would hand back
   * NULL, which the engine dereferences. Answer the known ones and let the
   * rest fall through to the typed default. */
  if (HAS("PlayServices")) {
    if (MHAS("isSignedIn") || MHAS("isConnected") || MHAS("isAvailable") ||
        MHAS("isSupported") || MHAS("isEnabled"))  { r.z = 0; return r; }
    if (M("login") || M("Login") || M("signIn"))   { nk_request_playservices_login(); return r; }
    if (MHAS("PlayerId") || MHAS("PlayerID") || MHAS("AccountId") || M("playerID")) {
      r.l = jni_make_string(""); return r; }
    if (MHAS("PlayerName") || MHAS("DisplayName") || MHAS("displayName")) {
      r.l = jni_make_string("Player"); return r; }
    if (MHAS("Token") || MHAS("AuthCode")) { r.l = jni_make_string(""); return r; }
    if (MHAS("Friends") || MHAS("Invites") || MHAS("Players") || MHAS("Details")) {
      r.l = jni_make_object(); return r; }
    if (M("invitePlayers")) { r.z = 0; return r; }
  }

  /* ---- attribution / notifications / webview login ----------------------- */
  if (HAS("NKAttribution")) {
    if (M("hasInfo") || M("shouldRequest")) { r.z = 0; return r; }
    if (M("getAppSetId")) { r.l = jni_make_string(""); return r; }
    if (M("requestAppSetID")) return r;
  }
  if (HAS("NKAdjust") || HAS("NKKongregate")) {
    if (strrchr(sig, ')') && strrchr(sig, ')')[1] == 'V') return r;
  }
  if (HAS("Notifications")) {
    /* Scheduling a local notification is meaningless with no OS notification
     * service to schedule it with. Accept and drop. */
    if (strrchr(sig, ')') && strrchr(sig, ')')[1] == 'V') return r;
  }
  if (HAS("NKLoginWebView")) {
    /* There is no WebView. show/showWithQuery are voids we swallow; the engine
     * gets its "user closed it" edge from the DidHide native, which we never
     * fire, so the login flow simply never opens. */
    if (M("show") || M("showWithQuery") || M("hide") || M("injectJavascript")) return r;
  }

  /* ---- android.* helpers the engine reaches for directly ----------------- */
  if (CLS("android/os/StatFs")) {
    if (MHAS("AvailableBytes") || MHAS("FreeBytes")) { r.j = 2LL * 1024 * 1024 * 1024; return r; }
    if (MHAS("BlockSize")) { r.j = 4096; return r; }
    if (MHAS("Count") || MHAS("Blocks")) { r.j = 512 * 1024; return r; }
    return r;
  }
  if (CLS("android/net/TrafficStats")) { r.j = 0; return r; }
  if (M("isDirectory")) { r.z = 0; return r; }
  if (M("mkdirs"))      { r.z = 1; return r; }
  if (M("getIntMetaData")) { r.i = 0; return r; }

  /* ---- fall-through: return a TYPE-CORRECT value ------------------------ *
   * A zeroed jvalue is the wrong default for any method whose return type is a
   * reference: it hands the engine NULL, and Java code practically never
   * null-checks the result of its own helper calls. One unhandled
   * object-returning method is therefore an instant crash deep in engine code
   * with nothing pointing at JNI as the cause. Parse the return type out of the
   * signature and hand back something safe and non-NULL. */
  {
    const char *ret = strrchr(sig, ')');
    const char rt = ret ? ret[1] : 'V';

    switch (rt) {
      case 'V': break;
      case 'Z': case 'B': case 'C': case 'S': case 'I': r.i = 0; break;
      case 'J': r.j = 0; break;
      case 'F': r.f = 0.0f; break;
      case 'D': r.d = 0.0; break;

      case 'L':
        if (!strncmp(ret + 1, "Ljava/lang/String;", 18)) r.l = jni_make_string("");
        else                                             r.l = jni_make_object();
        break;

      case '[':
        /* The engine iterates arrays after GetArrayLength(); our fake JNI
         * reports 0 for a generic object, so an empty non-NULL array makes the
         * loop a clean no-op rather than a NULL deref. */
        r.l = jni_make_object();
        break;

      default: r.j = 0; break;
    }

    /* Rate-limit the log. Some of these are called every frame (NKAttribution
     * .hasInfo is the usual offender), and an unfiltered line per call buries
     * everything else. Three lines per distinct method, then silence. */
    {
      static const char *seen[64];
      static int hits[64];
      static int nseen = 0;
      int idx = -1;
      for (int i = 0; i < nseen; i++)
        if (seen[i] == name) { idx = i; break; }   /* name pointers are stable */
      if (idx < 0 && nseen < 64) { idx = nseen++; seen[idx] = name; hits[idx] = 0; }
      if (idx >= 0 && hits[idx]++ < 3)
        debugPrintf("JNI unhandled: %s.%s %s -> default '%c'%s\n",
                    cls, name, sig, rt,
                    (rt == 'L' || rt == '[') ? " (non-NULL stub)" : "");
    }
  }
  return r;
}

/* Software keyboard. editbox.c owns the swkbd; here we only kick it off. The
 * typed result goes back to the engine through nativeInputTextChanged /
 * nativeBackSpace / nativeKeyboardHidden (RegisterNatives table 2). Full
 * wiring is a bring-up item -- see PORTING.md. */
void nk_request_keyboard(jobject prompt) {
  (void)prompt;
  debugPrintf("ShowKeyboard requested (swkbd wiring: see PORTING.md)\n");
}
