#pragma once

#include <cstdint>

enum class KernelObjectType : uint8_t {
    KProcess,
    KThread,
    KEvent,
    KSemaphore,
    KMutant,
    KTimer,
};

class KernelObject {
public:
    explicit KernelObject(KernelObjectType type) : type_(type) {}

    virtual ~KernelObject() = default;

    KernelObjectType type() const { return type_; }

private:
    KernelObjectType type_;
};
