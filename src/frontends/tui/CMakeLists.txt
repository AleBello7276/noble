

add_executable(noble-tui main.cpp TraceView.cpp)
target_link_libraries(noble-tui PRIVATE noble_runtime noble_trace ftxui::component ftxui::dom ftxui::screen)
