// canonical JNI 1.6 slot indices (byte offset = index*8)
//
// The Call<Type>Method family is laid out in strict type order, three slots
// each (plain / V / A):
//   34 Object  37 Boolean  40 Byte  43 Char  46 Short
//   49 Int     52 Long     55 Float 58 Double 61 Void
// and the static family repeats it from 114:
//   114 Object 117 Boolean 120 Byte 123 Char 126 Short
//   129 Int    132 Long    135 Float 138 Double 141 Void
//
// The Byte/Char/Short/Double rows were MISSING from the BTD5 donor. Double
// matters here: Supermonkey's MainActivity declares getScreenSizeInches()D.
//
// An unwired slot does not fail loudly and does not return 0. It falls through
// to jni_catch_all, which is declared to return uint64_t -- an INTEGER. Under
// the AArch64 ABI an integer return goes in x0 and a double return is read
// from d0, so d0 is simply never written and the caller reads whatever was
// left in that FP register. The value is undefined and unstable: during the
// audit it came back as ~2.8e180. A loose test assertion ("is it > 1.0?")
// passes on that garbage, which is how this survived a first round of testing.
// test/jni_dispatch_test.c now checks the exact value.
#define JNI_SLOT_COUNT 233
#define J_FindClass 6
#define J_GetMethodID 33
#define J_GetStaticMethodID 113
#define J_GetFieldID 94
#define J_GetStaticFieldID 144
#define J_NewStringUTF 167
#define J_GetStringUTFChars 169
#define J_ReleaseStringUTFChars 170
#define J_GetStringUTFLength 168
#define J_NewString 163
#define J_GetStringChars 165
#define J_ReleaseStringChars 166
#define J_GetStringLength 164
#define J_GetByteArrayElements 184
#define J_ReleaseByteArrayElements 192
#define J_GetArrayLength 171
#define J_NewByteArray 176
#define J_NewIntArray 179
#define J_NewObjectArray 172
#define J_GetObjectArrayElement 173
#define J_SetObjectArrayElement 174
#define J_GetObjectClass 31
#define J_GetVersion 4
#define J_ExceptionOccurred 15
#define J_ExceptionClear 17
#define J_ExceptionCheck 228
#define J_ExceptionDescribe 16
#define J_DeleteLocalRef 23
#define J_NewGlobalRef 21
#define J_DeleteGlobalRef 22
#define J_NewLocalRef 25
#define J_NewWeakGlobalRef 226
#define J_DeleteWeakGlobalRef 227
#define J_IsSameObject 24
#define J_EnsureLocalCapacity 26
#define J_PushLocalFrame 19
#define J_PopLocalFrame 20
#define J_RegisterNatives 215
#define J_UnregisterNatives 216
#define J_GetJavaVM 219
#define J_MonitorEnter 217
#define J_MonitorExit 218
#define J_IsInstanceOf 32
#define J_AllocObject 27
#define J_CallObjectMethod 34
#define J_CallObjectMethodV 35
#define J_CallVoidMethod 61
#define J_CallVoidMethodV 62
#define J_CallIntMethod 49
#define J_CallIntMethodV 50
#define J_CallBooleanMethod 37
#define J_CallBooleanMethodV 38
#define J_CallStaticObjectMethod 114
#define J_CallStaticObjectMethodV 115
#define J_CallStaticVoidMethod 141
#define J_CallStaticVoidMethodV 142
#define J_CallStaticIntMethod 129
#define J_CallStaticIntMethodV 130
#define J_CallStaticBooleanMethod 117
#define J_CallStaticBooleanMethodV 118
#define J_GetPrimitiveArrayCritical 222
#define J_ReleasePrimitiveArrayCritical 223
#define J_CallLongMethod 52
#define J_CallLongMethodV 53
#define J_CallByteMethod 40
#define J_CallByteMethodV 41
#define J_CallCharMethod 43
#define J_CallCharMethodV 44
#define J_CallShortMethod 46
#define J_CallShortMethodV 47
#define J_CallFloatMethod 55
#define J_CallFloatMethodV 56
#define J_CallStaticLongMethod 132
#define J_CallStaticLongMethodV 133
#define J_CallDoubleMethod 58
#define J_CallDoubleMethodV 59
#define J_CallStaticByteMethod 120
#define J_CallStaticByteMethodV 121
#define J_CallStaticCharMethod 123
#define J_CallStaticCharMethodV 124
#define J_CallStaticShortMethod 126
#define J_CallStaticShortMethodV 127
#define J_CallStaticDoubleMethod 138
#define J_CallStaticDoubleMethodV 139
#define J_CallStaticFloatMethod 135
#define J_CallStaticFloatMethodV 136
#define J_GetIntArrayElements 187
#define J_ReleaseIntArrayElements 195
#define J_NewObject 28
#define J_NewObjectV 29
#define J_NewObjectA 30
