cmake_minimum_required(VERSION 3.21)

project(rvsim LANGUAGES C CXX)

set(LIB_DIR external)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(RVSIM_BUILD_TESTS "Build unit tests (native only)" ON)
option(RVSIM_ENABLE_SANITIZERS "Enable ASan + UBSan (GCC/Clang native only)" OFF)

if(RVSIM_ENABLE_SANITIZERS AND NOT MSVC AND NOT EMSCRIPTEN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

include(cmake/get_cpm.cmake)

CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  GIT_TAG 148bd34a7e8106cd6e8dae8411a506a8e3e84f40
  DOWNLOAD_ONLY YES
)

if(NOT EMSCRIPTEN)
  CPMAddPackage(
    NAME glfw
    GITHUB_REPOSITORY glfw/glfw
    GIT_TAG 3.5.1
    OPTIONS
      "GLFW_BUILD_EXAMPLES OFF"
      "GLFW_BUILD_TESTS OFF"
      "GLFW_BUILD_DOCS OFF"
      "GLFW_INSTALL OFF"
  )
endif()

add_library(rvsim_platform INTERFACE)

if(MSVC)
  target_compile_options(rvsim_platform INTERFACE
    /utf-8
    /Zc:__cplusplus
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Debug>:/Od>
  )
else()
  target_compile_options(rvsim_platform INTERFACE
    $<$<CONFIG:Release>:-Os>
    $<$<CONFIG:Debug>:-O0 -g>
  )
endif()

if(EMSCRIPTEN)
  target_compile_definitions(rvsim_platform INTERFACE IMGUI_IMPL_OPENGL_ES3)
  target_compile_options(rvsim_platform INTERFACE --use-port=contrib.glfw3 -pthread)
  target_link_options(rvsim_platform INTERFACE --use-port=contrib.glfw3 -pthread)
