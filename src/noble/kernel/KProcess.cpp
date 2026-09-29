#include "KProcess.h"
#include "KThread.h"

KProcess::KProcess(uint32_t id) : KernelObject(KernelObjectType::KProcess), id_(id) {}

KProcess::~KProcess() = default;
