#pragma once

#include "diagnostics/TraceStore.h"
#include <atomic>
#include <functional>

namespace tui {

// display recorded events and copied thread metadata until the user closes the view
void RunTraceView(diagnostics::TraceStore& store, const std::atomic_int& result,
                  const std::function<void()>& requestStop);

}  // namespace tui
