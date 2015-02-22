CC := i686-w64-mingw32-gcc
CXX := i686-w64-mingw32-g++
WIDL := i686-w64-mingw32-widl


#DEBUG_FLAGS := -g -O0
DEBUG_FLAGS := -O2

CFLAGS_COMMON := -municode -DUNICODE -D_UNICODE -std=gnu99 -Id3d-headers $(DEBUG_FLAGS)
CXXFLAGS      := -municode -DUNICODE -D_UNICODE -std=c++11 -Id3d-headers -Imhook-lib -Wall -Wextra -fno-exceptions $(DEBUG_FLAGS)
CFLAGS_3RDPARTY := $(CFLAGS_COMMON) -w
CFLAGS := $(CFLAGS_COMMON) -Wall -Wextra -Wno-format
LDFLAGS := -static $(DEBUG_FLAGS)
LIBS    := -lgdi32 -luser32

MHOOK_SOURCES := $(wildcard mhook-lib/*.c mhook-lib/*.cpp disasm-lib/*.c)
MHOOK_OBJECTS := $(patsubst %.c,%.o,$(MHOOK_SOURCES))

all: screenview-x86.dll test.exe d3dcompiler-cli.exe

mhook-lib/%.o: mhook-lib/%.c
	@echo CC $<
	@$(CC) $(CFLAGS_3RDPARTY) -c -o "$@" "$<"

disasm-lib/%.o: disasm-lib/%.c
	@echo CC $<
	@$(CC) $(CFLAGS_3RDPARTY) -c -o "$@" "$<"

%.c.o %.c.d: %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -MMD -MF "$<.d" -MT "$<.o" -MP -c -o "$<.o" "$<"

%.cpp.o %.cpp.d: %.cpp
	@echo CXX $<
	@$(CXX) $(CXXFLAGS) -MMD -MF "$<.d" -MT "$<.o" -MP -c -o "$<.o" "$<"

screenview-x86.dll: src/view.cpp.o  \
                    src/logger.cpp.o \
                    src/duplication_source.cpp.o \
                    src/seven_dwm_source.cpp.o \
                    src/seven_dwm_injected.cpp.o \
                    src/injection.cpp.o \
                    src/win32.cpp.o \
                    $(MHOOK_OBJECTS)
	@echo LD $@
	@$(CXX) $(LDFLAGS) -shared -o "$@" $^ $(LIBS)

test.exe: test.c.o
	@echo LD $@
	@$(CC) $(LDFLAGS) -o "$@" $^

d3dcompiler-cli.exe: d3dcompiler-cli.c.o
	@echo LD $@
	@$(CC) $(LDFLAGS) -municode -o "$@" $^

clean:
	find . -depth -name '*.o' -delete -o -name '*.d' -delete
	rm -rf screenview-x86.dll test.exe d3dcompiler-cli.exe

d3d-headers/%.h: d3d-headers/%.idl
	@echo WIDL $<
	@$(WIDL) -h "$<" -o "$@"

-include $(shell find . -name '*.d')
