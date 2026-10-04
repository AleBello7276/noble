#include "KProcess.h"
#include "KThread.h"
#include "core/byte_swap.h"
#include "emulator/Memory.h"
#include <stdexcept>

Handle HandleTable::Insert(KernelObject* object) {
    if (!object)
        throw std::invalid_argument("cannot create a handle for a null object");

    std::scoped_lock lock(mutex_);

    if (next_handle_ > UINT32_MAX - 4)
        throw std::bad_alloc();

    const Handle handle = next_handle_;
    objects_.emplace(handle, object);
    next_handle_ += 4;
    return handle;
}

KernelObject* HandleTable::Lookup(Handle handle) {
    std::scoped_lock lock(mutex_);

    const auto object = objects_.find(handle);
    return object == objects_.end() ? nullptr : object->second;
}

void HandleTable::Remove(Handle handle) {
    std::scoped_lock lock(mutex_);

    objects_.erase(handle);
}

KProcess::KProcess(uint32_t id, ProcessType type)
    : KernelObject(KernelObjectType::KProcess), id_(id), type_(type) {}

KProcess::~KProcess() = default;

void KProcess::DetachGuestThread(KThread& thread) {
    std::scoped_lock lock(guestThreadsMutex_);
    if (!thread.guestProcessEntryLinked_)
        return;

    auto& entry = thread.guestThread_->processEntry;
    auto* previous
        = static_cast<GuestListEntry*>(memory_->Translate(byte_swap(entry.previous), sizeof(GuestListEntry)));
    auto* next
        = static_cast<GuestListEntry*>(memory_->Translate(byte_swap(entry.next), sizeof(GuestListEntry)));
    previous->next = entry.next;
    next->previous = entry.previous;
    entry.next = entry.previous = byte_swap(
        static_cast<GuestAddress>(thread.guestAddress_ + offsetof(GuestKernelThread, processEntry)));
    guestProcess_->threadCount = byte_swap(byte_swap(guestProcess_->threadCount) - 1);
    thread.guestProcessEntryLinked_ = false;
}
