#include "Kernel.h"

#include "GuestModule.h"
#include "KThread.h"
#include "Loader/XEXImage.h"
#include "Logger.h"
#include "core/byte_swap.h"
#include "cpu/Scheduler.h"
#include "emulator/Memory.h"
#include "gpu/NullGPU.h"
#include "hle/Exports.h"
#include "hle/krnl/Variables.h"
#include <algorithm>
#include <cstring>

namespace {

void InitializeList(GuestListEntry& list, GuestAddress address) {
    list.next = list.previous = byte_swap(address);
}

void InitializeProcessTLS(GuestKernelProcess& process, uint32_t slots, uint32_t dataSize, uint32_t rawSize,
                          GuestAddress dataAddress) {
    process.tlsTemplate = byte_swap(dataAddress);
    process.tlsDataSize = byte_swap(dataSize);
    process.tlsRawDataSize = byte_swap(rawSize);
    process.tlsSlotSize = byte_swap(static_cast<uint16_t>(slots * 4));
    for (uint32_t word = 0; word < 8; ++word) {
        const uint32_t available = slots > word * 32 ? std::min<uint32_t>(slots - word * 32, 32u) : 0;
        process.tlsSlotBitmap[word] = byte_swap(available ? UINT32_MAX << (32 - available) : 0u);
    }
}

bool TLSAllocated(const GuestKernelProcess& process, uint32_t index, uint32_t limit) {
    return index < limit && !(byte_swap(process.tlsSlotBitmap[index / 32]) & (0x80000000u >> (index % 32)));
}

}  // namespace

Kernel::Kernel(Memory& memory, Scheduler& scheduler, std::unique_ptr<GPUBackend> gpu)
    : memory_(memory), scheduler_(scheduler), gpu_(gpu ? std::move(gpu) : std::make_unique<NullGPU>(memory)),
      imports_(*this, memory) {}

bool Kernel::Initialize() {
    if (timeStampTimer_.joinable())
        return false;

    next_process_id_ = 1;
    next_thread_id_ = 1;

    try {
        if (!gpu_->Initialize()) {
            gpu_->Shutdown();
            return false;
        }
        hle::RegisterExports(imports_);
        const auto timestampAddress = imports_.VariableAddress(XboxLibrary::XboxKrnl, "KeTimeStampBundle");

        timeStampBundle_ = static_cast<hle::krnl::TimeStampBundle*>(
            memory_.Translate(timestampAddress, sizeof(hle::krnl::TimeStampBundle)));
        clockStart_ = std::chrono::steady_clock::now();

        using ClockUnits = std::chrono::duration<int64_t, std::ratio<1, 10000000>>;
        constexpr int64_t windowsEpochOffset = 116444736000000000LL;

        systemTimeStart_
            = std::chrono::duration_cast<ClockUnits>(std::chrono::system_clock::now().time_since_epoch())
                  .count()
              + windowsEpochOffset;

        UpdateTimeStampBundle();

        timeStampTimer_ = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                UpdateTimeStampBundle();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    } catch (const std::exception& error) {
        LOG_ERROR("Unable to initialize kernel exports: {}", error.what());
        timeStampBundle_ = nullptr;

        imports_.ClearVariables();
        gpu_->Shutdown();
        return false;
    }

    return true;
}

void Kernel::Shutdown() {
    timeStampTimer_.request_stop();
    if (timeStampTimer_.joinable())
        timeStampTimer_.join();
    timeStampBundle_ = nullptr;

    gpu_->Shutdown();

    {
        std::lock_guard lock(titleTerminateMutex_);
        titleTerminateNotifications_.clear();
    }

    {
        std::scoped_lock lock(criticalSectionMutex_);
        criticalSectionWaiters_.clear();
    }

    for (auto& process : processes_) {
        for (auto& thread : process->threads_)
            FreeGuestThread(*thread);
        memory_.FreeVirtual(process->guestAddress_);
    }

    processes_.clear();
    imports_.ClearVariables();

    if (executableModule_)
        memory_.FreeVirtual(executableModule_);

    if (executableHeader_)
        memory_.FreeVirtual(executableHeader_);

    executableModule_ = 0;
    executableHeader_ = 0;
    executableSystemFlags_ = 0;
    executableTLSSlots_ = KProcess::TLS_SLOT_COUNT;
    executableTLSSize_ = 0;
    executableTLSRawSize_ = 0;
    executableTLSTemplate_ = 0;
}

