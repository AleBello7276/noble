#include "DebuggerView.h"
#include "debugger/Debugger.h"
#include "diagnostics/TraceStore.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <format>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <thread>

namespace tui {

// accept an aligned hexadecimal guest instruction address
std::optional<uint32_t> ParseAddress(std::string_view input) {
    if (input.starts_with("0x") || input.starts_with("0X"))
        input.remove_prefix(2);
    uint32_t address = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), address, 16);
    if (input.empty() || error != std::errc{} || end != input.data() + input.size() || (address & 3))
        return std::nullopt;
    return address;
}

// separate copied command output into independently scrollable rows
std::vector<std::string> Lines(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream stream(value);
    for (std::string line; std::getline(stream, line);)
        result.push_back(std::move(line));
    return result;
}

// distinguish scheduler states without relying on color alone
ftxui::Color StateColor(debugger::ThreadStatus status) {
    using ftxui::Color;
    switch (status) {
    case debugger::ThreadStatus::Running:
        return Color::Green;
    case debugger::ThreadStatus::Paused:
        return Color::Yellow;
    case debugger::ThreadStatus::Waiting:
        return Color::Cyan;
    case debugger::ThreadStatus::Faulted:
        return Color::Red;
    case debugger::ThreadStatus::Exited:
        return Color::GrayDark;
    }
    return Color::White;
}

