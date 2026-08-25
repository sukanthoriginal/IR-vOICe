BINARYDIR := Release

#Toolchain
CC := clang
CXX := clang++
LD := $(CXX)
AR := ar
OBJCOPY := objcopy

#Additional flags
OPENCV_PREFIX := $(shell brew --prefix opencv)

PREPROCESSOR_MACROS := NDEBUG RELEASE NO_RASPICAM NO_HW_INPUT
INCLUDE_DIRS := $(OPENCV_PREFIX)/include/opencv5
LIBRARY_DIRS := $(OPENCV_PREFIX)/lib
LIBRARY_NAMES := opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui opencv_videoio opencv_calib ncurses pthread
ADDITIONAL_LINKER_INPUTS :=
MACOS_FRAMEWORKS :=
LINUX_PACKAGES :=

# Same optimization flags as release_rpi2.mak (what actually runs on the Pi 4)
# to keep floating-point behavior in the oscillator/scan code as close as
# possible to the real deployment.
CFLAGS := -ffunction-sections -O3 -std=c++11 -ffast-math -funsafe-math-optimizations -fomit-frame-pointer -funroll-loops
CXXFLAGS := -ffunction-sections -O3 -std=c++11 -ffast-math -funsafe-math-optimizations -fomit-frame-pointer -funroll-loops
ASFLAGS :=
LDFLAGS :=
COMMONFLAGS :=

#Additional options detected from testing the toolchain
USE_DEL_TO_CLEAN := 0
CP_NOT_AVAILABLE := 0
IS_LINUX_PROJECT := 0
