#include "Kernel.h"

#include "GuestModule.h"
#include "Loader/XEXImage.h"
#include "Logger.h"
#include "core/byte_swap.h"
#include "cpu/Scheduler.h"
#include "emulator/Memory.h"
#include "hle/Exports.h"
#include "hle/krnl/Variables.h"
#include <algorithm>
#include <cstring>

// require an aligned writable guest critical section before accessing its shared state
hle::krnl::CriticalSection* CheckedCriticalSection(Memory& memory, GuestAddress address) {
    if (address % alignof(hle::krnl::CriticalSection)
        || !memory.IsAccessible(address, sizeof(hle::krnl::CriticalSection), true))
        throw std::out_of_range("critical section is not in aligned writable guest memory");

    auto* section = static_cast<hle::krnl::CriticalSection*>(
        memory.Translate(address, sizeof(hle::krnl::CriticalSection)));

    if (section->type != 1)
        throw std::invalid_argument("critical section has an invalid object type");

    return section;
}

Kernel::Kernel(Memory& memory, Scheduler& scheduler)
    : memory_(memory), scheduler_(scheduler), imports_(*this, memory) {}

bool Kernel::Initialize() {
    if (timeStampTimer_.joinable())
        return false;

    next_process_id_ = 1;
    next_thread_id_ = 1;

    try {
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
        return false;
    }

    return true;
}

void Kernel::Shutdown() {
    timeStampTimer_.request_stop();
    if (timeStampTimer_.joinable())
        timeStampTimer_.join();
    timeStampBundle_ = nullptr;

    {
        std::scoped_lock lock(criticalSectionMutex_);
        criticalSectionWaiters_.clear();
    }

    for (auto& process : processes_)
        for (auto& thread : process->threads_)
            if (thread->mStackLimit)
                memory_.FreeVirtual(thread->mStackLimit);

    processes_.clear();
    imports_.ClearVariables();

    if (executableModule_)
        memory_.FreeVirtual(executableModule_);

    if (executableHeader_)
        memory_.FreeVirtual(executableHeader_);

    executableModule_ = 0;
    executableHeader_ = 0;
    executableSystemFlags_ = 0;
}

void Kernel::UpdateTimeStampBundle() {
    using ClockUnits = std::chrono::duration<uint64_t, std::ratio<1, 10000000>>;
    const uint64_t elapsed
        = std::chrono::duration_cast<ClockUnits>(std::chrono::steady_clock::now() - clockStart_).count();
    hle::krnl::UpdateTimeStampBundle(*timeStampBundle_, elapsed, systemTimeStart_ + elapsed);
}

bool Kernel::SetExecutableModule(const XLoader::IImage& image, std::string_view imagePath) {
    if (executableModule_ || !image.getMemorySize() || image.getMemorySize() > UINT32_MAX)
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
                    break;
                }
            }
        }

        GuestModule record{};
        record.imageBase = byte_swap(image.getBaseAddress());
        record.imageSize = byte_swap(static_cast<uint32_t>(image.getMemorySize()));
        record.fullImageSize = record.imageSize;
        record.entryPoint = byte_swap(image.getEntryPoint());
        record.loadCount = byte_swap(uint16_t(1));
        record.xexHeaderBase = byte_swap(headerAddress);
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
    return true;
}

KProcess* Kernel::CreateGuestProcess(const ProcessCreateInfo& info) {
    auto process = std::make_unique<KProcess>(next_process_id_++, info.type);

    process->mImageBase_ = info.image_base;
    process->mEntryPoint_ = info.entry_point;

    KProcess* result = process.get();

    processes_.push_back(std::move(process));

    return result;
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

    thread.mContext.CIA = info.entry_point;

    /* PPC stack pointer = r1 */
    thread.mContext.GPRs[1].u64 = thread.mStackBase;
    // thread.mContext.GPRs[3].u64 = info.parameter;

    // TODO: arguments loading

    thread.mAffinityMask = info.affinity_mask;
    thread.mPriority = info.priority;
    thread.mState = ThreadState::Created;
}

