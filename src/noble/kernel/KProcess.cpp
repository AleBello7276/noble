#include "KProcess.h"
#include "KThread.h"
#include "core/byte_swap.h"
#include "emulator/Memory.h"

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