void RunDebuggerView(debugger::Debugger& debugger, diagnostics::TraceStore& trace,
                     const std::function<void()>& requestStop) {
    using namespace ftxui;
    auto screen = App::Fullscreen();
    enum class Pane { Code, Inspection, Threads };
    Pane active = Pane::Code;
    bool navigation = false;
    int selected = 0, tab = 0, threadFirst = 0;
    std::array<int, 6> offsets{};
    int codeRows = 1, inspectionRows = 1, inspectionCount = 0;
    uint32_t codeStart = 0, codeCursor = 0;
    bool followPC = true, memoryInitialized = false;
    std::string command, response = "enter help for commands";
    std::string codeAddress, memoryAddress = "00000000";
    Box codeBox, inspectionBox, threadBox;
    std::vector<Box> instructionBoxes;
    std::vector<debugger::Instruction> instructions;

    // manual navigation remains in place until follow pc is explicitly selected
    auto moveCode = [&](int64_t rows) {
        codeStart = uint32_t(std::clamp(int64_t(codeStart) + rows * 4, int64_t(0), int64_t(0xFFFFFFFC)));
        codeCursor = codeStart;
        followPC = false;
        active = Pane::Code;
        codeAddress = std::format("{:08X}", codeStart);
    };
    auto follow = [&] {
        followPC = true;
        active = Pane::Code;
        const auto threads = debugger.Threads();
        if (selected >= 0 && selected < int(threads.size()))
            codeAddress = std::format("{:08X}", threads[selected].registers.CIA);
    };
    auto jump = [&] {
        const auto address = ParseAddress(codeAddress);
        if (!address) {
            response = "use an aligned hexadecimal instruction address";
            tab = 5;
            return;
        }
        codeStart = codeCursor = *address;
        followPC = false;
        active = Pane::Code;
        navigation = true;
        response = std::format("disassembly at {:08X}", *address);
    };
    auto toggleBreakpoint = [&](uint32_t address) {
        const auto breakpoints = debugger.Breakpoints();
        const bool present = std::find(breakpoints.begin(), breakpoints.end(), address) != breakpoints.end();
        if (present)
            debugger.RemoveBreakpoint(address);
        else
            debugger.AddBreakpoint(address);
        response = std::format("breakpoint {} at {:08X}", present ? "removed" : "set", address);
    };
    auto scrollInspection = [&](int rows) {
        offsets[tab] = std::clamp(offsets[tab] + rows, 0, std::max(0, inspectionCount - inspectionRows));
        active = Pane::Inspection;
        navigation = true;
    };

    InputOption inputStyle;
    inputStyle.multiline = false;
    auto input = Input(&command, "command or help", inputStyle);
    auto memoryInput = Input(&memoryAddress, "memory hex address", inputStyle);
    auto codeInput = Input(&codeAddress, "instruction hex address", inputStyle);
    ButtonOption buttonStyle;
    buttonStyle.transform = [](const EntryState& state) {
        auto label = text("[" + state.label + "]") | color(Color::Cyan);
        return state.focused ? label | inverted : label;
    };
    auto jumpButton = Button("go", jump, buttonStyle);
    auto followButton = Button("follow PC", follow, buttonStyle);
    auto previousButton = Button("<", [&] { moveCode(-codeRows); }, buttonStyle);
    auto nextButton = Button(">", [&] { moveCode(codeRows); }, buttonStyle);
    auto codeControls
        = Container::Horizontal({codeInput, jumpButton, followButton, previousButton, nextButton});
    const std::vector<std::string> tabNames{"gpr", "fpr", "vectors", "history", "logs", "output"};
    auto tabs = Toggle(&tabNames, &tab);
    auto controls = Container::Vertical({codeControls, tabs, memoryInput, input});
    input->TakeFocus();

    auto renderer = Renderer(controls, [&] {
        const auto threads = debugger.Threads();
        selected = std::clamp(selected, 0, std::max(0, int(threads.size()) - 1));
        const int topHeight = std::clamp(screen.dimy() / 3, 6, 20);
        const int inspectionHeight = std::max(3, screen.dimy() - topHeight - 11);
        codeRows = topHeight - 2;
        inspectionRows = inspectionHeight - 2;
        Elements threadElements, codeElements, inspectionElements;

        threadFirst = std::max(0, selected - codeRows + 1);
        for (int i = threadFirst; i < int(threads.size()) && i < threadFirst + codeRows; ++i) {
            const auto& t = threads[i];
            auto row = hbox({text(std::format("t{} ", t.id)) | color(Color::White),
                             text(debugger::Name(t.status)) | color(StateColor(t.status)),
                             text(std::format(" {:08X}", t.registers.CIA)) | color(Color::Cyan)});
            if (i == selected)
                row = row | bgcolor(Color::Blue) | bold;
            threadElements.push_back(row);
        }
        if (!threads.empty()) {
            const auto& t = threads[selected];
            if (!memoryInitialized) {
                memoryAddress = std::format("{:08X}", uint32_t(t.registers.GPRs[1].u64));
                memoryInitialized = true;
            }
            if (followPC) {
                const uint32_t before = uint32_t(std::min(2, codeRows / 2)) * 4;
                codeStart = t.registers.CIA >= before ? t.registers.CIA - before : 0;
                codeCursor = t.registers.CIA;
                if (!codeInput->Focused())
                    codeAddress = std::format("{:08X}", t.registers.CIA);
            }
        }
        instructions = debugger.Disassemble(codeStart, codeRows);
        instructionBoxes.resize(instructions.size());
        for (size_t i = 0; i < instructions.size(); ++i) {
            const auto& ins = instructions[i];
            const bool current = !threads.empty() && ins.address == threads[selected].registers.CIA;
            const auto split = ins.text.find(' ');
            auto row = hbox(
                {text(ins.breakpoint ? "● " : "· ") | color(ins.breakpoint ? Color::Red : Color::GrayDark),
                 text(current ? "> " : "  ") | color(Color::Yellow) | bold,
                 text(std::format("{:08X} ", ins.address)) | color(Color::Cyan),
                 text(std::format("{:08X} ", ins.word)) | color(Color::GrayDark),
                 text(ins.text.substr(0, split)) | color(ins.mapped ? Color::Green : Color::Red) | bold,
                 text(split == std::string::npos ? "" : ins.text.substr(split)) | color(Color::White)});
            if (ins.address == codeCursor && active == Pane::Code)
                row = row | bgcolor(Color::Blue);
            else if (current)
                row = row | bgcolor(Color::GrayDark);
            codeElements.push_back(row | reflect(instructionBoxes[i]));
        }

        std::string contents;
        if (tab == 5)
            contents = response;
        else if (tab == 4) {
            for (const auto& event : trace.Read().events)
                if (event.event.kind == diagnostics::EventKind::Log)
                    contents += event.event.message + "\n";
        } else if (!threads.empty()) {
            const auto& t = threads[selected];
            if (tab == 2) {
                // format one copied snapshot instead of querying the debugger for each vector
                for (unsigned i = 0; i < 128; ++i) {
                    const auto& v = t.registers.VRs[i];
                    const bool changed = std::memcmp(&v, &t.previous.VRs[i], sizeof(v)) != 0;
                    contents += std::format("{} v{:03} ", changed ? '*' : ' ', i);
                    for (int byte = 15; byte >= 0; --byte)
                        contents += std::format("{:02X} ", v.bytes[byte]);
                    contents += std::format("\n  f32 {} {} {} {}\n", v.flt[3], v.flt[2], v.flt[1], v.flt[0]);
                }
            } else {
                contents = debugger.Command(std::format("{} {}",
                                                        tab == 0 ? "regs" :
                                                        tab == 1 ? "fregs" :
                                                                   "history",
                                                        t.id));
            }
        }
        const auto lines = Lines(contents);
        inspectionCount = int(lines.size());
        offsets[tab] = std::clamp(offsets[tab], 0, std::max(0, inspectionCount - inspectionRows));
        for (int i = offsets[tab]; i < inspectionCount && i < offsets[tab] + inspectionRows; ++i)
            inspectionElements.push_back(
                text(lines[i])
                | color(lines[i].find('*') != std::string::npos ? Color::Yellow : Color::White));
        const auto panelColor = active == Pane::Inspection ? Color::Magenta : Color::GrayDark;
        const auto codeColor = active == Pane::Code ? Color::Cyan : Color::GrayDark;
        const std::string inspectionTitle = std::format(
            " {} | rows {}-{} / {} | wheel or F7/F8 ", tabNames[tab], inspectionCount ? offsets[tab] + 1 : 0,
            std::min(inspectionCount, offsets[tab] + inspectionRows), inspectionCount);
        Elements memoryLines;
        for (const auto& line : Lines(debugger.Command("memory " + memoryAddress + " 32")))
            memoryLines.push_back(text(line) | color(Color::White));

        return vbox({
            text("noble | F2 address  F3 PC  F4 breakpoint  F5 run  F6 pause  F10 next  F11 step")
                | color(Color::Cyan) | bold,
            hbox({text("code hex: ") | color(Color::Cyan), codeInput->Render() | flex, jumpButton->Render(),
                  followButton->Render(), previousButton->Render(), nextButton->Render()}),
            hbox(
                {window(text(" threads ") | bold, vbox({vbox(threadElements), filler()}) | reflect(threadBox))
                     | color(Color::Blue) | size(WIDTH, EQUAL, 28),
                 window(text(followPC ? " disassembly | following PC " : " disassembly | browsing ") | bold
                            | color(Color::Cyan),
                        vbox({vbox(codeElements), filler()}) | reflect(codeBox))
                     | color(codeColor) | flex})
                | size(HEIGHT, EQUAL, topHeight),
            tabs->Render() | color(Color::Magenta),
            window(text(inspectionTitle) | bold | color(Color::Magenta),
                   vbox({vbox(inspectionElements), filler()}) | reflect(inspectionBox))
                | color(panelColor) | size(HEIGHT, EQUAL, inspectionHeight),
            hbox({text("memory hex: ") | color(Color::Green), memoryInput->Render()}),
            window(text(" memory | bytes and ASCII ") | bold, vbox(memoryLines)) | color(Color::Green)
                | size(HEIGHT, EQUAL, 4),
            hbox({text("> ") | color(Color::Yellow), input->Render()}) | borderStyled(Color::Yellow),
        });
    });
    auto component = CatchEvent(renderer, [&](Event event) {
        const auto threads = debugger.Threads();
        if (event == Event::Escape) {
            navigation = false;
            input->TakeFocus();
            return true;
        }
        if (event == Event::F1) {
            response = debugger.Command("help");
            tab = 5;
            offsets[tab] = 0;
            active = Pane::Inspection;
            return true;
        }
        if (event == Event::F9 && !threads.empty()) {
            selected = (selected + 1) % int(threads.size());
            follow();
            return true;
        }
        if (event.is_mouse()) {
            const auto& mouse = event.mouse();
            const bool wheel = mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown;
            const int delta = mouse.button == Mouse::WheelUp ? -3 : 3;
            if (wheel && codeBox.Contain(mouse.x, mouse.y)) {
                moveCode(delta);
                return true;
            }
            if (wheel && inspectionBox.Contain(mouse.x, mouse.y)) {
                scrollInspection(delta);
                return true;
            }
            if (wheel && threadBox.Contain(mouse.x, mouse.y)) {
                selected
                    = std::clamp(selected + (delta > 0 ? 1 : -1), 0, std::max(0, int(threads.size()) - 1));
                follow();
                return true;
            }
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
                for (size_t i = 0; i < instructionBoxes.size(); ++i) {
                    const auto& box = instructionBoxes[i];
                    if (!box.Contain(mouse.x, mouse.y))
                        continue;
                    codeCursor = instructions[i].address;
                    active = Pane::Code;
                    followPC = false;
                    navigation = true;
                    if (mouse.x < box.x_min + 2)
                        toggleBreakpoint(codeCursor);
                    return true;
                }
                if (inspectionBox.Contain(mouse.x, mouse.y)) {
                    active = Pane::Inspection;
                    navigation = true;
                    return true;
                }
                if (threadBox.Contain(mouse.x, mouse.y)) {
                    const int row = threadFirst + mouse.y - threadBox.y_min;
                    if (row < int(threads.size())) {
                        selected = row;
                        follow();
                        active = Pane::Threads;
                        navigation = true;
                    }
                    return true;
                }
            }
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed)
                navigation = false;
        }
        if (event == Event::Tab || event == Event::TabReverse)
            navigation = false;
        if (event == Event::F2) {
            navigation = false;
            codeAddress.clear();
            codeInput->TakeFocus();
            return true;
        }
        if (event == Event::F3) {
            follow();
            return true;
        }
        if (event == Event::F4) {
            toggleBreakpoint(codeCursor);
            return true;
        }
        if (event == Event::F7) {
            scrollInspection(-1);
            return true;
        }
        if (event == Event::F8) {
            scrollInspection(1);
            return true;
        }
        if (event == Event::F6) {
            debugger.PauseAll();
            return true;
        }
        if (event == Event::PageUp || event == Event::PageDown) {
            if (tabs->Focused() && !navigation)
                active = Pane::Inspection;
            const int direction = event == Event::PageUp ? -1 : 1;
            if (active == Pane::Inspection)
                scrollInspection(direction * inspectionRows);
            else if (active == Pane::Threads) {
                selected = std::clamp(selected + direction, 0, std::max(0, int(threads.size()) - 1));
                followPC = true;
            } else
                moveCode(direction * codeRows);
            return true;
        }
        if (navigation && active == Pane::Inspection && (event == Event::Home || event == Event::End)) {
            offsets[tab] = event == Event::Home ? 0 : std::max(0, inspectionCount - inspectionRows);
            return true;
        }
        if (navigation && (event == Event::ArrowUp || event == Event::ArrowDown)) {
            const int direction = event == Event::ArrowUp ? -1 : 1;
            if (active == Pane::Inspection)
                scrollInspection(direction);
            else if (active == Pane::Threads) {
                selected = std::clamp(selected + direction, 0, std::max(0, int(threads.size()) - 1));
                followPC = true;
            } else {
                const uint32_t target = uint32_t(
                    std::clamp(int64_t(codeCursor) + direction * 4, int64_t(0), int64_t(0xFFFFFFFC)));
                if (target < codeStart)
                    moveCode(-1);
                else if (uint64_t(target) >= uint64_t(codeStart) + codeRows * 4)
                    moveCode(1);
                codeCursor = target;
                followPC = false;
            }
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
                follow();
                response = debugger.Step(id, event == Event::F10 ? debugger::StepKind::Over :
                                             event == Event::F12 ? debugger::StepKind::Out :
                                                                   debugger::StepKind::Into) ?
                               "stepping" :
                               "thread must be paused";
                return true;
            }
        }
        if (event == Event::Return && codeInput->Focused()) {
            jump();
            return true;
        }
        if (event == Event::Return && input->Focused()) {
            if (command == "quit") {
                requestStop();
                screen.Exit();
            } else {
                response = debugger.Command(command);
                tab = 5;
                offsets[tab] = 0;
            }
            command.clear();
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
