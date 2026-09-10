/* nk_bootstrap.c -- see nk_bootstrap.h.
 *
 * Every callback signature here is from nk_natives.h, which is generated from
 * the relocations in libnative.so. Nothing in this file is guessed.
 *
 * MIT license -- see LICENSE.
 */

#include <string.h>
#include <stdlib.h>
#include <switch.h>

#include "config.h"
#include "jni.h"
#include "jni_fake.h"
#include "nk_bootstrap.h"
#include "nk_natives.h"
#include "util.h"

extern JNIEnv fake_env;

static void *g_thiz;
void nk_set_activity(void *thiz) { g_thiz = thiz; }

/* ---- resolved callbacks ------------------------------------------------ */
typedef void (*cb_v)  (JNIEnv, void *);
typedef void (*cb_b)  (JNIEnv, void *, unsigned char);
typedef void (*cb_ii) (JNIEnv, void *, int, int);
typedef void (*cb_s)  (JNIEnv, void *, void *);
typedef void (*cb_ai) (JNIEnv, void *, void *, int);
typedef void (*cb_aib)(JNIEnv, void *, void *, int, unsigned char);
typedef void (*cb_is) (JNIEnv, void *, int, void *);
typedef void (*cb_szss)(JNIEnv, void *, void *, unsigned char, void *, void *);

/* consent (Usercentrics) */
static cb_v    c_consentReadySuccess, c_consentReadyFailure, c_consentCollected;
static cb_s    c_tcfString;
static cb_szss c_serviceConsent;
/* store */
static cb_b    c_storeInit, c_cancelTransaction;
static cb_s    c_itemCallback;
static cb_aib  c_updateTransaction;
static cb_ai   c_updateSubscription;
static cb_v    c_completeRestore;
/* licensing */
static cb_ii   c_licenseResult;
/* ads */
static cb_b    c_rvAvailability;
static cb_v    c_interstitialLoadFailed;
/* misc */
static cb_is   c_playServicesLogin;
static void  (*c_ratingResult)(JNIEnv, void *, int);

/* Resolve, and keep resolving until everything is found.
 *
 * The first on-device run showed why this cannot be a one-shot: JNI_OnLoad
 * registers only MainActivity's 24 natives. LicenseChecker, Store,
 * PlayServicesInterface and NKUserCentrics -- 41 more, including the entire
 * consent and billing handshake -- are registered LATER, from inside
 * nativeSurfaceCreated. Resolving once straight after JNI_OnLoad bound every
 * one of them to NULL, so none of the answers this module exists to deliver
 * could ever have been sent.
 *
 * So it is called once up front and again from the pump until the set stops
 * growing. Cheap: a handful of string compares over a small table, and it stops
 * entirely once complete. */
void nk_bootstrap_resolve(void)
{
  #define B(v, n) if (!v) v = (void *)jni_registered(n)
  B(c_consentReadySuccess,   "nativeConsentReadySuccess");
  B(c_consentReadyFailure,   "nativeConsentReadyFailure");
  B(c_consentCollected,      "nativeConsentCollectedSuccess");
  B(c_serviceConsent,        "nativeServiceConsent");
  B(c_tcfString,             "nativeTCFString");

  B(c_storeInit,             "_native_initCallback");
  B(c_itemCallback,          "_native_itemCallback");
  B(c_updateTransaction,     "_native_updateTransactionCallback");
  B(c_updateSubscription,    "_native_updateSubscriptionCallback");
  B(c_cancelTransaction,     "_native_cancelTransactionCallback");
  B(c_completeRestore,       "_native_completeRestoreCallback");

  B(c_licenseResult,         "nativeLicenseResult");
  B(c_rvAvailability,        "nativeOnRewardedVideoAvailabilityChanged");
  B(c_interstitialLoadFailed,"nativeOnInterstitialAdLoadFailed");
  B(c_playServicesLogin,     "PlayServices_LoginCompleted");
  B(c_ratingResult,          "nativeRatingPromptResult");
  #undef B
}

