add_library(signals_audio STATIC
  player.cpp
  device_sink.cpp
  miniaudio_impl.cpp
)
target_include_directories(signals_audio PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_include_directories(signals_audio SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
target_compile_definitions(signals_audio PRIVATE MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER
                                                 MA_NO_NODE_GRAPH MA_NO_ENGINE)
set_source_files_properties(miniaudio_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
find_package(Threads REQUIRED)
target_link_libraries(signals_audio PUBLIC signals_core PRIVATE Threads::Threads)
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Apple's CoreAudio headers use Clang blocks, which GCC cannot parse; GCC builds on macOS get no audio device.
  target_compile_definitions(signals_audio PRIVATE MA_NO_COREAUDIO)
elseif(APPLE)
  target_compile_definitions(signals_audio PRIVATE MA_NO_RUNTIME_LINKING)
  target_link_libraries(signals_audio PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
  target_link_libraries(signals_audio PRIVATE ${CMAKE_DL_LIBS} m)
endif()
signals_configure_target(signals_audio)
