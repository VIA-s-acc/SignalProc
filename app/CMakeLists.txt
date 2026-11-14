add_executable(signals main.cpp)
target_link_libraries(signals PRIVATE signals_cli signals_tui)
signals_configure_target(signals)
# argv, std::filesystem::path(std::string) and path::string() are UTF-8 only under this code page; without it Cyrillic paths reach the program in the ANSI code page.
if(WIN32)
  target_sources(signals PRIVATE signals.manifest)
endif()
