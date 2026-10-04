#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "GuestThread.h"
#include "KObject.h"

using Handle = uint32_t;

class HandleTable {
public:
    // retain an object under a new process handle
    Handle Insert(KernelObject* object);

    // find an object while the owning process retains its lifetime
    KernelObject* Lookup(Handle handle);

    // release a handle without destroying the process owned object
    void Remove(Handle handle);

private:
    std::mutex mutex_;
    Handle next_handle_ = 0x100;

    std::unordered_map<Handle, KernelObject*> objects_;
};

class KThread;
class Memory;

using GuestAddress = uint32_t;

// identify the guest execution environment using the kernel process type values
enum class ProcessType : uint32_t { Idle = 0, Title = 1, System = 2 };

class KProcess final : public KernelObject {
public:
    static constexpr size_t TLS_SLOT_COUNT = 256;
    explicit KProcess(uint32_t id, ProcessType type = ProcessType::Title);
    ~KProcess();

    uint32_t id() const { return id_; }

    // get the guest address of the process record referenced by each kthread
    GuestAddress guest_address() const override { return guestAddress_; }

    // get the process type inherited by threads belonging to this process
    ProcessType type() const { return type_; }

    bool terminated() const { return terminated_; }

    uint32_t exit_code() const { return exit_code_; }

private:
    friend class Kernel;
    friend class KThread;

    // detach a terminated thread while preserving its guest object until kernel shutdown
    void DetachGuestThread(KThread& thread);

    uint32_t id_;
    ProcessType type_;

    GuestAddress mImageBase_ = 0;
    GuestAddress mEntryPoint_ = 0;

    bool terminated_ = false;
    uint32_t exit_code_ = 0;

    std::vector<std::unique_ptr<KThread>> threads_;
    GuestAddress guestAddress_ = 0;
    GuestKernelProcess* guestProcess_ = nullptr;
    uint32_t tlsSlotCount_ = TLS_SLOT_COUNT;
    Memory* memory_ = nullptr;
    std::mutex guestThreadsMutex_;

public:
    HandleTable handles;
};