void Kernel::UpdateTimeStampBundle() {
    using ClockUnits = std::chrono::duration<uint64_t, std::ratio<1, 10000000>>;
    const uint64_t elapsed
        = std::chrono::duration_cast<ClockUnits>(std::chrono::steady_clock::now() - clockStart_).count();
    hle::krnl::UpdateTimeStampBundle(*timeStampBundle_, elapsed, systemTimeStart_ + elapsed);
}

bool Kernel::SetExecutableModule(const XLoader::IImage& image, std::string_view imagePath) {
    std::scoped_lock objectsLock(threadObjectsMutex_, tlsMutex_);
    if (executableModule_ || !image.getMemorySize() || image.getMemorySize() > UINT32_MAX)
        return false;

    for (const auto& process : processes_)
        if (process->type() == ProcessType::Title && !process->threads_.empty())
            return false;

    const auto header = image.getHeaderData();
    const GuestAddress recordAddress = memory_.AllocateVirtual(sizeof(GuestModule));
    if (!recordAddress)
        return false;

    const GuestAddress headerAddress = header.empty() ? 0 : memory_.AllocateVirtual(header.size());
    if (!header.empty() && !headerAddress) {
        memory_.FreeVirtual(recordAddress);
        return false;
    }

    uint32_t systemFlags = 0;
    uint32_t tlsSlots = KProcess::TLS_SLOT_COUNT;
    uint32_t tlsSize = 0, tlsRawSize = 0;
    GuestAddress tlsTemplate = 0;
    try {
        if (headerAddress)
            std::memcpy(memory_.Translate(headerAddress, header.size()), header.data(), header.size());

        if (!header.empty()) {
            if (header.size() < 0x18)
                throw std::invalid_argument("truncated executable xex header");

            const auto word = [&](size_t offset) {
                uint32_t value;
                std::memcpy(&value, header.data() + offset, sizeof(value));
                return byte_swap(value);
            };

            const uint64_t tableEnd = 0x18 + uint64_t(word(0x14)) * 8;
            if (word(0) != 0x58455832 || tableEnd > header.size() || tableEnd > word(8)
                || word(8) > header.size())
                throw std::invalid_argument("invalid executable xex optional header table");

            for (size_t offset = 0x18; offset < tableEnd; offset += 8) {
                if (word(offset) == XLoader::SystemFlags) {
                    systemFlags = word(offset + 4);
                } else if (word(offset) == XLoader::TLSInfo) {
                    const uint32_t tlsOffset = word(offset + 4);
                    if (tlsOffset < tableEnd || uint64_t(tlsOffset) + 16 > word(8))
                        throw std::invalid_argument("invalid executable tls header offset");

                    const uint32_t slots = word(tlsOffset);

                    if (slots) {
                        const uint64_t paddedSlots = (uint64_t(slots) + 3) & ~uint64_t(3);

                        if (paddedSlots * 4 > UINT16_MAX)
                            throw std::invalid_argument("executable tls slot storage is too large");

                        tlsSlots = static_cast<uint32_t>(paddedSlots);
                        tlsTemplate = word(tlsOffset + 4);
                        tlsSize = word(tlsOffset + 8);
                        tlsRawSize = word(tlsOffset + 12);

                        if (tlsRawSize > tlsSize || uint64_t(tlsSize) + tlsSlots * 4 > UINT32_MAX
                            || (tlsRawSize && !memory_.IsAccessible(tlsTemplate, tlsRawSize)))
                            throw std::invalid_argument("invalid executable tls template");
                    }
                }
            }
        }

        GuestModule record{};
        record.imageBase = image.getBaseAddress();
        record.imageSize = static_cast<uint32_t>(image.getMemorySize());
        record.fullImageSize = record.imageSize;
        record.entryPoint = image.getEntryPoint();
        record.loadCount = 1;
        record.xexHeaderBase = headerAddress;
        std::memcpy(memory_.Translate(recordAddress, sizeof(record)), &record, sizeof(record));

        if (!imagePath.empty())
            hle::krnl::UpdateCommandLine(imports_, imagePath);
        imports_.UpdateVariable<uint32_t>(XboxLibrary::XboxKrnl, "XexExecutableModuleHandle", recordAddress);
    } catch (const std::exception& error) {
        LOG_ERROR("Unable to publish executable module: {}", error.what());
        memory_.FreeVirtual(recordAddress);

        if (headerAddress)
            memory_.FreeVirtual(headerAddress);

        return false;
    }

    executableModule_ = recordAddress;
    executableHeader_ = headerAddress;
    executableSystemFlags_ = systemFlags;
    executableTLSSlots_ = tlsSlots;
    executableTLSSize_ = tlsSize;
    executableTLSRawSize_ = tlsRawSize;
    executableTLSTemplate_ = tlsTemplate;

    for (auto& process : processes_) {
        if (process->type() == ProcessType::Title) {
            process->tlsSlotCount_ = std::min<uint32_t>(tlsSlots, KProcess::TLS_SLOT_COUNT);
            InitializeProcessTLS(*process->guestProcess_, tlsSlots, tlsSize, tlsRawSize, tlsTemplate);
        }
    }

    return true;
}

