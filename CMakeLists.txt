cmake_minimum_required(VERSION 3.10)

project(rvsim)

set(LIB_DIR external)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This project must be built using the Emscripten SDK (emcmake cmake ...)")
endif()


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EMS_FLAGS
    "-sDISABLE_EXCEPTION_CATCHING=1"
    "--use-port=contrib.glfw3"
    "-pthread"
)

add_library(imgui_lib STATIC 
    ${LIB_DIR}/imgui/imgui.cpp
    ${LIB_DIR}/imgui/imgui_demo.cpp
    ${LIB_DIR}/imgui/imgui_draw.cpp
    ${LIB_DIR}/imgui/imgui_tables.cpp
    ${LIB_DIR}/imgui/imgui_widgets.cpp
    ${LIB_DIR}/imgui/misc/cpp/imgui_stdlib.cpp
    ${LIB_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${LIB_DIR}/imgui/backends/imgui_impl_opengl3.cpp)

target_include_directories(imgui_lib PUBLIC ${LIB_DIR}/imgui ${LIB_DIR}/imgui/backends)

target_compile_options(imgui_lib PRIVATE -Os -Wall -Wextra ${EMS_FLAGS})

add_library(imgui_text_editor STATIC ${LIB_DIR}/text_editor/TextEditor.cpp)

target_include_directories(imgui_text_editor PUBLIC ${LIB_DIR}/text_editor)

target_compile_options(imgui_text_editor PRIVATE -Os -Wall -Wextra ${EMS_FLAGS})

target_link_libraries(imgui_text_editor PRIVATE imgui_lib)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(rvsim ${SOURCES})

set_target_properties(rvsim PROPERTIES 
    OUTPUT_NAME "index"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/web"
)

target_link_libraries(rvsim PRIVATE imgui_lib)
target_link_libraries(rvsim PRIVATE imgui_text_editor)

target_compile_options(rvsim PRIVATE 
    -Os 
    -Wall 
    -Wextra 
    ${EMS_FLAGS}
)

target_include_directories(rvsim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIB_DIR}
)

target_link_options(rvsim PRIVATE
    ${EMS_FLAGS}
    -Os
    # "-sWASM=1"
    # "-sALLOW_MEMORY_GROWTH=1"
    # "-sNO_EXIT_RUNTIME=0"
    "-sASSERTIONS=1"
    "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets@assets/"
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell_minimal.html"
)

# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# execute_process(COMMAND ${CMAKE_CXX_COMPILER} --cflags
#     OUTPUT_VARIABLE EM_CFLAGS
#     COMMAND_ERROR_IS_FATAL ANY
# )
# string(STRIP "${EM_CFLAGS}" EM_CFLAGS)
# find_package(Python3 REQUIRED)
# message("Python: ${Python3_EXECUTABLE}")
# set(ENV{EXTRA_FLAGS} "${EM_CFLAGS}")
# execute_process(
#     COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/fix_compile_commands.py
#     WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
# )