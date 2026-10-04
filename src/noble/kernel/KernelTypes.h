#pragma once

#include "KEvent.h"
#include "KProcess.h"
#include "core/endian.h"
#include "emulator/PPCModule.h"
#include <cstdint>
namespace hle::krnl {
struct TimeStampBundle;
struct CriticalSection;
}  // namespace hle::krnl

constexpr uint32_t kInvalidProcessor = UINT32_MAX;
constexpr uint8_t kAllProcessors = 0x3F;

class Memory;
class Scheduler;

struct ProcessCreateInfo {
    GuestAddress image_base = 0;
    GuestAddress entry_point = 0;
    ProcessType type = ProcessType::Title;
};

struct ThreadCreateInfo {
    GuestAddress entry_point = 0;
    GuestAddress parameter = 0;
    // optional xapi trampoline receives the entry point in r3 and parameter in r4
    GuestAddress startup = 0;
    uint32_t creation_flags = 0;
    // allocate the handle in the calling process when it differs from the target process
    KProcess* handle_process = nullptr;

    uint32_t stack_size = 512 * 1024;

    uint8_t affinity_mask = kAllProcessors;
    int32_t priority = 0;

    bool create_suspended = false;
};

struct X_KSPINLOCK {
    be<uint32_t> prcb_of_owner;
};

struct X_OBJECT_TYPE {
    be<uint32_t> allocate_proc;            // 0x0
    be<uint32_t> free_proc;                // 0x4
    be<uint32_t> close_proc;               // 0x8
    be<uint32_t> delete_proc;              // 0xC
    be<uint32_t> unknown_proc;             // 0x10
    be<uint32_t> unknown_size_or_object_;  // this seems to be a union, it can be a pointer
                                           // or it can be the size of the object
    be<uint32_t> pool_tag;                 // 0x18
};

struct X_KPROCESS {
    X_KSPINLOCK thread_list_spinlock;
    // list of threads in this process, guarded by the spinlock above
    X_LIST_ENTRY thread_list;

    // quantum value assigned to each thread of the process
    be<int32_t> quantum;
    // kernel sets this to point to a section of size 0x2F700 called CLRDATAA,
    // except it clears bit 31 of the pointer. in 17559 the address is 0x801C0000,
    // so it sets this ptr to 0x1C0000
    be<uint32_t> clrdataa_masked_ptr;
    be<uint32_t> thread_count;
    uint8_t process_priority_class;
    uint8_t default_thread_priority;
    uint8_t max_dynamic_priority;
    uint8_t disable_quantum_decay;
    be<uint32_t> kernel_stack_size;
    be<uint32_t> tls_static_data_address;
    be<uint32_t> tls_data_size;
    be<uint32_t> tls_raw_data_size;
    be<uint16_t> tls_slot_size;
    // ExCreateThread calls a subfunc references this field, returns
    // X_STATUS_PROCESS_IS_TERMINATING if true
    uint8_t is_terminating;
    // one of X_PROCTYPE_
    uint8_t process_type;
    be<uint32_t> tls_slot_bitmap[8];
    be<uint32_t> unk_50;
    X_LIST_ENTRY unk_54;
    be<uint32_t> unk_5C;
};

struct KernelGuestGlobals {
    X_OBJECT_TYPE ExThreadObjectType;
    X_OBJECT_TYPE ExEventObjectType;
    X_OBJECT_TYPE ExMutantObjectType;
    X_OBJECT_TYPE ExSemaphoreObjectType;
    X_OBJECT_TYPE ExTimerObjectType;
    X_OBJECT_TYPE IoCompletionObjectType;
    X_OBJECT_TYPE IoDeviceObjectType;
    X_OBJECT_TYPE IoFileObjectType;
    X_OBJECT_TYPE ObDirectoryObjectType;
    X_OBJECT_TYPE ObSymbolicLinkObjectType;
    // a constant buffer that some object types' "unknown_size_or_object" field
    // points to
    X_DISPATCH_HEADER XboxKernelDefaultObject;
    X_KPROCESS idle_process;    // X_PROCTYPE_IDLE. runs in interrupt contexts. is
                                // also the context the kernel starts in?
    X_KPROCESS title_process;   // X_PROCTYPE_TITLE
    X_KPROCESS system_process;  // X_PROCTYPE_SYSTEM. no idea when this runs. can
                                // create threads in this process with
                                // ExCreateThread and the thread flag 0x2

    // locks.
    X_KSPINLOCK dispatcher_lock;  // called the "dispatcher lock" in nt 3.5 ppc
                                  // .dbg file. Used basically everywhere that
                                  // DISPATCHER_HEADER'd objects appear
    // this lock is only used in some Ob functions. It's odd that it is used at
    // all, as each table already has its own spinlock.
    X_KSPINLOCK ob_lock;
    X_KSPINLOCK tls_lock;  // protects per-process TLS bitmap allocations

    // if LLE emulating Xam, this is needed or you get an immediate freeze
    X_KEVENT UsbdBootEnumerationDoneEvent;
};
