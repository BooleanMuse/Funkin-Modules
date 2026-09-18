# ============================================================================
# Funkin' Rack — VCV Rack plugin.
#
#   ./build.sh              -> plugin.so         (downloads the SDK if needed)
#   ./build.sh install      -> installs it into your Rack
#   ./build.sh test         -> the desktop tests
#   ./build.sh mockup       -> a picture of both panels
#
# Or, by hand:  RACK_DIR=/path/to/Rack-SDK make
# ============================================================================
RACK_DIR ?= third_party/Rack-SDK

FLAGS += -Isrc -Ithird_party

# The SDK pins -std=c++11 in compile.mk. Our engine is C++17 (std::filesystem
# walks the mod folders). This lands AFTER the SDK's own -std on the command
# line, because compile.mk appends $(FLAGS) below its CXXFLAGS, and the last one
# wins. It is the extension point the SDK leaves open for exactly this.
FLAGS += -std=c++17

# stb_image declares its whole API and we compile only the PNG half of it, so
# the rest are static functions nobody calls. Warning about them says nothing.
FLAGS += -Wno-unused-function

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk
