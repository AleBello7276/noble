#pragma once

#include <functional>
namespace debugger {
class Debugger;
}
namespace diagnostics {
class TraceStore;
}
namespace tui {
void RunDebuggerView(debugger::Debugger& debugger, diagnostics::TraceStore& trace,
                     const std::function<void()>& requestStop);
}
