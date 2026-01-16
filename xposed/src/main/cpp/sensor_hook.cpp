//
// Created by fuqiuluo on 2024/10/15.
//
#include <dobby.h>
#include <atomic>
#include <unistd.h>
#include "sensor_hook.h"
#include "logging.h"
#include "elf_util.h"
#include "dobby_hook.h"

// Resolve actual on-device path via /proc/self/maps (APEX/system variations).
static constexpr const char* kSensorServiceSoName = "libsensorservice.so";

extern std::atomic_bool enableSensorHook;

// _ZN7android16SensorEventQueue5writeERKNS_2spINS_7BitTubeEEEPK12ASensorEventm
OriginalSensorEventQueueWriteType OriginalSensorEventQueueWrite = nullptr;

OriginalConvertToSensorEventType OriginalConvertToSensorEvent = nullptr;

int64_t SensorEventQueueWrite(void *tube, void *events, int64_t numEvents) {
    if (enableSensorHook.load(std::memory_order_relaxed)) {
        LOGD("SensorEventQueueWrite called");
    }
    return OriginalSensorEventQueueWrite(tube, events, numEvents);
}

void ConvertToSensorEvent(void *src, void *dst) {
    if (enableSensorHook.load(std::memory_order_relaxed)) {
        auto a = *(int32_t *)((char*)src + 4);
        auto b = *(int32_t *)((char*)src + 8);
        auto c = *(int64_t *)((char*)src + 16);

        *(int64_t *)((char*)dst + 16) = 0LL;
        *(int32_t *)((char*)dst + 24) = 0;
        *(int64_t *)((char*)dst) = c;
        *(int32_t *)((char*)dst + 8) = a;
        *(int32_t *)((char*)dst + 12) = b;
        *(int8_t *)((char*)dst + 28) = b;

        if (b == 18) {
            *(float *)((char*)dst + 16) = -1.0;
        } else if (b == 19) {
            *(int64_t *)((char*)dst + 16) = -1;
        } else {
            *(float *)((char*)dst + 16) = -1.0;
            *(float *)((char*)dst + 24) = -1.0;
            *(int8_t *)((char*)dst + 28) = *(int8_t *)((char*)src + 36);
        }
    } else {
        if (OriginalConvertToSensorEvent != nullptr) {
            OriginalConvertToSensorEvent(src, dst);
        }
    }

    if (enableSensorHook.load(std::memory_order_relaxed)) {
        LOGD("ConvertToSensorEvent called");
    }
}

bool doSensorHook() {
    SandHook::ElfImg sensorService(kSensorServiceSoName);

    if (!sensorService.isValid()) {
        LOGE("failed to load libsensorservice");
        return false;
    }

    auto sensorWrite = sensorService.getSymbolAddress<void*>("_ZN7android16SensorEventQueue5writeERKNS_2spINS_7BitTubeEEEPK12ASensorEventm");
    if (sensorWrite == nullptr) {
        sensorWrite = sensorService.getSymbolAddress<void*>("_ZN7android16SensorEventQueue5writeERKNS_2spINS_7BitTubeEEEPK12ASensorEventj");
    }

    auto convertToSensorEvent = sensorService.getSymbolAddress<void*>("_ZN7android8hardware7sensors4V1_014implementation20convertToSensorEventERKNS2_5EventEP15sensors_event_t");

    LOGD("Dobby SensorEventQueue::write found at %p", sensorWrite);
    LOGD("Dobby convertToSensorEvent found at %p", convertToSensorEvent);

    bool installedAny = false;
    if (sensorWrite != nullptr) {
        auto orig = InlineHook(sensorWrite, (void *)SensorEventQueueWrite);
        OriginalSensorEventQueueWrite = (OriginalSensorEventQueueWriteType)orig;
        installedAny |= (orig != nullptr);
    }

    if (convertToSensorEvent != nullptr) {
        auto orig = InlineHook(convertToSensorEvent, (void *)ConvertToSensorEvent);
        OriginalConvertToSensorEvent = (OriginalConvertToSensorEventType)orig;
        installedAny |= (orig != nullptr);
    }

    return installedAny;
}
