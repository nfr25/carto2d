cmake_minimum_required(VERSION 3.21)
project(Editor2D LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# render2d: its libraries are built inside our build (notions §2).
# Only the libraries: render2d skips its programs and tests when it is not
# the main project (R2D_BUILD_PROGRAMS, render2d notions §49).
# Where: the git submodule render2d/ of carto2D's repository (render2d's
# repository at a tag, notions §79), else ../render2d/render2d beside this
# folder (the working copy at home). Overridden by
# make CMAKE_ARGS=-DR2D_DIR=C:/elsewhere/render2d
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render2d/render2d/CMakeLists.txt")
    set(R2D_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/render2d/render2d")
else()
    set(R2D_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../render2d/render2d")
endif()
set(R2D_DIR "${R2D_DEFAULT}" CACHE PATH "render2d C++ project")
message(STATUS "render2d: ${R2D_DIR}")
if(NOT EXISTS "${R2D_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "render2d not found in '${R2D_DIR}' (set R2D_DIR)")
endif()
# Second argument: its build files go inside ours (mandatory, the source
# folder is not below this one).
add_subdirectory(${R2D_DIR} ${CMAKE_BINARY_DIR}/render2d)

# ---------------------------------------------------------------------------
# GLFW and Dear ImGui (docking branch): same versions as editor3d.
# ---------------------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE)

# nlohmann/json: commands and events as JSON (notions §16, §17). Same
# release as editor3d; a single header.
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(glfw imgui json)

# glad: OpenGL 4.5 core loader, copied from editor3d (third_party/glad).
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# Dear ImGui as a static library, with the GLFW + OpenGL3 backends.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)        # InputText on a std::string (the style dialog)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC glfw ${CMAKE_DL_LIBS})
target_compile_definitions(imgui PRIVATE GLFW_INCLUDE_NONE)

# ---------------------------------------------------------------------------
# Warnings on our own code only (not on ImGui, GLFW or render2d).
# ---------------------------------------------------------------------------
add_library(e2d_warnings INTERFACE)
target_compile_options(e2d_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)

# ---------------------------------------------------------------------------
# The editor
# ---------------------------------------------------------------------------
# The wanted view (notions §10): pure maths, tested without a window.
add_library(e2d_view STATIC src/view/Navigator.cpp src/view/DragGesture.cpp src/view/Tool.cpp)
target_include_directories(e2d_view PUBLIC src)
target_link_libraries(e2d_view PUBLIC r2d_core PRIVATE e2d_warnings)

# Commands, history, events (notions §15 to §17): no window, no OpenGL,
# tested on their own.
add_library(e2d_command STATIC
    src/command/Document.cpp
    src/command/History.cpp
    src/command/Registry.cpp
    src/command/LayerCommands.cpp
    src/command/SelectionCommands.cpp
    src/command/Commander.cpp
    src/event/EventBus.cpp)
target_include_directories(e2d_command PUBLIC src)
target_link_libraries(e2d_command PUBLIC nlohmann_json::nlohmann_json PRIVATE e2d_warnings)

# The render thread (notions §11).
find_package(Threads REQUIRED)

add_executable(editor2d
    src/main.cpp
    src/Application.cpp
    src/view/MapTexture.cpp
    src/worker/RenderWorker.cpp
    src/worker/QueryWorker.cpp
    src/worker/LayerSync.cpp
    src/worker/LayerProbe.cpp
    src/worker/DbConnection.cpp      # the connection string, the recent ones (§65)
    src/worker/DbBrowser.cpp         # a database's tables, in a thread (§65)
    src/panels/ConnectDialog.cpp     # File > Connect to database (§65)
    src/panels/LayerList.cpp         # a source's layers, to tick: shared (§65, §66)
    src/panels/LayerChooser.cpp      # the layers of a dropped GeoPackage (§66)
    src/view/SelectionOverlay.cpp
    src/view/ShapeOverlay.cpp
    src/panels/Toolbar.cpp
    src/panels/Icons.cpp             # the toolbar's icons, drawn by hand (§61)
    src/panels/StyleDialog.cpp
    src/panels/ClassifyPanel.cpp     # Categorize, Graduate (§48 to §50)
    src/panels/SelectionPanel.cpp
    src/panels/TablePanel.cpp        # the table of the selection (§60)
    src/panels/LayersPanel.cpp
    src/panels/LayerSymbol.cpp       # the symbol in front of a layer's name (§68)
    src/panels/SymbolImage.cpp       # a point symbol's image, PNG or SVG (§78)
    src/view/SymbolTextures.cpp      # the Layers panel's image symbols on the card (§78)
    src/panels/CrsDialog.cpp         # View > Map system > Other... (§70)
    src/panels/PrintDialog.cpp       # File > Print... (§77)
    src/panels/HistoryPanel.cpp
    src/panels/Workspace.cpp
    src/window/Window.cpp
    src/window/FileDialog.cpp        # Windows' "Open" / "Save as" window (§53)
    src/window/Platform.cpp          # the executable's folder, the browser (§71)
    src/window/ImGuiLayer.cpp)

# Our folders (view/, window/, worker/, command/, event/, panels/) never share a
# name with render2d's (app/, backend/, canvas/, core/, map/, render/, style/).
target_include_directories(editor2d PRIVATE src)
# §79 (7a): the program is carto2D; the target keeps its name (the sources and
# the history stay editor2d's), the file it makes is carto2d(.exe).
set_target_properties(editor2d PROPERTIES OUTPUT_NAME carto2d)
target_link_libraries(editor2d PRIVATE
    e2d_view e2d_command Threads::Threads
    r2d_map r2d_cairo                           # render2d: the map, and Cairo images
    glad glfw imgui
    e2d_warnings)
target_compile_definitions(editor2d PRIVATE GLFW_INCLUDE_NONE)
# GCC 16, Release only: a false "may be used uninitialized" deep inside
# std::variant / std::optional of render2d's style model, copied around by
# the legend dialog. Not our bug; silenced for this one file.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(src/panels/StyleDialog.cpp PROPERTIES COMPILE_OPTIONS -Wno-maybe-uninitialized)
endif()
if(WIN32)
    target_link_libraries(editor2d PRIVATE comdlg32 shell32)   # GetOpenFileNameW (§53), ShellExecuteW (§71)
    # §71: the icon, a resource compiled by windres (CMake's RC language).
    enable_language(RC)
    target_sources(editor2d PRIVATE src/window/editor2d.rc)
    # §71: Release is a windowed program (no black console beside it when
    # started from the Explorer); Debug keeps its console, for our printf.
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(editor2d PROPERTIES WIN32_EXECUTABLE ON)
    endif()
endif()

# No default configuration: without a .yaml on the command line, editor2d
# starts on an empty map (notions §13, 0.5.4).

# ---------------------------------------------------------------------------
# Tests: plain programs, no window. `make test` runs them.
# ---------------------------------------------------------------------------
enable_testing()

# Checks that render2d's headers and libraries are reachable from here,
# with the numbers of notions §7.
add_executable(link_test tests/LinkTest.cpp)
target_include_directories(link_test PRIVATE src)
target_link_libraries(link_test PRIVATE r2d_core r2d_gdal r2d_cairo e2d_warnings)
add_test(NAME link COMMAND link_test)

# The wanted view and the image placement, with the numbers of notions §10.
add_executable(navigator_test tests/NavigatorTest.cpp)
target_link_libraries(navigator_test PRIVATE e2d_view e2d_warnings)
add_test(NAME navigator COMMAND navigator_test)

# The two stacks, gesture by gesture (notions §15), and the events (§17).
add_executable(history_test tests/HistoryTest.cpp)
target_link_libraries(history_test PRIVATE e2d_command e2d_warnings)
add_test(NAME history COMMAND history_test)

# JSON -> command, refusals, unique layer ids (notions §16).
add_executable(registry_test tests/RegistryTest.cpp)
target_link_libraries(registry_test PRIVATE e2d_command e2d_warnings)
add_test(NAME registry COMMAND registry_test)

# selection.set: the four modes, undo, refusals (notions §22).
add_executable(selection_test tests/SelectionTest.cpp)
target_link_libraries(selection_test PRIVATE e2d_command e2d_warnings)
add_test(NAME selection COMMAND selection_test)

# layer.add, layer.remove, layer.move, with a fake probe (notions §38).
add_executable(layer_commands_test tests/LayerCommandsTest.cpp)
target_link_libraries(layer_commands_test PRIVATE e2d_command e2d_warnings)
add_test(NAME layer_commands COMMAND layer_commands_test)

# A thread's Map in line with the Document's list: the example of notions §37.
add_executable(layer_sync_test tests/LayerSyncTest.cpp src/worker/LayerSync.cpp)
target_include_directories(layer_sync_test PRIVATE src)
target_link_libraries(layer_sync_test PRIVATE r2d_map e2d_warnings)
add_test(NAME layer_sync COMMAND layer_sync_test)

add_executable(print_test tests/PrintTest.cpp src/worker/RenderWorker.cpp src/worker/LayerSync.cpp)   # §77
target_include_directories(print_test PRIVATE src)
target_link_libraries(print_test PRIVATE r2d_map r2d_cairo e2d_warnings Threads::Threads)
add_test(NAME print COMMAND print_test)

# The symbol of a layer: which rule, which symbolizer, which colours (notions §68).
add_executable(layer_symbol_test tests/LayerSymbolTest.cpp src/panels/LayerSymbol.cpp)
target_include_directories(layer_symbol_test PRIVATE src)
target_link_libraries(layer_symbol_test PRIVATE r2d_map r2d_cairo e2d_warnings)   # §78: symbolPixels
add_test(NAME layer_symbol COMMAND layer_symbol_test)

# A point symbol's image in the legend dialog: paths and checks (notions §78).
add_executable(symbol_image_test tests/SymbolImageTest.cpp src/panels/SymbolImage.cpp)
target_include_directories(symbol_image_test PRIVATE src)
target_link_libraries(symbol_image_test PRIVATE r2d_map e2d_warnings)
add_test(NAME symbol_image COMMAND symbol_image_test)

# A click or a drag: the 4-pixel threshold (notions §20).
add_executable(click_test tests/ClickTest.cpp)
target_link_libraries(click_test PRIVATE e2d_view e2d_warnings)
add_test(NAME click COMMAND click_test)

# Rectangle, zoom window, polygon, Esc (notions §27 to §29).
add_executable(tool_test tests/ToolTest.cpp)
target_link_libraries(tool_test PRIVATE e2d_view e2d_warnings)
add_test(NAME tool COMMAND tool_test)

# "Connect to database": the connection string, the recent ones, the thread
# (notions §65). E2D_TEST_PG="PG:..." also lists a real database's tables.
add_executable(connection_test tests/ConnectionTest.cpp src/worker/DbConnection.cpp src/worker/DbBrowser.cpp)
target_include_directories(connection_test PRIVATE src)
target_link_libraries(connection_test PRIVATE r2d_gdal Threads::Threads e2d_warnings)
add_test(NAME connection COMMAND connection_test)
