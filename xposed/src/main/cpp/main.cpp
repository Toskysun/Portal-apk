
#include <dobby.h>
#include <atomic>
#include <jni.h>
#include <sys/mman.h>
#include <unistd.h>
#include "sensor_hook.h"

// Toggled by Java; read from hooked functions potentially on other threads.
std::atomic_bool enableSensorHook{false};

// 0 = not installed, 1 = installing, 2 = installed
static std::atomic_int g_sensorHookState{0};

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    return JNI_VERSION_1_6;
}

extern "C"
JNIEXPORT void JNICALL
Java_moe_fuqiuluo_dobby_Dobby_setStatus(JNIEnv *env, jobject thiz, jboolean status) {
    const bool enabled = (status == JNI_TRUE);
    enableSensorHook.store(enabled, std::memory_order_relaxed);

    // Delay inline hooking until the feature is actually enabled. This avoids
    // running Dobby hooks during zygote fork/specialize on newer Android.
    if (enabled) {
        int expected = 0;
        if (g_sensorHookState.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            const bool ok = doSensorHook();
            g_sensorHookState.store(ok ? 2 : 0, std::memory_order_release);
        }
    }
}
