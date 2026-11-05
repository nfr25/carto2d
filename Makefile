# Thin wrapper around CMake: `make`, `make run`, `make test`, `make release`,
# `make clean`, and `make fresh` after unpacking a zip.
# CMake generates the real build files in $(BUILD_DIR).
# render2d is built inside it too (build/render2d/): from the submodule
# render2d/render2d, else from ../render2d/render2d (CMakeLists.txt says which).

BUILD_DIR  ?= build
BUILD_TYPE ?= Debug
CMAKE_ARGS ?=
# The map to show (a project .yaml). Empty: carto2D starts on an empty map (notions §13).
CONFIG     ?=
# render2d's sources, for `touch`: the submodule if there, else beside.
R2D        ?= $(if $(wildcard render2d/render2d/CMakeLists.txt),render2d/render2d,../render2d/render2d)

.PHONY: all configure build run test release clean touch fresh

all: build

# Configure every time: instant when nothing changed, and it repairs a build
# folder left half-configured by an interrupted run.
configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_ARGS)

build: configure
	cmake --build $(BUILD_DIR) --parallel

run: build
	./$(BUILD_DIR)/carto2d $(CONFIG)

# Builds and runs the tests (no window needed).
test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

release:
	$(MAKE) BUILD_DIR=build-release BUILD_TYPE=Release build

clean:
	rm -rf build build-release

# After unpacking a zip: its files keep the date they had on Claude's side,
# often OLDER than our last build, and ninja would then rebuild nothing (the
# 0.3.6 that stayed a 0.3.6). `touch` dates every source now; `fresh` does
# that, then the tests (Debug) and the Release. GLFW and ImGui stay built.
touch:
	find src tests CMakeLists.txt $(R2D)/src $(R2D)/CMakeLists.txt -type f -exec touch {} +

fresh: touch
	$(MAKE) test
	$(MAKE) release
