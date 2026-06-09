BINARYDIR := Release

#Toolchain
CC := gcc
CXX := g++
LD := $(CXX)
AR := ar
OBJCOPY := objcopy

#Additional flags
PREPROCESSOR_MACROS := NDEBUG RELEASE NO_RASPICAM
INCLUDE_DIRS := /usr/local/include
LIBRARY_DIRS := /usr/local/lib
LIBRARY_NAMES := rt ncurses pthread wiringPi
ADDITIONAL_LINKER_INPUTS :=
MACOS_FRAMEWORKS :=
LINUX_PACKAGES := opencv4

CFLAGS := -ffunction-sections -O2 -std=c++11 -ffast-math -funsafe-math-optimizations
CXXFLAGS := -ffunction-sections -O2 -std=c++11 -ffast-math -funsafe-math-optimizations
ASFLAGS :=
LDFLAGS := -Wl,-gc-sections
COMMONFLAGS :=

START_GROUP := -Wl,--start-group
END_GROUP := -Wl,--end-group

#Additional options detected from testing the toolchain
USE_DEL_TO_CLEAN := 0
CP_NOT_AVAILABLE := 0
IS_LINUX_PROJECT := 1