KProcess* Kernel::CreateGuestProcess(const ProcessCreateInfo& info) {
    std::scoped_lock lock(threadObjectsMutex_);
    return CreateGuestProcessLocked(info);
}

KProcess* Kernel::GetThreadProcess(bool system) {
    std::scoped_lock lock(threadObjectsMutex_);

    const auto type = system ? ProcessType::System : ProcessType::Title;
    for (const auto& process : processes_)
        if (process->type() == type)
            return process.get();

    if (!system)
        return nullptr;

    ProcessCreateInfo info;
    info.type = ProcessType::System;

    return CreateGuestProcessLocked(info);
}

KProcess* Kernel::CreateGuestProcessLocked(const ProcessCreateInfo& info) {
    auto process = std::make_unique<KProcess>(next_process_id_++, info.type);

    const GuestAddress address = memory_.AllocateVirtual(sizeof(GuestKernelProcess));

    if (!address)
        return nullptr;

    try {
        process->guestAddress_ = address;
        process->memory_ = &memory_;
        process->guestProcess_
            = static_cast<GuestKernelProcess*>(memory_.Translate(address, sizeof(GuestKernelProcess)));

        auto& record = *process->guestProcess_;
        record = {};

        InitializeList(record.threadList, address + offsetof(GuestKernelProcess, threadList));
        InitializeList(record.reservedList, address + offsetof(GuestKernelProcess, reservedList));

        record.quantum = byte_swap(uint32_t(6));
        record.processType = static_cast<uint8_t>(info.type);
        record.maxDynamicPriority = 15;
        record.kernelStackSize = byte_swap(uint32_t(16 * 1024));

        const bool title = info.type == ProcessType::Title;
        const uint32_t slots = title ? executableTLSSlots_ : uint32_t(KProcess::TLS_SLOT_COUNT);

        process->tlsSlotCount_ = std::min<uint32_t>(slots, KProcess::TLS_SLOT_COUNT);
        InitializeProcessTLS(record, slots, title ? executableTLSSize_ : 0, title ? executableTLSRawSize_ : 0,
                             title ? executableTLSTemplate_ : 0);

        process->mImageBase_ = info.image_base;
        process->mEntryPoint_ = info.entry_point;

        KProcess* result = process.get();

        processes_.push_back(std::move(process));

        return result;
    } catch (...) {
        memory_.FreeVirtual(address);
        throw;
    }
}

