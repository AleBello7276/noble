#pragma once

#include "core/endian.h"
#include <cstddef>
#include <cstdint>

// xbox kernel memory layouts with guest pointers and multibyte scalars stored big endian
struct GuestListEntry {
    uint32_t next;
    uint32_t previous;
};

struct GuestDispatchHeader {
    uint8_t type, absolute, size, inserted;
    uint32_t signalState;
    GuestListEntry waitList;
};

struct GuestKernelProcess {
    uint32_t threadListLock;
    GuestListEntry threadList;
    uint32_t quantum;
    uint32_t clrData;
    uint32_t threadCount;
    uint8_t priorityClass, defaultPriority, maxDynamicPriority, disableQuantumDecay;
    uint32_t kernelStackSize;
    uint32_t tlsTemplate;
    uint32_t tlsDataSize;
    uint32_t tlsRawDataSize;
    uint16_t tlsSlotSize;
    uint8_t terminating, processType;
    // the most significant bit describes the first slot and a set bit means free
    uint32_t tlsSlotBitmap[8];
    uint32_t reserved50;
    GuestListEntry reservedList;
    uint32_t reserved5C;
};

struct GuestKernelTimer {
    GuestDispatchHeader header;
    uint64_t dueTime;
    GuestListEntry tableEntry;
    uint32_t dpc, period;
};

struct GuestWaitBlock {
    GuestListEntry waitList;
    uint32_t thread, object, nextWaitBlock;
    uint16_t result, waitType;
};

struct GuestKernelThread {
    GuestDispatchHeader header;
    GuestListEntry mutants;
    GuestKernelTimer waitTimer;
    GuestWaitBlock timeoutWait;
    uint8_t reserved58[4];
    uint32_t stackBase;
    uint32_t stackLimit;
    uint32_t kernelStack;
    uint32_t tlsAddress;
    uint8_t state;
    uint8_t alerted[2];
    uint8_t alertable;
    uint8_t priority;
    uint8_t fpuExceptions;
    uint8_t processTypeCopy;
    uint8_t processType;
    GuestListEntry apcLists[2];
    uint32_t process;
    uint8_t executingKernelAPC, deferredAPC, userAPCPending, mayQueueAPCs;
    uint32_t apcLock, contextSwitches;
    GuestListEntry readyEntry;
    uint32_t msrMask, waitResult;
    uint8_t waitIRQL, processorMode, waitNext, waitReason;
    uint32_t waitBlocks, reservedAC;
    be<int32_t> apcDisableCount;
    uint32_t quantum;
    uint8_t saturationIncrement, basePriority, priorityDecrement, boostDisabled;
    uint8_t suspendCount, preempted, terminated, currentCPU;
    uint32_t prcb, alternatePRCB;
    uint8_t priorityClass, basePriorityCopy, maxDynamicPriority, reservedCB;
    uint32_t timerListLock, stackAllocation;
    uint8_t suspendAPC[0x28];
    GuestDispatchHeader suspendSemaphore;
    uint32_t suspendSemaphoreLimit;
    GuestListEntry processEntry;
    uint32_t reserved118;
    GuestListEntry queueEntry;
    uint32_t reserved124[3];
    uint64_t createTime, exitTime;
    uint32_t exitStatus;
    GuestListEntry timerList;
    uint32_t threadID, startAddress;
    GuestListEntry reservedList;
    uint32_t reserved15C, lastError, fiber, reserved168, creationFlags;
    // reserved until guest floating point and vector context save operations are implemented
    uint8_t vscr[0x10];
    uint8_t vectorContext[0x800];
    uint64_t fpscr;
    uint64_t floatingContext[32];
    uint8_t reservedAPC[0x28];
};

struct GuestProcessorControlBlock {
    uint32_t currentThread, nextThread, idleThread;
    uint8_t currentCPU, reserved0D[3];
    uint32_t processorMask;
    uint8_t reserved14[0x34];
    GuestListEntry queuedDPCs;
    uint32_t dpcActive;
    uint8_t reserved54[0x14];
    GuestListEntry readyLists[32];
    uint8_t exitDPC[0x1C];
    GuestListEntry terminatingThreads;
    uint8_t switchDPC[0x1C];
};

struct GuestProcessorRegion {
    uint32_t tlsAddress, msrMask;
    uint16_t softwareInterrupts, reserved0A;
    uint8_t dpcProcessType, timesliceEnded, timerPending, reserved0F;
    uint32_t fpuRelated, vectorRelated;
    uint8_t currentIRQL, backgroundFlags[3];
    uint32_t timerRelated;
    uint8_t reserved20[0x10];
    uint64_t self;
    uint8_t reserved38[0x34];
    uint32_t alternativeStack, stackBase, stackLimit, alternateBase, alternateLimit;
    uint32_t interruptHandlers[32];
    GuestProcessorControlBlock prcbData;
    uint32_t prcb;
    uint8_t reserved2AC[0x2C];
};

static_assert(sizeof(GuestListEntry) == 8);
static_assert(sizeof(GuestDispatchHeader) == 0x10);
static_assert(sizeof(GuestKernelProcess) == 0x60);
static_assert(offsetof(GuestKernelProcess, tlsSlotBitmap) == 0x30);
static_assert(offsetof(GuestKernelProcess, processType) == 0x2F);
static_assert(sizeof(GuestKernelTimer) == 0x28);
static_assert(sizeof(GuestWaitBlock) == 0x18);
static_assert(sizeof(GuestKernelThread) == 0xAB0);
static_assert(offsetof(GuestKernelThread, stackBase) == 0x5C);
static_assert(offsetof(GuestKernelThread, tlsAddress) == 0x68);
static_assert(offsetof(GuestKernelThread, process) == 0x84);
static_assert(offsetof(GuestKernelThread, apcDisableCount) == 0xB0);
static_assert(offsetof(GuestKernelThread, suspendCount) == 0xBC);
static_assert(offsetof(GuestKernelThread, processEntry) == 0x110);
static_assert(offsetof(GuestKernelThread, threadID) == 0x14C);
static_assert(offsetof(GuestKernelThread, lastError) == 0x160);
static_assert(sizeof(GuestProcessorControlBlock) == 0x1A8);
static_assert(sizeof(GuestProcessorRegion) == 0x2D8);
static_assert(offsetof(GuestProcessorRegion, self) == 0x30);
static_assert(offsetof(GuestProcessorRegion, stackBase) == 0x70);
static_assert(offsetof(GuestProcessorRegion, prcbData) == 0x100);
static_assert(offsetof(GuestProcessorRegion, prcb) == 0x2A8);