KThread* Kernel::CreateThread(KProcess* process, const ThreadCreateInfo& info) {
    if (!process)
        return nullptr;

    auto thread = std::make_unique<KThread>(next_thread_id_++, process);

    if (!AllocateThreadStack(*thread, info.stack_size)) {
        return nullptr;
    }

    InitializeThreadContext(*thread, info);

    thread->mState = info.create_suspended ? ThreadState::Suspended : ThreadState::Created;

    KThread* result = thread.get();
    process->threads_.push_back(std::move(thread));

    return result;
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
    if (!thread.id())
        throw std::invalid_argument("critical section owner must have a nonzero thread id");

    std::scoped_lock lock(criticalSectionMutex_);
    auto* section = CheckedCriticalSection(memory_, address);

    const uint32_t owner = byte_swap(section->owningThread);
    const uint32_t recursion = byte_swap(section->recursionCount);
    const uint32_t count = byte_swap(section->lockCount);

    if (owner == 0 && recursion == 0 && count == UINT32_MAX) {
        section->lockCount = 0;
        section->recursionCount = byte_swap(uint32_t(1));
        // use noble thread ids until the kernel exposes guest kthread objects
        section->owningThread = byte_swap(thread.id());
        return true;
    }

    if (!owner || !recursion || count > INT32_MAX || recursion > uint64_t(count) + 1)
        throw std::invalid_argument("critical section has inconsistent ownership state");

    if (owner == thread.id()) {
        if (recursion == INT32_MAX || count == INT32_MAX)
            throw std::overflow_error("critical section recursion overflow");

        section->lockCount = byte_swap(count + 1);
        section->recursionCount = byte_swap(recursion + 1);
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

    section->lockCount = byte_swap(count + 1);
    return false;
}

void Kernel::LeaveCriticalSection(KThread& thread, GuestAddress address) {
    std::scoped_lock lock(criticalSectionMutex_);
    auto* section = CheckedCriticalSection(memory_, address);
    const uint32_t owner = byte_swap(section->owningThread);

    uint32_t recursion = byte_swap(section->recursionCount);
    uint32_t count = byte_swap(section->lockCount);

    if (!thread.id() || owner != thread.id() || !recursion || count > INT32_MAX
        || recursion > uint64_t(count) + 1)
        throw std::invalid_argument("critical section release by a nonowner or invalid recursion state");

    auto waiters = criticalSectionWaiters_.find(address);
    const size_t waiterCount = waiters == criticalSectionWaiters_.end() ? 0 : waiters->second.size();
    if (uint64_t(count) + 1 - recursion != waiterCount)
        throw std::invalid_argument("critical section waiter state does not match the kernel queue");

    --recursion;
    --count;
    section->lockCount = byte_swap(count);
    section->recursionCount = byte_swap(recursion);

    if (recursion)
        return;

    section->owningThread = 0;

    if (waiters == criticalSectionWaiters_.end())
        return;

    while (!waiters->second.empty()) {
        KThread* next = waiters->second.front();
        waiters->second.pop_front();
        // publish ownership before a released worker can resume the waiting thread
        section->owningThread = byte_swap(next->id());
        section->recursionCount = byte_swap(uint32_t(1));

        if (scheduler_.WakeThread(next)) {
            if (waiters->second.empty())
                criticalSectionWaiters_.erase(waiters);

            return;
        }
        // remove the acquisition count of a waiter that was terminated before handoff
        section->owningThread = 0;
        section->recursionCount = 0;
        section->lockCount = byte_swap(--count);
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

void Kernel::ExitThread(KThread* thread, uint32_t exit_code) {
    if (!thread)
        return;

    scheduler_.TerminateThread(thread, exit_code);
}

void Kernel::ExitProcess(KProcess* process, uint32_t exit_code) {
    if (!process)
        return;

    process->terminated_ = true;

    process->exit_code_ = exit_code;

    for (auto& thread : process->threads_) {
        scheduler_.TerminateThread(thread.get(), exit_code);
    }
}

uint32_t Kernel::AllocateTLS(KThread& thread) {
    std::lock_guard lock(tlsMutex_);
    auto* process = thread.process();

    for (size_t index = 0; index < KProcess::TLS_SLOT_COUNT; ++index) {
        if (!process->tlsSlots_.test(index)) {
            process->tlsSlots_.set(index);
            thread.mTlsValues[index] = 0;

            return static_cast<uint32_t>(index);
        }
    }

    return UINT32_MAX;
}

bool Kernel::FreeTLS(KThread& thread, uint32_t index) {
    std::lock_guard lock(tlsMutex_);

    auto* process = thread.process();

    if (index >= KProcess::TLS_SLOT_COUNT || !process->tlsSlots_.test(index))
        return false;

    for (auto& processThread : process->threads_)
        processThread->mTlsValues[index] = 0;

    process->tlsSlots_.reset(index);

    return true;
}

uint32_t Kernel::GetTLSValue(KThread& thread, uint32_t index) {
    std::lock_guard lock(tlsMutex_);

    if (index >= KProcess::TLS_SLOT_COUNT || !thread.process()->tlsSlots_.test(index))
        return 0;

    return thread.mTlsValues[index];
}

bool Kernel::SetTLSValue(KThread& thread, uint32_t index, uint32_t value) {
    std::lock_guard lock(tlsMutex_);

    if (index >= KProcess::TLS_SLOT_COUNT || !thread.process()->tlsSlots_.test(index))
        return false;

    thread.mTlsValues[index] = value;
    return true;
}