bool Kernel::AllocateThreadStack(KThread& thread, uint32_t size) {
    if (size == 0 || (uint64_t)size + 4095 > UINT32_MAX)
        return false;

    size = AlignUp(size, 0x1000);
    const uint32_t base = memory_.AllocateVirtual(size);

    if (!base) {
        LOG_ERROR("Unable to allocate guest thread stack");
        return false;
    }

    thread.mStackLimit = base;
    thread.mStackBase = base + size;
    return true;
}

void Kernel::InitializeThreadContext(KThread& thread, const ThreadCreateInfo& info) {
    thread.mContext = {};
    thread.mContext.HostThread = &thread;

    thread.mContext.CIA = info.startup ? info.startup : info.entry_point;
    thread.mContext.NIA = thread.mContext.CIA;
    thread.mContext.SPRs.LR = KThread::kReturnAddress;
    thread.mReturnValueIsExitCode = !info.startup;

    // initialize the guest stack, entry parameter and processor control region
    thread.mContext.GPRs[1].u64 = thread.mStackBase - 0x100;
    thread.mContext.GPRs[3].u64 = info.startup ? info.entry_point : info.parameter;
    thread.mContext.GPRs[4].u64 = info.startup ? info.parameter : 0;
    thread.mContext.GPRs[13].u64 = thread.pcrAddress_;

    thread.mAffinityMask = info.affinity_mask;
    thread.mPriority = info.priority;
    thread.mState = ThreadState::Created;
}

KThread* Kernel::CreateThread(KProcess* process, const ThreadCreateInfo& info) {
    std::scoped_lock lock(threadObjectsMutex_, tlsMutex_);
    if (!process || !process->guestProcess_ || process->terminated() || !info.affinity_mask
        || (info.affinity_mask & ~kAllProcessors) || info.priority < 0 || info.priority > 31)
        return nullptr;

    auto thread = std::make_unique<KThread>(next_thread_id_++, process);

    try {
        if (!AllocateThreadStack(*thread, info.stack_size) || !InitializeGuestThread(*thread, info)) {
            FreeGuestThread(*thread);
            return nullptr;
        }

        InitializeThreadContext(*thread, info);
        thread->mState = info.create_suspended ? ThreadState::Suspended : ThreadState::Created;
        thread->suspend_count = info.create_suspended ? 1 : 0;
        thread->SyncGuestState();

        thread->handleProcess_ = info.handle_process ? info.handle_process : process;
        thread->handle_ = thread->handleProcess_->handles.Insert(thread.get());

        std::scoped_lock guestListLock(process->guestThreadsMutex_);
        auto& list = process->guestProcess_->threadList;
        auto* tail = static_cast<GuestListEntry*>(
            memory_.Translate(byte_swap(list.previous), sizeof(GuestListEntry)));
        KThread* result = thread.get();
        process->threads_.push_back(std::move(thread));

        // link only after all allocations and host ownership have succeeded
        const GuestAddress entry = result->guestAddress_ + offsetof(GuestKernelThread, processEntry);
        result->guestThread_->processEntry.next = byte_swap(
            static_cast<GuestAddress>(process->guestAddress_ + offsetof(GuestKernelProcess, threadList)));
        result->guestThread_->processEntry.previous = list.previous;

        tail->next = byte_swap(entry);
        list.previous = byte_swap(entry);

        process->guestProcess_->threadCount = byte_swap(byte_swap(process->guestProcess_->threadCount) + 1);
        result->guestProcessEntryLinked_ = true;

        return result;

    } catch (...) {
        if (thread)
            FreeGuestThread(*thread);
        throw;
    }
}

