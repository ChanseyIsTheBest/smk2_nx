/* nk_bootstrap.h -- drives the ASYNCHRONOUS platform answers the engine waits
 * for after nativeLoad.
 *
 * This module has no counterpart in the BTD5 donor, and it is the single
 * biggest piece of new work in this port.
 *
 * On Android, MainActivity kicks off several long-running platform jobs during
 * startup -- the Usercentrics GDPR consent flow, Google Play Billing setup, the
 * Play Licensing check, IronSource ad availability -- and each reports back
 * later by calling a registered native. The engine's boot path does not
 * proceed past its first-run gate until it has heard from them. There is no
 * timeout on most of these: an answer that never arrives is a permanent wait,
 * which presents as a game that renders its loading screen forever with a
 * perfectly healthy 60fps heartbeat in the log. That is a genuinely confusing
 * failure to debug from cold, hence this file existing separately and saying so.
 *
 * We answer all of them, in the "offline, owned, nothing purchasable, no ads,
 * consent already handled" shape.
 *
 * WHY THE DELAY. The callbacks are NOT delivered immediately. The engine
 * installs its handlers during the first few nativeTicks, and a callback that
 * arrives before its handler exists is dropped -- silently, since the engine
 * treats a null handler as "not interested yet", not as an error. So each
 * answer is scheduled a few frames in. The frame numbers are deliberately
 * spread rather than all fired on one frame: several of these paths take the
 * same internal lock, and delivering them together made the BTD5-lineage
 * engine re-enter it.
 */

#ifndef __NK_BOOTSTRAP_H__
#define __NK_BOOTSTRAP_H__

/* Resolve the async result callbacks from the RegisterNatives registry.
 * Call once, right after JNI_OnLoad. */
void nk_bootstrap_resolve(void);

/* Call once per frame from the main loop with the current frame counter.
 * Fires each scheduled answer on its frame; cheap no-op afterwards. */
void nk_bootstrap_pump(unsigned long long frame);

/* Tell this module which fake MainActivity object to pass as the callbacks'
 * jobject receiver. Call before the first pump. */
void nk_set_activity(void *thiz);

/* Compare the natives the engine actually registered against nk_natives.h and
 * log any mismatch by name. Purely diagnostic -- a mismatch means your
 * libnative.so is a different build from the one this port was derived from,
 * which is worth knowing at boot rather than at the first crash. */
void nk_audit_natives(void);

/* Request handlers invoked from ninjakiwi.c when the engine calls UP into the
 * Java shell. They schedule the matching answer to come back DOWN. */
void nk_request_consent_ui(void);      /* NKUserCentrics.showConsentUI      */
void nk_request_consent_config(void);  /* NKUserCentrics.configureOptions   */
void nk_request_store_init(void);      /* Store construction / Init         */
void nk_request_license_check(void);   /* LicenseChecker.check              */
void nk_request_product_info(void);    /* Store.requestProductInfo          */
void nk_request_restore(void);         /* Store.requestRestorePurchases     */
void nk_request_purchase(void);        /* Store.requestPurchase             */
void nk_request_rv_availability(void); /* IronSource ad availability        */
void nk_request_rating_dismissed(void);/* MainActivity.showRatingPrompt     */
void nk_request_playservices_login(void); /* PlayServicesInterface login    */

#endif
