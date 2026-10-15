# ===========================================================================
#  ascent-ios — Theos library build (arm64 dylib for TrollFools injection)
#
#  macOS:   brew install theos   (or run the GitHub workflow)
#  build:   make clean && make
#  output:  .theos/obj/ascent.dylib
# ===========================================================================
TARGET = iphone:clang:latest:15.0
ARCHS  = arm64 arm64e

include $(THEOS)/makefiles/common.mk

LIBRARY_NAME = ascent

ascent_FILES = \
    src/main.mm \
    src/game.cpp \
    src/overlay.mm

ascent_FRAMEWORKS = UIKit Foundation CoreGraphics QuartzCore
ascent_CFLAGS = -fobjc-arc -std=c++17 -Isrc -O2 -fvisibility=hidden -Wno-deprecated-declarations
ascent_LDFLAGS = -undefined dynamic_lookup
ascent_LIBRARIES = c++

include $(THEOS_MAKE_PATH)/library.mk
