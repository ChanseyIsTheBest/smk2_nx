/* nk_natives.h -- GENERATED. The complete RegisterNatives surface of Bloons
 * Supermonkey's libnative.so.
 *
 * Regenerate with:   python3 tools/gen_natives.py libnative.so
 *
 * This is GROUND TRUTH, not inference: each row is a JNINativeMethod triple
 * {const char *name, const char *signature, void *fnPtr} recovered from the
 * relocations in .rela.dyn / .rela.plt. The name and signature pointers are
 * R_AARCH64_RELATIVE (addend = the string's link-time address); the function
 * pointer is usually R_AARCH64_ABS64 against an exported symbol, because this
 * build -- unlike BTD5's, where nativeLicenseResult was emitted as the
 * obfuscated symbol 'ox94jnabared' -- exports its callbacks under readable
 * C++-mangled names.
 *
 * 65 natives in 11 tables. Registration order below matches address order,
 * which is the order JNI_OnLoad walks them.
 *
 * The loader does NOT bind these by symbol. It binds by the name the engine
 * passes to RegisterNatives (jni_registered() in jni_fake.c), which is what
 * Android itself does and which keeps working if a future build obfuscates or
 * makes a callback static. This header exists so the expected surface is
 * checkable against a boot log, and so a missing binding names itself.
 */

#ifndef __NK_NATIVES_H__
#define __NK_NATIVES_H__

/* name, signature -- for the startup audit in nk_bootstrap.c */
#define NK_NATIVES_LIST \
  X(nativeRatingPromptResult                     , "(I)V") \
  X(useImmersiveMode                             , "(FF)Z") \
  X(nativeBackPressed                            , "()V") \
  X(nativeLoad                                   , "(Lcom/ninjakiwi/MainActivity;Ljava/lang/Object;)V") \
  X(nativeLowMemory                              , "(I)V") \
  X(nativeOrientationChanged                     , "(I)V") \
  X(nativePause                                  , "()V") \
  X(nativeResume                                 , "()V") \
  X(nativeResize                                 , "(II)V") \
  X(nativeSurfaceCreated                         , "(Landroid/view/Surface;II)V") \
  X(nativeSurfaceDestroyed                       , "()V") \
  X(nativeTick                                   , "()V") \
  X(nativeTouchEnded                             , "(FFI)V") \
  X(nativeTouchCancelled                         , "(FFI)V") \
  X(nativeTouchHeld                              , "(FFIZ)V") \
  X(nativeTouchStarted                           , "(FFI)V") \
  X(nativeUnload                                 , "()V") \
  X(nativeGainedAudioFocus                       , "()V") \
  X(nativeLostAudioFocus                         , "(Z)V") \
  X(nativeGetPublicKey                           , "()Ljava/lang/String;") \
  X(nativeOnActivityResult                       , "(IILandroid/content/Intent;)V") \
  X(nativeNotificationReceived                   , "(Z)V") \
  X(nativeDeepLinkURI                            , "(Ljava/lang/String;)V") \
  X(nativeInputKeyDown                           , "(II)V") \
  X(nativeInputKeyUp                             , "(II)V") \
  X(nativeKeyDown                                , "(II)V") \
  X(nativeBackSpace                              , "()V") \
  X(nativeInputTextChanged                       , "(Ljava/lang/String;)V") \
  X(nativeKeyboardHidden                         , "()V") \
  X(_native_updateTransactionCallback            , "([Lcom/ninjakiwi/Store$Order;IZ)V") \
  X(_native_itemCallback                         , "([Lcom/ninjakiwi/Store$Product;)V") \
  X(_native_initCallback                         , "(Z)V") \
  X(_native_updateSubscriptionCallback           , "([Lcom/ninjakiwi/Store$Order;I)V") \
  X(_native_cancelTransactionCallback            , "(Z)V") \
  X(_native_completeRestoreCallback              , "()V") \
  X(nativeLicenseResult                          , "(II)V") \
  X(nativeOnRewardedVideoAdOpened                , "()V") \
  X(nativeOnRewardedVideoAdClosed                , "()V") \
  X(nativeOnRewardedVideoAvailabilityChanged     , "(Z)V") \
  X(nativeOnRewardedVideoAdShowFailed            , "()V") \
  X(nativeOnRewardedVideoAdRewarded              , "(Ljava/lang/String;I)V") \
  X(nativeOnInterstitialAdReady                  , "()V") \
  X(nativeOnInterstitialAdLoadFailed             , "()V") \
  X(nativeOnInterstitialAdOpened                 , "()V") \
  X(nativeOnInterstitialAdClosed                 , "()V") \
  X(nativeOnInterstitialAdShowSucceeded          , "()V") \
  X(nativeOnInterstitialAdShowFailed             , "()V") \
  X(nativeConsentCollectedSuccess                , "()V") \
  X(nativeConsentReadySuccess                    , "()V") \
  X(nativeConsentReadyFailure                    , "()V") \
  X(nativeServiceConsent                         , "(Ljava/lang/String;ZLjava/lang/String;Ljava/lang/String;)V") \
  X(nativeTCFString                              , "(Ljava/lang/String;)V") \
  X(Notifications_PushNotesRegistrationSuccessful , "(Ljava/lang/String;)V") \
  X(PlayServices_LoginCompleted                  , "(ILjava/lang/String;)V") \
  X(PlayServices_FriendsCompleted                , "([Lcom/ninjakiwi/PlayServicesInterface$PlayerDetails;)V") \
  X(PlayServices_SendInvitesCompleted            , "(Ljava/util/ArrayList;)V") \
  X(PlayServices_ReceivedInvite                  , "(Ljava/lang/String;Ljava/lang/String;)V") \
  X(ManualClose                                  , "()V") \
  X(DidHide                                      , "()V") \
  X(LoginFailed                                  , "()V") \
  X(UrlRequested                                 , "(Ljava/lang/String;)Z") \
  X(ManualClose                                  , "()V") \
  X(DidHide                                      , "()V") \
  X(LoginFailed                                  , "()V") \
  X(UrlRequested                                 , "(Ljava/lang/String;)Z")

#define NK_NATIVES_COUNT 65

#endif