bool Kernel::InitializeGuestThread(KThread& thread, const ThreadCreateInfo& info) {
    thread.guestAddress_ = memory_.AllocateVirtual(sizeof(GuestKernelThread));
    thread.pcrAddress_ = memory_.AllocateVirtual(sizeof(GuestProcessorRegion));

    const auto& process = *thread.process()->guestProcess_;
    const uint32_t staticSize = byte_swap(process.tlsDataSize);
    const uint32_t slotSize = byte_swap(process.tlsSlotSize);
    const uint32_t rawSize = byte_swap(process.tlsRawDataSize);

    if (uint64_t(staticSize) + slotSize > UINT32_MAX || slotSize < thread.process()->tlsSlotCount_ * 4
        || rawSize > staticSize
        || (rawSize && !memory_.IsAccessible(byte_swap(process.tlsTemplate), rawSize)))
        return false;

    thread.tlsAllocation_ = memory_.AllocateVirtual(uint64_t(staticSize) + slotSize);
    if (!thread.guestAddress_ || !thread.pcrAddress_ || !thread.tlsAllocation_)
        return false;

    thread.tls_address = thread.tlsAllocation_ + staticSize;
    std::memset(memory_.Translate(thread.tlsAllocation_, uint64_t(staticSize) + slotSize), 0,
                uint64_t(staticSize) + slotSize);

    if (rawSize)
        std::memcpy(memory_.Translate(thread.tlsAllocation_, rawSize),
                    memory_.Translate(byte_swap(process.tlsTemplate), rawSize), rawSize);

    thread.guestThread_
        = static_cast<GuestKernelThread*>(memory_.Translate(thread.guestAddress_, sizeof(GuestKernelThread)));

    thread.guestPCR_ = static_cast<GuestProcessorRegion*>(
        memory_.Translate(thread.pcrAddress_, sizeof(GuestProcessorRegion)));

    auto& record = *thread.guestThread_;
    record = {};
    record.header.type = 6;

    InitializeList(record.header.waitList,
                   thread.guestAddress_ + offsetof(GuestKernelThread, header.waitList));
    InitializeList(record.mutants, thread.guestAddress_ + offsetof(GuestKernelThread, mutants));
    InitializeList(record.waitTimer.header.waitList,
                   thread.guestAddress_ + offsetof(GuestKernelThread, waitTimer.header.waitList));
    InitializeList(record.waitTimer.tableEntry,
                   thread.guestAddress_ + offsetof(GuestKernelThread, waitTimer.tableEntry));
    InitializeList(record.timeoutWait.waitList,
                   thread.guestAddress_ + offsetof(GuestKernelThread, waitTimer.header.waitList));

    record.timeoutWait.thread = byte_swap(thread.guestAddress_);
    record.timeoutWait.object
        = byte_swap(static_cast<GuestAddress>(thread.guestAddress_ + offsetof(GuestKernelThread, waitTimer)));
    record.timeoutWait.result = byte_swap(uint16_t(0x102));
    record.timeoutWait.waitType = byte_swap(uint16_t(1));
    record.stackBase = byte_swap(thread.mStackBase);
    record.stackLimit = byte_swap(thread.mStackLimit);
    record.kernelStack = byte_swap(thread.mStackBase - 240);
    record.stackAllocation = byte_swap(thread.mStackLimit);
    record.tlsAddress = byte_swap(thread.tls_address);
    record.priority = record.basePriority = record.basePriorityCopy = static_cast<uint8_t>(info.priority);
    record.processType = record.processTypeCopy = process.processType;

    for (size_t i = 0; i < 2; ++i)
        InitializeList(record.apcLists[i], thread.guestAddress_ + offsetof(GuestKernelThread, apcLists)
                                               + i * sizeof(GuestListEntry));
    record.process = byte_swap(thread.process()->guest_address());
    record.msrMask = byte_swap(uint32_t(0xFDFFD7FF));
    record.quantum = process.quantum;
    record.priorityClass = process.priorityClass;
    record.maxDynamicPriority = process.maxDynamicPriority;
    record.prcb = record.alternatePRCB
        = byte_swap(static_cast<GuestAddress>(thread.pcrAddress_ + offsetof(GuestProcessorRegion, prcbData)));
    using Units = std::chrono::duration<uint64_t, std::ratio<1, 10000000>>;
    record.createTime = byte_swap(
        systemTimeStart_
        + std::chrono::duration_cast<Units>(std::chrono::steady_clock::now() - clockStart_).count());
    record.threadID = byte_swap(thread.id());
    record.startAddress = byte_swap(info.entry_point);
    record.creationFlags = byte_swap(info.creation_flags | uint32_t(info.create_suspended));

    InitializeList(record.timerList, thread.guestAddress_ + offsetof(GuestKernelThread, timerList));
    InitializeList(record.reservedList, thread.guestAddress_ + offsetof(GuestKernelThread, reservedList));
    InitializeList(record.queueEntry, thread.guestAddress_ + offsetof(GuestKernelThread, queueEntry));
    InitializeList(record.readyEntry, thread.guestAddress_ + offsetof(GuestKernelThread, readyEntry));
    InitializeList(record.suspendSemaphore.waitList,
                   thread.guestAddress_ + offsetof(GuestKernelThread, suspendSemaphore.waitList));
    record.suspendSemaphore.type = 5;
    record.suspendSemaphoreLimit = byte_swap(uint32_t(1));

    auto& pcr = *thread.guestPCR_;
    pcr = {};
    pcr.tlsAddress = byte_swap(thread.tlsAllocation_);
    pcr.msrMask = record.msrMask;
    pcr.self = byte_swap(uint64_t(thread.pcrAddress_));
    pcr.stackBase = record.stackBase;
    pcr.stackLimit = record.stackLimit;
    pcr.prcb = record.prcb;
    pcr.prcbData.currentThread = byte_swap(thread.guestAddress_);
    const auto cpu = static_cast<uint8_t>(std::countr_zero(info.affinity_mask));
    pcr.prcbData.currentCPU = record.currentCPU = cpu;
    pcr.prcbData.processorMask = byte_swap(uint32_t(1) << cpu);

    InitializeList(pcr.prcbData.queuedDPCs,
                   thread.pcrAddress_ + offsetof(GuestProcessorRegion, prcbData.queuedDPCs));
    InitializeList(pcr.prcbData.terminatingThreads,
                   thread.pcrAddress_ + offsetof(GuestProcessorRegion, prcbData.terminatingThreads));

    for (size_t i = 0; i < 32; ++i)
        InitializeList(pcr.prcbData.readyLists[i], thread.pcrAddress_
                                                       + offsetof(GuestProcessorRegion, prcbData.readyLists)
                                                       + i * sizeof(GuestListEntry));

    return true;
}