/* ---- the audit --------------------------------------------------------- */
void nk_audit_natives(void)
{
  int missing = 0, total = 0;
  debugPrintf("--- RegisterNatives audit (expected %d) ---\n", NK_NATIVES_COUNT);
  #define X(name, sig) do {                                                   \
      total++;                                                                \
      if (!jni_registered(#name)) {                                           \
        missing++;                                                            \
        debugPrintf("  MISSING  %-44s %s\n", #name, sig);                     \
      }                                                                       \
    } while (0);
  NK_NATIVES_LIST
  #undef X
  if (missing == 0)
    debugPrintf("  all %d natives registered as expected\n", total);
  else
    debugPrintf("  %d/%d natives NOT registered -- your libnative.so is a\n"
                "  different build from the one this port was derived from.\n"
                "  The port may still work; expect the listed features to be\n"
                "  inert. Regenerate nk_natives.h with tools/gen_natives.py.\n",
                missing, total);
}

/* ---- scheduling -------------------------------------------------------- *
 * A tiny frame-scheduled queue. Answers are spread over the first ~90 frames
 * (1.5 s at 60fps), which both mirrors the timing of the real async Java
 * callbacks and keeps two of them from landing inside the same engine lock. */

#define MAX_PENDING 24

typedef struct { unsigned long long at; void (*fn)(void); int done; } Pending;
static Pending  g_q[MAX_PENDING];
static int      g_qn;
static Mutex    g_qlock;
static int      g_qinit;

static void q_once(void) { if (!g_qinit) { mutexInit(&g_qlock); g_qinit = 1; } }

static void schedule(unsigned long long at, void (*fn)(void))
{
  q_once();
  mutexLock(&g_qlock);
  /* Collapse duplicates: the engine retries several of these every frame while
   * it waits, and without this a stalled handshake queues thousands of copies. */
  for (int i = 0; i < g_qn; i++)
    if (g_q[i].fn == fn && !g_q[i].done) { mutexUnlock(&g_qlock); return; }
  if (g_qn < MAX_PENDING) { g_q[g_qn].at = at; g_q[g_qn].fn = fn; g_q[g_qn].done = 0; g_qn++; }
  mutexUnlock(&g_qlock);
}

/* ---- the answers ------------------------------------------------------- */

/* GDPR consent. The engine wants, in this order: "the consent SDK is ready"
 * then "the user's choices have been collected". We additionally deny every
 * individual service and hand back an empty TCF string, which is the correct
 * shape for a device with no ad SDKs running at all. */
static void answer_consent_ready(void)
{
  if (c_consentReadySuccess) { c_consentReadySuccess(fake_env, g_thiz); debugPrintf("[boot] consent: ready\n"); }
  else if (c_consentReadyFailure) c_consentReadyFailure(fake_env, g_thiz);
}

static void answer_consent_collected(void)
{
  /* nativeServiceConsent(String service, bool granted, String a, String b).
   * Reporting a single denied placeholder service is enough for the engine to
   * consider the list delivered; it iterates whatever it is given. */
  if (c_serviceConsent)
    c_serviceConsent(fake_env, g_thiz, jni_make_string("offline"), 0,
                     jni_make_string(""), jni_make_string(""));
  if (c_tcfString) c_tcfString(fake_env, g_thiz, jni_make_string(""));
  if (c_consentCollected) { c_consentCollected(fake_env, g_thiz); debugPrintf("[boot] consent: collected\n"); }
}

/* Licensing. Verified against THIS binary, not inherited: disassembly of
 * nativeLicenseResult (0x756460) maps the first int argument to an internal
 * tri-state -- result==1 -> 2, result==-1 -> 0, anything else -> 1. State 1 is
 * the permissive value (Android LVL's convention is 0 == LICENSED), so (0, 0)
 * is "licensed, no reason code". You own the game; this reports that. */
static void answer_license(void)
{
  if (c_licenseResult) { c_licenseResult(fake_env, g_thiz, 0, 0); debugPrintf("[boot] license: allowed\n"); }
}

/* Billing. "Initialised successfully, with an empty catalogue." The engine
 * needs the init edge before it will draw any store-adjacent UI; the empty
 * product list then makes every purchasable item simply not appear. */
static void answer_store_init(void)
{
  if (c_storeInit) { c_storeInit(fake_env, g_thiz, 1); debugPrintf("[boot] store: init ok\n"); }
  /* Empty, non-NULL product array. jni_fake reports length 0 for a generic
   * object, so the engine's iteration over it is a clean no-op. Handing NULL
   * here instead is an immediate crash inside the engine's catalogue parse. */
  if (c_itemCallback) c_itemCallback(fake_env, g_thiz, jni_make_object());
}

static void answer_no_purchases(void)
{
  /* No owned orders, and the restore completes. Without the completion edge
   * the "Restoring..." modal never closes. */
  if (c_updateTransaction)  c_updateTransaction(fake_env, g_thiz, jni_make_object(), 0, 0);
  if (c_updateSubscription) c_updateSubscription(fake_env, g_thiz, jni_make_object(), 0);
  if (c_completeRestore)    { c_completeRestore(fake_env, g_thiz); debugPrintf("[boot] store: no purchases\n"); }
}

/* Ads. Never available, so every ad-gated button disables itself rather than
 * hanging on a load that will not finish. */
static void answer_no_ads(void)
{
  if (c_rvAvailability)         c_rvAvailability(fake_env, g_thiz, 0);
  if (c_interstitialLoadFailed) c_interstitialLoadFailed(fake_env, g_thiz);
  debugPrintf("[boot] ads: unavailable\n");
}

/* Play Games: not signed in. Result 0 with an empty id is the "declined /
 * unavailable" shape; the engine falls back to local profiles. */
static void answer_playservices(void)
{
  if (c_playServicesLogin) { c_playServicesLogin(fake_env, g_thiz, 0, jni_make_string("")); debugPrintf("[boot] play services: signed out\n"); }
}

static void answer_rating_dismissed(void)
{
  /* 0 == dismissed without rating. Closes the modal the engine believes is up. */
  if (c_ratingResult) c_ratingResult(fake_env, g_thiz, 0);
}

/* ---- boot schedule ----------------------------------------------------- *
 * Frame numbers, not milliseconds: the engine's gates advance per tick, so a
 * wall-clock delay would drift against them on a slow first load. */
static int g_scheduled;

/* Current frame, published for the relative scheduling the up-call handlers
 * below do. Without this they all schedule against frame 0, i.e. fire on the
 * very next pump -- which defeats the whole point of deferring them out of the
 * engine's own call stack. */
static unsigned long long g_now;

void nk_bootstrap_pump(unsigned long long frame)
{
  g_now = frame;

  /* Late-registered tables: keep looking until they turn up. */
  static int settled = 0;
  if (!settled) {
    nk_bootstrap_resolve();
    if (c_consentReadySuccess && c_storeInit && c_licenseResult) {
      settled = 1;
      debugPrintf("[boot] all async callbacks resolved by frame %llu\n",
                  (unsigned long long)frame);
      nk_audit_natives();       /* now the whole surface exists, audit it */
    } else if (frame == 120) {
      debugPrintf("[boot] after 120 frames still unresolved:%s%s%s\n",
                  c_consentReadySuccess ? "" : " consent",
                  c_storeInit           ? "" : " store",
                  c_licenseResult       ? "" : " license");
      settled = 1;              /* stop retrying; they are not coming */
      nk_audit_natives();
    }
  }

  if (!g_scheduled) {
    g_scheduled = 1;
    schedule(6,  answer_consent_ready);
    schedule(12, answer_consent_collected);
    schedule(20, answer_license);
    schedule(28, answer_store_init);
    schedule(36, answer_no_purchases);
    schedule(44, answer_no_ads);
    schedule(52, answer_playservices);
  }

  q_once();
  mutexLock(&g_qlock);
  for (int i = 0; i < g_qn; i++) {
    if (!g_q[i].done && frame >= g_q[i].at) {
      void (*fn)(void) = g_q[i].fn;
      g_q[i].done = 1;
      mutexUnlock(&g_qlock);
      fn();                      /* called UNLOCKED: these re-enter the engine */
      mutexLock(&g_qlock);
    }
  }
  mutexUnlock(&g_qlock);
}

/* ---- up-call request handlers ----------------------------------------- *
 * ninjakiwi.c calls these when the engine asks the Java shell to start
 * something. We schedule the answer a few frames out rather than answering
 * inline, because answering inline re-enters the engine from inside its own
 * up-call -- which in this engine deadlocks on the same non-recursive mutex it
 * took to make the call. */
void nk_request_consent_ui(void)        { schedule(g_now + 4, answer_consent_collected); }
void nk_request_consent_config(void)    { schedule(g_now + 2, answer_consent_ready); }
void nk_request_store_init(void)        { schedule(g_now + 4, answer_store_init); }
void nk_request_license_check(void)     { schedule(g_now + 4, answer_license); }
void nk_request_product_info(void)      { schedule(g_now + 3, answer_store_init); }
void nk_request_restore(void)           { schedule(g_now + 3, answer_no_purchases); }
void nk_request_purchase(void)          { schedule(g_now + 3, answer_no_purchases); }
void nk_request_rv_availability(void)   { schedule(g_now + 2, answer_no_ads); }
void nk_request_rating_dismissed(void)  { schedule(g_now + 2, answer_rating_dismissed); }
void nk_request_playservices_login(void){ schedule(g_now + 4, answer_playservices); }
