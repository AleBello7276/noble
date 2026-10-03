#pragma once

#include <bitset>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "KObject.h"

using Handle = uint32_t;

class HandleTable {
public:
    Handle Insert(KernelObject* object);

    KernelObject* Lookup(Handle handle);

    void Remove(Handle handle);

private:
    Handle next_handle_ = 0x100;

    std::unordered_map<Handle, KernelObject*> objects_;
};

class KThread;

using GuestAddress = uint32_t;

// identify the guest execution environment using the kernel process type values
enum class ProcessType : uint32_t { Idle = 0, Title = 1, System = 2 };

class KProcess final : public KernelObject {
public:
    static constexpr size_t TLS_SLOT_COUNT = 2048;
    explicit KProcess(uint32_t id, ProcessType type = ProcessType::Title);
    ~KProcess();

    uint32_t id() const { return id_; }

    // get the process type inherited by threads belonging to this process
    ProcessType type() const { return type_; }

    bool terminated() const { return terminated_; }

    uint32_t exit_code() const { return exit_code_; }

private:
    friend class Kernel;

    uint32_t id_;
    ProcessType type_;

    GuestAddress mImageBase_ = 0;
    GuestAddress mEntryPoint_ = 0;

    bool terminated_ = false;
    uint32_t exit_code_ = 0;

    std::vector<std::unique_ptr<KThread>> threads_;
    std::bitset<TLS_SLOT_COUNT> tlsSlots_{};

public:
    HandleTable handles;
};