void Kernel::FreeGuestThread(KThread& thread) {
    if (thread.handle_ && thread.handleProcess_)
        thread.handleProcess_->handles.Remove(thread.handle_);

    thread.handle_ = 0;
    for (auto address :
         {thread.mStackLimit, thread.guestAddress_, thread.pcrAddress_, thread.tlsAllocation_}) {
        if (address)
            memory_.FreeVirtual(address);
    }

    thread.guestThread_ = nullptr;
    thread.guestPCR_ = nullptr;
    thread.guestAddress_ = thread.pcrAddress_ = thread.tlsAllocation_ = thread.tls_address = 0;
    thread.mStackLimit = thread.mStackBase = 0;
}

void Kernel::StartThread(KThread* thread) {
    if (!thread)
        return;

    // add to queue
    scheduler_.MakeRunnable(thread);
}

KThread* Kernel::CreateInitialThread(KProcess* process) {
    ThreadCreateInfo info{};

    info.entry_point = process->mEntryPoint_;
    info.parameter = 0;
    info.stack_size = 1024 * 1024;

    info.affinity_mask = 1 << 0;
    info.priority = 0;
    info.create_suspended = false;

    return CreateThread(process, info);
}

bool Kernel::EnterCriticalSection(KThread& thread, GuestAddress address, bool tryOnly) {
    return EnterCriticalSection(thread, hle::Pointer<hle::krnl::CriticalSection>(memory_, address), tryOnly);
}

