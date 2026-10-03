#include "Kernel.h"

#include "GuestModule.h"
#include "Logger.h"
#include "core/byte_swap.h"
#include "cpu/Scheduler.h"
#include "emulator/Memory.h"
#include "hle/Exports.h"
#include "hle/krnl/Variables.h"
#include <algorithm>
#include <cstring>

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

    try {
        if (headerAddress)
            std::memcpy(memory_.Translate(headerAddress, header.size()), header.data(), header.size());

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
    return true;
}

KProcess* Kernel::CreateGuestProcess(const ProcessCreateInfo& info) {
    auto process = std::make_unique<KProcess>(next_process_id_++);

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