else()
  set(THREADS_PREFER_PTHREAD_FLAG TRUE)
  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)

  if(APPLE)
    target_compile_definitions(rvsim_platform INTERFACE GL_SILENCE_DEPRECATION)
  endif()

  if(WIN32)
    target_compile_definitions(rvsim_platform INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
endif()

# Project-wide warning policy applied to first-party targets only.
add_library(rvsim_warnings INTERFACE)
if(MSVC)
  target_compile_options(rvsim_warnings INTERFACE /W4 /utf-8 /Zc:__cplusplus)
else()
  target_compile_options(rvsim_warnings INTERFACE -Wall -Wextra)
endif()

# GCC 14 ships std::print's runtime support in libstdc++exp.
add_library(rvsim_stdlib INTERFACE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14 AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
  target_link_libraries(rvsim_stdlib INTERFACE stdc++exp)
endif()

# ---------------------------------------------------------------------------
# rvsim_core: pure C++23 simulator library (no ImGui / GLFW / Emscripten UI).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE RVSIM_CORE_SOURCES CONFIGURE_DEPENDS "src/simulator/*.cpp")

add_library(rvsim_core STATIC ${RVSIM_CORE_SOURCES})
target_include_directories(rvsim_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rvsim_core PRIVATE rvsim_warnings)

if(EMSCRIPTEN)
  # Every object linked into the -pthread GUI must be built with atomics.
  target_compile_options(rvsim_core PUBLIC -pthread)
  target_link_options(rvsim_core PUBLIC -pthread)
else()
  target_link_libraries(rvsim_core PUBLIC Threads::Threads)
endif()

add_library(imgui_lib STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_demo.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(imgui_lib SYSTEM PUBLIC
  "${imgui_SOURCE_DIR}"
  "${imgui_SOURCE_DIR}/backends"
)

target_link_libraries(imgui_lib PUBLIC rvsim_platform)

if(NOT EMSCRIPTEN)
  target_link_libraries(imgui_lib PUBLIC glfw)
endif()

add_library(imgui_text_editor STATIC
  "${LIB_DIR}/text_editor/TextEditor.cpp"
)

target_include_directories(imgui_text_editor SYSTEM PUBLIC
  "${LIB_DIR}/text_editor"
)

target_link_libraries(imgui_text_editor PUBLIC imgui_lib)

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(EMBEDDED_FONTS_HEADER "${GENERATED_DIR}/embedded_fonts.h")
set(EMBEDDED_FONTS_SOURCE "${GENERATED_DIR}/embedded_fonts.cpp")
set(FONT_SANS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf")
set(FONT_MONO "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansMono-Regular.ttf")

add_custom_command(
  OUTPUT "${EMBEDDED_FONTS_HEADER}" "${EMBEDDED_FONTS_SOURCE}"
  COMMAND ${CMAKE_COMMAND}
    -DOUTPUT_HEADER=${EMBEDDED_FONTS_HEADER}
    -DOUTPUT_SOURCE=${EMBEDDED_FONTS_SOURCE}
    -DFONT_SANS=${FONT_SANS}
    -DFONT_MONO=${FONT_MONO}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_assets.cmake"
    "${FONT_SANS}"
    "${FONT_MONO}"
  COMMENT "Embedding font assets into generated C++ sources"
  VERBATIM
)

# ---------------------------------------------------------------------------
# rvsim: Dear ImGui IDE (Native desktop + WebAssembly).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE RVSIM_GUI_SOURCES CONFIGURE_DEPENDS
  "src/windows/*.cpp"
  "src/ui/*.cpp"
  "src/platform/*.cpp"
)

add_executable(rvsim
  src/main.cpp
  src/app.cpp
  src/context.cpp
  ${RVSIM_GUI_SOURCES}
  "${EMBEDDED_FONTS_SOURCE}"
  "${EMBEDDED_FONTS_HEADER}"
)

target_include_directories(rvsim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR}"
  "${GENERATED_DIR}"
)

target_link_libraries(rvsim PRIVATE
  rvsim_core
  imgui_lib
  imgui_text_editor
  rvsim_warnings
  rvsim_stdlib
)

if(EMSCRIPTEN)
  set_target_properties(rvsim PROPERTIES
    OUTPUT_NAME "index"
    SUFFIX ".html"
  )

  target_link_options(rvsim PRIVATE
    -sPTHREAD_POOL_SIZE=2
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=1GB
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell_minimal.html"
    $<$<CONFIG:Debug>:-sASSERTIONS=1>
    # Advisory only: JS-side heap views are slower with pthreads + growth.
    -Wno-pthreads-mem-growth
  )
  set_property(TARGET rvsim APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shell_minimal.html")

  add_custom_command(TARGET rvsim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/web/coi-serviceworker.js"
      "$<TARGET_FILE_DIR:rvsim>/coi-serviceworker.js"
  )
else()
  target_link_libraries(rvsim PRIVATE OpenGL::GL Threads::Threads)

  target_link_options(rvsim PRIVATE
    $<$<AND:$<PLATFORM_ID:Linux>,$<CXX_COMPILER_ID:GNU>,$<CONFIG:Release>>:-static-libstdc++ -static-libgcc>
  )

  if(WIN32)
    set_target_properties(rvsim PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    if(MSVC)
      target_link_options(rvsim PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# rvsim-cli: headless runner (native only).
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
  add_executable(rvsim_cli src/cli/main.cpp)
  set_target_properties(rvsim_cli PROPERTIES OUTPUT_NAME "rvsim-cli")
  target_link_libraries(rvsim_cli PRIVATE rvsim_core rvsim_warnings rvsim_stdlib)
  target_link_options(rvsim_cli PRIVATE
    $<$<AND:$<PLATFORM_ID:Linux>,$<CXX_COMPILER_ID:GNU>,$<CONFIG:Release>>:-static-libstdc++ -static-libgcc>
  )
endif()

# ---------------------------------------------------------------------------
# rvsim_tests: GoogleTest unit + integration suite (native only).
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND RVSIM_BUILD_TESTS)
  CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    VERSION 1.17.0
    OPTIONS
      "INSTALL_GTEST OFF"
      "BUILD_GMOCK OFF"
    SYSTEM YES
  )

  enable_testing()
  include(GoogleTest)

  file(GLOB_RECURSE RVSIM_TEST_SOURCES CONFIGURE_DEPENDS "tests/*.cpp")
  add_executable(rvsim_tests ${RVSIM_TEST_SOURCES})
  target_link_libraries(rvsim_tests PRIVATE
    rvsim_core
    rvsim_warnings
    rvsim_stdlib
    GTest::gtest_main
  )
  target_compile_definitions(rvsim_tests PRIVATE
    RVSIM_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata"
  )
  gtest_discover_tests(rvsim_tests DISCOVERY_MODE PRE_TEST)

  # End-to-end CLI checks.
  add_test(NAME cli.run_fibonacci
    COMMAND rvsim_cli run "${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata/fibonacci.s" --dump-regs)
  set_tests_properties(cli.run_fibonacci PROPERTIES
    PASS_REGULAR_EXPRESSION "recursive fib\\(15\\) = 610.*instret")
  add_test(NAME cli.run_fibonacci_rv64
    COMMAND rvsim_cli run "${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata/fibonacci.s" --arch rv64i)
  set_tests_properties(cli.run_fibonacci_rv64 PROPERTIES
    PASS_REGULAR_EXPRESSION "recursive fib\\(15\\) = 610")
  add_test(NAME cli.assemble_disasm
    COMMAND rvsim_cli assemble "${CMAKE_CURRENT_SOURCE_DIR}/tests/testdata/quicksort.s" --disasm)
  set_tests_properties(cli.assemble_disasm PROPERTIES
    PASS_REGULAR_EXPRESSION "<quicksort>:")
endif()