bool Kernel::EnterCriticalSection(KThread& thread, hle::Pointer<hle::krnl::CriticalSection> section,
                                  bool tryOnly) {
    if (!thread.guest_address())
        throw std::invalid_argument("critical section owner must have a guest kthread record");

    std::scoped_lock lock(criticalSectionMutex_);
    if (section->type != 1)
        throw std::invalid_argument("critical section has an invalid object type");
    const GuestAddress address = section.guest_address();

    const uint32_t owner = section->owningThread;
    const uint32_t recursion = section->recursionCount;
    const uint32_t count = section->lockCount;

    if (owner == 0 && recursion == 0 && count == UINT32_MAX) {
        section->lockCount = 0;
        section->recursionCount = 1;
        section->owningThread = thread.guest_address();
        return true;
    }

    if (!owner || !recursion || count > INT32_MAX || recursion > uint64_t(count) + 1)
        throw std::invalid_argument("critical section has inconsistent ownership state");

    if (owner == thread.guest_address()) {
        if (recursion == INT32_MAX || count == INT32_MAX)
            throw std::overflow_error("critical section recursion overflow");

        section->lockCount = count + 1;
        section->recursionCount = recursion + 1;
        return true;
    }

    if (tryOnly)
        return false;

    if (count == INT32_MAX)
        throw std::overflow_error("critical section waiter count overflow");

    auto& waiters = criticalSectionWaiters_[address];
    waiters.push_back(&thread);

    if (!scheduler_.PrepareWait(&thread)) {
        waiters.pop_back();

        if (waiters.empty())
            criticalSectionWaiters_.erase(address);

        throw std::runtime_error("unable to prepare critical section wait");
    }

    section->lockCount = count + 1;
    return false;
}

void Kernel::LeaveCriticalSection(KThread& thread, GuestAddress address) {
    LeaveCriticalSection(thread, hle::Pointer<hle::krnl::CriticalSection>(memory_, address));
}

void Kernel::LeaveCriticalSection(KThread& thread, hle::Pointer<hle::krnl::CriticalSection> section) {
    std::scoped_lock lock(criticalSectionMutex_);
    if (section->type != 1)
        throw std::invalid_argument("critical section has an invalid object type");
    const GuestAddress address = section.guest_address();
    const uint32_t owner = section->owningThread;

    uint32_t recursion = section->recursionCount;
    uint32_t count = section->lockCount;

    if (!thread.guest_address() || owner != thread.guest_address() || !recursion || count > INT32_MAX
        || recursion > uint64_t(count) + 1)
        throw std::invalid_argument("critical section release by a nonowner or invalid recursion state");

    auto waiters = criticalSectionWaiters_.find(address);
    const size_t waiterCount = waiters == criticalSectionWaiters_.end() ? 0 : waiters->second.size();
    if (uint64_t(count) + 1 - recursion != waiterCount)
        throw std::invalid_argument("critical section waiter state does not match the kernel queue");

    --recursion;
    --count;
    section->lockCount = count;
    section->recursionCount = recursion;

    if (recursion)
        return;

    section->owningThread = 0;

    if (waiters == criticalSectionWaiters_.end())
        return;

    while (!waiters->second.empty()) {
        KThread* next = waiters->second.front();
        waiters->second.pop_front();
        // publish ownership before a released worker can resume the waiting thread
        section->owningThread = next->guest_address();
        section->recursionCount = 1;

        if (scheduler_.WakeThread(next)) {
            if (waiters->second.empty())
                criticalSectionWaiters_.erase(waiters);

            return;
        }
        // remove the acquisition count of a waiter that was terminated before handoff
        section->owningThread = 0;
        section->recursionCount = 0;
        section->lockCount = --count;
    }

    criticalSectionWaiters_.erase(waiters);
}

KThread* Kernel::CurrentThread() {
    return scheduler_.CurrentThread();
}

KProcess* Kernel::CurrentProcess() {
    KThread* thread = CurrentThread();

    if (!thread)
        return nullptr;

    return thread->process();
}

void Kernel::SetGraphicsInterruptCallback(GuestAddress routine, GuestAddress userData) {
    gpu_->SetInterruptCallback(routine, userData);
}

Kernel::GraphicsInterruptCallback Kernel::GetGraphicsInterruptCallback() const {
    return gpu_->GetInterruptCallback();
}

