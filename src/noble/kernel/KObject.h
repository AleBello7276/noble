#pragma once

#include "core/endian.h"
#include <atomic>
#include <cstdint>

struct X_LIST_ENTRY {
    be<uint32_t> Flink;
    be<uint32_t> Blink;
};

enum class KernelObjectType : uint8_t {
    KProcess,
    KThread,
    KEvent,
    KSemaphore,
    KMutant,
    KTimer,
};

enum X_OBJECT_TYPES : uint8_t {
    EventNotificationObject = 0x0,     // Manual Reset
    EventSynchronizationObject = 0x1,  // Auto Reset
    MutantObject = 0x2,
    ProcessObject = 0x3,
    QueueObject = 0x4,
    SemaphoreObject = 0x5,
    ThreadObject = 0x6,
    Spare1Object = 0x7,  // GateObject?
    TimerNotificationObject = 0x8,
    TimerSynchronizationObject = 0x9,
    Spare2Object = 0xA,
    Spare3Object = 0xB,
    Spare4Object = 0xC,
    Spare5Object = 0xD,
    Spare6Object = 0xE,
    Spare7Object = 0xF,
    Spare8Object = 0x10,
    Spare9Object = 0x11,
    ApcObject = 0x12,
    DpcObject = 0x13,
    DeviceQueueObject = 0x14,
    EventPairObject = 0x15,
    InterruptObject = 0x16,
    ProfileObject = 0x17,
    UndefinedObject = 0xFF,
};

typedef struct {
    struct {
        X_OBJECT_TYPES type;

        union {
            uint8_t abandoned;
            uint8_t absolute;
            uint8_t npx_irql;
            uint8_t signalling;
        };
        union {
            uint8_t size;
            uint8_t hand;
            uint8_t process_type;
        };
        union {
            uint8_t inserted;
            uint8_t debug_active;
            uint8_t dpc_active;
        };
    };

    be<uint32_t> signal_state;
    X_LIST_ENTRY wait_list;
} X_DISPATCH_HEADER;

class KernelObject {
public:
    explicit KernelObject(KernelObjectType type) : type_(type) {}

    virtual ~KernelObject() = default;

    KernelObjectType type() const { return type_; }

    virtual uint32_t guest_address() const { return 0; }

    bool RetainGuestReference() {
        auto count = guestReferences_.load(std::memory_order_relaxed);
        while (count != UINT32_MAX) {
            if (guestReferences_.compare_exchange_weak(count, count + 1, std::memory_order_relaxed))
                return true;
        }
        return false;
    }

    bool ReleaseGuestReference() {
        auto count = guestReferences_.load(std::memory_order_relaxed);
        while (count) {
            if (guestReferences_.compare_exchange_weak(count, count - 1, std::memory_order_relaxed))
                return true;
        }
        return false;
    }

    uint32_t guest_reference_count() const { return guestReferences_.load(std::memory_order_relaxed); }

private:
    KernelObjectType type_;
    std::atomic_uint32_t guestReferences_ = 0;
};
