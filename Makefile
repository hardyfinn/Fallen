# Fallen for VCV Rack.
# Build:  export RACK_DIR=/path/to/Rack-SDK && make && make install   (or make dist)
# Tests:  make test   (host build of the engines, no Rack SDK needed)

RACK_DIR ?= ../..

# No -Isrc: our src/dsp headers must not shadow Rack's <dsp/...> on case-insensitive filesystems.
CXXFLAGS += -Wall -Wextra -Wno-unused-parameter $(EXTRA_CXXFLAGS)

SOURCES += $(wildcard src/*.cpp src/modules/*.cpp src/io/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += README.md

ifneq ($(wildcard $(RACK_DIR)/plugin.mk),)
include $(RACK_DIR)/plugin.mk
# Rack's compile.mk adds -std=c++11. We need C++17.
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS)) -std=c++17
endif

TEST_CXX ?= g++
TEST_SRC := $(wildcard tests/*.cpp) $(wildcard src/io/*.cpp)
build/fallen_tests: $(TEST_SRC) $(wildcard tests/*.hpp src/time/*.hpp src/dsp/*.hpp src/io/*.hpp)
	@mkdir -p build
	$(TEST_CXX) -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -o $@ $(TEST_SRC) -lpthread

.PHONY: test panels
test: build/fallen_tests
	./build/fallen_tests

# Regenerate res/*.svg and src/generated/Layout.hpp (needs Python 3 and fonttools).
panels:
	python3 scripts/make_panels.py
