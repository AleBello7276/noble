#pragma once

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

class KProcess final : public KernelObject {
public:
    explicit KProcess(uint32_t id) : KernelObject(KernelObjectType::KProcess), id_(id) {}

    uint32_t id() const { return id_; }

    bool terminated() const { return terminated_; }

    uint32_t exit_code() const { return exit_code_; }

private:
    friend class Kernel;

    uint32_t id_;

    GuestAddress mImageBase_ = 0;
    GuestAddress mEntryPoint_ = 0;

    bool terminated_ = false;
    uint32_t exit_code_ = 0;

    std::vector<std::unique_ptr<KThread>> threads_;

public:
    HandleTable handles;
};
