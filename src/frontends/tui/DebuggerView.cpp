#include "DebuggerView.h"
#include "debugger/Debugger.h"
#include "diagnostics/TraceStore.h"
#include <algorithm>
#include <format>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <thread>

namespace tui {
void RunDebuggerView(debugger::Debugger& debugger, diagnostics::TraceStore& trace,
                     const std::function<void()>& requestStop) {
    using namespace ftxui;
    auto screen = App::Fullscreen();
    int selected = 0;
    int tab = 0;
    int scroll = 0;
    std::string command, response = "enter help for commands";
    std::string memoryAddress = "00000000";
    bool memoryInitialized = false;
    auto input = Input(&command, "command, or help");
    auto memoryInput = Input(&memoryAddress, "memory address in hex");
    const std::vector<std::string> tabNames{"gpr", "fpr", "vectors", "history", "logs", "output"};
    auto tabs = Toggle(&tabNames, &tab);
    auto controls = Container::Vertical({tabs, memoryInput, input});
    input->TakeFocus();
    auto lines = [&](const std::string& value, bool scrolling = false) {
        Elements elements;
        std::istringstream stream(value);
        for (std::string line; std::getline(stream, line);)
            elements.push_back(text(line));
        if (scrolling && !elements.empty()) {
            scroll = std::clamp(scroll, 0, int(elements.size()) - 1);
            elements[scroll] = elements[scroll] | focus;
        }
        return vbox(std::move(elements));
    };
    auto renderer = Renderer(controls, [&] {
        const auto threads = debugger.Threads();
        selected = std::clamp(selected, 0, std::max(0, int(threads.size()) - 1));
        Elements threadRows, codeRows;
        for (size_t i = 0; i < threads.size(); ++i) {
            const auto& t = threads[i];
            auto row = text(std::format("t{} {} {:08X} {}", t.id, debugger::Name(t.status), t.registers.CIA,
                                        debugger::Name(t.reason)));
            if (int(i) == selected)
                row = row | inverted;
            threadRows.push_back(row);
        }
        std::string registers;
        if (tab == 5)
            registers = response;
        if (tab == 4) {
            const auto recorded = trace.Read();
            for (const auto& event : recorded.events)
                if (event.event.kind == diagnostics::EventKind::Log)
                    registers += event.event.message + "\n";
        }
        if (!threads.empty()) {
            const auto& t = threads[selected];
            if (!memoryInitialized) {
                memoryAddress = std::format("{:08X}", uint32_t(t.registers.GPRs[1].u64));
                memoryInitialized = true;
            }
            for (const auto& ins :
                 debugger.Disassemble(t.registers.CIA >= 16 ? t.registers.CIA - 16 : 0, 16)) {
                auto row = text(std::format("{} {:08X} {:08X} {}", ins.breakpoint ? '*' : ' ', ins.address,
                                            ins.word, ins.text));
                if (ins.address == t.registers.CIA)
                    row = row | inverted | focus;
                codeRows.push_back(row);
            }
            if (tab == 2) {
                for (unsigned i = 0; i < 128; ++i)
                    registers += debugger.Command(std::format("vregs {} {}", t.id, i));
            } else if (tab < 4) {
                registers = debugger.Command(std::format("{} {}",
                                                         tab == 0 ? "regs" :
                                                         tab == 1 ? "fregs" :
                                                                    "history",
                                                         t.id));
            }
        }
        return vbox({text("noble debugger | F5 continue | F6 pause all | F10 next | F11 step | F12 finish | "
                          "PgUp/PgDn thread")
                         | bold,
                     hbox({vbox(std::move(threadRows)) | border | size(WIDTH, EQUAL, 28),
                           vbox(std::move(codeRows)) | frame | border | flex})
                         | size(HEIGHT, EQUAL, std::clamp(screen.dimy() / 3, 5, 18)),
                     tabs->Render(), lines(registers, true) | vscroll_indicator | frame | flex | border,
                     hbox({text("memory hex: "), memoryInput->Render()}),
                     lines(debugger.Command("memory " + memoryAddress + " 32")) | border,
                     hbox({text("> "), input->Render()}) | border});
    });
    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::F7) {
            --scroll;
            return true;
        }
        if (event == Event::F8) {
            ++scroll;
            return true;
        }
        const auto threads = debugger.Threads();
        if (event == Event::F6) {
            debugger.PauseAll();
            return true;
        }
        if (!threads.empty()) {
            selected = std::clamp(selected, 0, int(threads.size()) - 1);
            const auto id = threads[selected].id;
            if (event == Event::F5) {
                debugger.Continue(id);
                return true;
            }
            if (event == Event::F10 || event == Event::F11 || event == Event::F12) {
                response = debugger.Step(id, event == Event::F10 ? debugger::StepKind::Over :
                                             event == Event::F12 ? debugger::StepKind::Out :
                                                                   debugger::StepKind::Into) ?
                               "stepping" :
                               "thread must be paused";
                return true;
            }
        }
        if (event == Event::Return && input->Focused()) {
            if (command == "quit") {
                requestStop();
                screen.Exit();
            } else {
                response = debugger.Command(command);
                tab = 5;
                scroll = 0;
            }
            command.clear();
            return true;
        }
        if (event == Event::PageUp) {
            --selected;
            return true;
        }
        if (event == Event::PageDown) {
            ++selected;
            return true;
        }
        return false;
    });
    std::jthread refresh([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            screen.PostEvent(Event::Custom);
        }
    });
    screen.Loop(component);
}
}  // namespace tui