void Kernel::RegisterTitleTerminateNotification(GuestAddress routine, uint32_t priority) {
    std::lock_guard lock(titleTerminateMutex_);
    titleTerminateNotifications_.push_back({routine, priority});
}

void Kernel::RemoveTitleTerminateNotification(GuestAddress routine) {
    std::lock_guard lock(titleTerminateMutex_);
    const auto found
        = std::find_if(titleTerminateNotifications_.begin(), titleTerminateNotifications_.end(),
                       [routine](const auto& notification) { return notification.routine == routine; });
    if (found != titleTerminateNotifications_.end())
        titleTerminateNotifications_.erase(found);
}

std::vector<Kernel::TitleTerminateNotification> Kernel::GetTitleTerminateNotifications() const {
    std::lock_guard lock(titleTerminateMutex_);
    return titleTerminateNotifications_;
}

void Kernel::ExitThread(KThread* thread, uint32_t exit_code) {
    if (!thread)
        return;

    scheduler_.TerminateThread(thread, exit_code);
}

void Kernel::ExitProcess(KProcess* process, uint32_t exit_code) {
    std::scoped_lock lock(threadObjectsMutex_);
    if (!process)
        return;

    process->terminated_ = true;
    process->guestProcess_->terminating = 1;

    process->exit_code_ = exit_code;

    for (auto& thread : process->threads_) {
        scheduler_.TerminateThread(thread.get(), exit_code);
    }
}

uint32_t Kernel::AllocateTLS(KThread& thread) {
    std::lock_guard lock(tlsMutex_);
    auto* process = thread.process();

    if (!process || !process->guestProcess_ || !thread.tls_address)
        return UINT32_MAX;
    for (uint32_t index = 0; index < process->tlsSlotCount_; ++index) {
        auto& bitmap = process->guestProcess_->tlsSlotBitmap[index / 32];
        const uint32_t bit = 0x80000000u >> (index % 32);
        if (byte_swap(bitmap) & bit) {
            bitmap = byte_swap(byte_swap(bitmap) & ~bit);
            uint32_t zero = 0;
            std::memcpy(memory_.Translate(thread.tls_address + index * 4, 4), &zero, 4);

            return static_cast<uint32_t>(index);
        }
    }

    return UINT32_MAX;
}

bool Kernel::FreeTLS(KThread& thread, uint32_t index) {
    std::lock_guard lock(tlsMutex_);

    auto* process = thread.process();

    if (!process || !process->guestProcess_
        || !TLSAllocated(*process->guestProcess_, index, process->tlsSlotCount_))
        return false;

    for (auto& processThread : process->threads_) {
        uint32_t zero = 0;
        std::memcpy(memory_.Translate(processThread->tls_address + index * 4, 4), &zero, 4);
    }
    auto& bitmap = process->guestProcess_->tlsSlotBitmap[index / 32];
    bitmap = byte_swap(byte_swap(bitmap) | (0x80000000u >> (index % 32)));

    return true;
}

uint32_t Kernel::GetTLSValue(KThread& thread, uint32_t index) {
    std::lock_guard lock(tlsMutex_);

    const auto* process = thread.process();
    if (!process || !process->guestProcess_ || !thread.tls_address
        || !TLSAllocated(*process->guestProcess_, index, process->tlsSlotCount_))
        return 0;

    uint32_t value;
    std::memcpy(&value, memory_.Translate(thread.tls_address + index * 4, 4), 4);
    return byte_swap(value);
}

bool Kernel::SetTLSValue(KThread& thread, uint32_t index, uint32_t value) {
    std::lock_guard lock(tlsMutex_);

    const auto* process = thread.process();
    if (!process || !process->guestProcess_ || !thread.tls_address
        || !TLSAllocated(*process->guestProcess_, index, process->tlsSlotCount_))
        return false;

    value = byte_swap(value);
    std::memcpy(memory_.Translate(thread.tls_address + index * 4, 4), &value, 4);
    return true;
}
