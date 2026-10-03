#include "KProcess.h"
#include "KThread.h"

KProcess::KProcess(uint32_t id, ProcessType type)
    : KernelObject(KernelObjectType::KProcess), id_(id), type_(type) {}

KProcess::~KProcess() = default;
