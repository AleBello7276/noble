#include "TraceView.h"

#include "Loader/table/ImportTable.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <thread>

namespace tui {
namespace {

// format a copied scheduler state without consulting live emulator objects
std::string_view State(const diagnostics::ThreadSnapshot& thread) {
    if (thread.faulted)
        return "faulted";
    switch (thread.state) {
    case diagnostics::ThreadStatus::Created:
        return "created";
    case diagnostics::ThreadStatus::Ready:
        return "ready";
    case diagnostics::ThreadStatus::Running:
        return "running";
    case diagnostics::ThreadStatus::Waiting:
        return "waiting";
    case diagnostics::ThreadStatus::Suspended:
        return "suspended";
    case diagnostics::ThreadStatus::Terminated:
        return "terminated";
    }
    return "unknown";
}

// format one immutable trace event for the timeline
std::string Describe(const diagnostics::Event& event) {
    using diagnostics::EventKind;
    switch (event.kind) {
    case EventKind::ThreadCreated:
        return "thread created";
    case EventKind::ThreadReady:
        return "thread ready";
    case EventKind::ThreadScheduled:
        return "thread scheduled";
    case EventKind::ThreadWaiting:
        return "thread waiting";
    case EventKind::ThreadTerminated:
        return "thread terminated";
    case EventKind::PriorityChanged:
        return std::format("priority changed: {}", event.snapshot.priority);
    case EventKind::BlockEntered:
        return std::format("block entered {:08X}", event.address);
    case EventKind::CompileStarted:
        return std::format("compiling {:08X}", event.address);
    case EventKind::CompileFinished:
        return std::format("compile {} {:08X}", event.value ? "finished" : "failed", event.address);
    case EventKind::HLEEntered:
    case EventKind::HLEReturned: {
        const auto* definition = XLoader::FindImport(static_cast<XboxLibrary>(event.auxiliary),
                                                     static_cast<uint16_t>(event.value));
        return std::format("hle {} {} [{:04X}]", event.kind == EventKind::HLEEntered ? "enter" : "return",
                           definition ? definition->name : "unknown", event.value);
    }
    case EventKind::Fault:
        return std::format("guest fault {:08X}", event.address);
    case EventKind::Log:
        return event.message;
    }
    return "unknown event";
}

}  // namespace

void RunTraceView(diagnostics::TraceStore& store, const std::atomic_int& result,
                  const std::function<void()>& requestStop) {
    using namespace ftxui;
    auto screen = App::Fullscreen();
    diagnostics::TraceSnapshot snapshot;
    int selected = 0;
    int scroll = 0;
    bool frozen = false;
    bool threadFilter = false;
    bool faultFilter = false;
    auto renderer = Renderer([&] {
        if (!frozen)
            snapshot = store.Read();

        selected = std::clamp(selected, 0, std::max(0, int(snapshot.threads.size()) - 1));
        const auto* thread = snapshot.threads.empty() ? nullptr : &snapshot.threads[selected];
        const int status = result.load(std::memory_order_acquire);
        const auto statusText = status < 0 ? "running" : status == 0 ? "finished" : "failed";

        Elements rows{text(" id  process state       cpu pri affinity last block entry") | bold};
        for (size_t i = 0; i < snapshot.threads.size(); ++i) {
            const auto& item = snapshot.threads[i];
            const auto cpu = item.cpu == UINT32_MAX ? "-" : std::to_string(item.cpu);

            auto row = text(std::format("{:3} {:7} {:11} {:>3} {:3}       {:02X}   {:08X} {:08X}", item.id,
                                        item.process, State(item), cpu, item.priority, item.affinity,
                                        item.lastBlock, item.entry));

            if (int(i) == selected)
                row = row | inverted | focus;

            rows.push_back(row);
        }

        Elements timeline;
        std::vector<const diagnostics::RecordedEvent*> visible;

        for (const auto& recorded : snapshot.events) {
            const auto& event = recorded.event;

            if (threadFilter && (!thread || event.thread != thread->id))
                continue;

            if (faultFilter && event.kind != diagnostics::EventKind::Fault
                && !(event.kind == diagnostics::EventKind::Log && event.value >= 4))
                continue;

            visible.push_back(&recorded);
        }

        const int count = std::max(3, screen.dimy() - 16);
        scroll = std::clamp(scroll, 0, std::max(0, int(visible.size()) - count));
        const int end = int(visible.size()) - scroll;
        const int begin = std::max(0, end - count);

        for (int i = begin; i < end; ++i) {
            const auto& recorded = *visible[i];
            std::string description = Describe(recorded.event);
            std::replace(description.begin(), description.end(), '\n', ' ');
            std::replace(description.begin(), description.end(), '\r', ' ');

            auto row = paragraph(std::format("{:10.3f}ms t{:3} {}", recorded.microseconds / 1000.0,
                                             recorded.event.thread, description));

            if (recorded.event.kind == diagnostics::EventKind::Fault
                || (recorded.event.kind == diagnostics::EventKind::Log && recorded.event.value >= 4))
                row = row | color(Color::Red);

            if (i == end - 1)
                row = row | focus;

            timeline.push_back(row);
        }

        if (timeline.empty())
            timeline.push_back(text("no matching events"));

        const auto details
            = thread ? std::format("thread {}  kthread {:08X}  stack {:08X}..{:08X}  tls {:08X}", thread->id,
                                   thread->object, thread->stackLimit, thread->stackBase, thread->tls) :
                       "no guest threads yet";

        return vbox({text(std::format("noble trace | {} | {} | execution {} | evicted {} | lost {}",
                                      statusText, frozen ? "display frozen" : "live",
                                      store.ExecutionEnabled() ? "on" : "off", snapshot.evicted,
                                      snapshot.lost))
                         | bold,
                     separator(), vbox(rows) | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 8),
                     separator(), text(details), separator(),
                     vbox(timeline) | vscroll_indicator | yframe | flex, separator(),
                     text("up/down thread | t filter | f faults | b execution | space freeze"),
                     text("pgup/pgdn history | q quit and stop guest")})
               | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Escape) {
            requestStop();
            screen.Exit();
            return true;
        }

        if (event == Event::ArrowUp) {
            --selected;
            return true;
        }

        if (event == Event::ArrowDown) {
            ++selected;
            return true;
        }

        if (event == Event::PageUp) {
            scroll += 10;
            return true;
        }

        if (event == Event::PageDown) {
            scroll -= 10;
            return true;
        }

        if (event == Event::Character('t')) {
            threadFilter = !threadFilter;
            scroll = 0;
            return true;
        }

        if (event == Event::Character('f')) {
            faultFilter = !faultFilter;
            scroll = 0;
            return true;
        }

        if (event == Event::Character('b')) {
            store.SetExecutionEnabled(!store.ExecutionEnabled());
            return true;
        }

        if (event == Event::Character(' ')) {
            frozen = !frozen;
            return true;
        }

        return false;
    });

    std::jthread refresh([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            screen.PostEvent(Event::Custom);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    screen.Loop(component);
    refresh.request_stop();
}

}  // namespace tui
