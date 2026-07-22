# Makefile — two targets for the NT4 native Instagram client.
#
#   make test   -> build core/ + tests with clang + ASan/UBSan on the host, run
#   make nt4    -> cross-compile the NT4 app to dist/app.exe (i686-w64-mingw32)
#   make clean
#
# The host test build is where core/ (raster, http, ws, json, model) and img/
# get real coverage at full speed. NT4 is for final integration only.

# ---- host native test build (clang) ----
CC        ?= clang
SAN        = -fsanitize=address,undefined -fno-omit-frame-pointer
HOSTFLAGS  = -std=c11 -g -O1 -Wall -Wextra -Werror $(SAN)

CORE_SRC   = $(wildcard core/*.c)
IMG_SRC    = $(wildcard img/*.c)
TEST_SRC   = $(wildcard tests/*.c)
TEST_BIN   = build/coretest

# ---- NT4 cross build (mingw-w64) ----
# Freestanding: Homebrew's mingw links the UCRT (api-ms-win-crt-*.dll), absent on
# NT4. We drop the C runtime (-nostdlib), supply our own entry + heap/mem shims
# in pal/nt4_crt.c, and import only NT4-native DLLs. libgcc (arithmetic helpers,
# __chkstk) is pulled in by explicit path since -nostdlib drops the default libs.
XCC        = i686-w64-mingw32-gcc
LIBGCC     = $(shell $(XCC) -print-libgcc-file-name)
NT4DEFS    = -D_WIN32_WINNT=0x0400 -DWINVER=0x0400
# -march=i486: guest runs -cpu 486 (only CPU that survives NT4 setup); target
# i486 so our code emits no CMOV/P6 ops. -fno-builtin/-fno-tree-loop-... keep the
# byte-loop mem* shims from being turned back into self-recursive mem* calls.
NT4WARN    = -std=c11 -Wall -Wextra -march=i486 -mtune=i486 \
             -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns
NT4LDFLAGS = -nostdlib -Wl,-e,_mainCRTStartup -Wl,--subsystem,windows \
             -Wl,--major-subsystem-version=4,--minor-subsystem-version=0 \
             -Wl,--major-os-version=4,--minor-os-version=0
APP_SRC    = $(wildcard core/*.c) $(wildcard img/*.c) $(wildcard pal/*.c) $(wildcard ui/*.c)
APP_INC    = -Icore -Iimg -Ipal -Iui
APP_LIBS   = -lgdi32 -lcomctl32 -ladvapi32 -luser32 -lkernel32 -lwsock32 $(LIBGCC)
APP_EXE    = dist/app.exe

# milestone-4 bring-up: sockets + http + json + model against a plain-HTTP mock
# server (tools/mock_graph_server.py), no window -> no gdi32/comctl32 import.
NET_SRC    = core/json.c core/http.c core/model.c \
             pal/nt4_crt.c pal/pal_common_win32.c pal/net_win32.c \
             net/nettest_main.c
NET_INC    = -Icore -Ipal -Inet
NET_LIBS   = -lwsock32 -luser32 -lkernel32 $(LIBGCC)
NET_EXE    = dist/nettest.exe

# mbedTLS 2.28 LTS, vendored (gitignored) under third_party/, cross-compiled as
# a static lib with our own minimal config (net/mbedtls_config_nt4.h) + platform
# glue (net/mbedtls_platform_nt4.c: time/gmtime_r/snprintf — everything else
# mbedTLS needs, calloc/free/mem*/str*, already lives in pal/nt4_crt.c). A
# static lib needs no entry point / -nostdlib — that's only an EXE link concern
# — so it compiles with plain freestanding+i486 flags, no NT4LDFLAGS.
MBEDTLS_DIR  = third_party/mbedtls
MBEDTLS_SRC  = $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBEDTLS_OBJD = build/mbedtls_obj
MBEDTLS_OBJ  = $(patsubst $(MBEDTLS_DIR)/library/%.c,$(MBEDTLS_OBJD)/%.o,$(MBEDTLS_SRC))
MBEDTLS_INC  = -I$(MBEDTLS_DIR)/include -Inet
MBEDTLS_DEFS = -DMBEDTLS_CONFIG_FILE='"mbedtls_config_nt4.h"'
MBEDTLS_LIB  = build/libmbedtls_nt4.a

.PHONY: test nt4 nettest mbedtls preview clean run-vm

# Host-side render of the feed (ui/feed.c is pure raster) -> PPM -> PNG.
preview: | build
	$(CC) -std=c11 -O2 -Wall $(CORE_SRC) $(IMG_SRC) ui/feed.c tools/render_preview.c \
	  -Icore -Iimg -Iui -o build/preview
	./build/preview build/feed.ppm 340 600
	@magick build/feed.ppm build/feed.png 2>/dev/null && echo "wrote build/feed.png" \
	  || echo "(install imagemagick for PNG; PPM at build/feed.ppm)"

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(CORE_SRC) $(IMG_SRC) $(TEST_SRC) | build
	$(CC) $(HOSTFLAGS) $(CORE_SRC) $(IMG_SRC) $(TEST_SRC) -Icore -Iimg -o $@

nt4: | dist
	$(XCC) $(APP_SRC) -o $(APP_EXE) \
	  $(NT4DEFS) $(NT4WARN) $(APP_INC) \
	  $(NT4LDFLAGS) $(APP_LIBS)
	@echo "built $(APP_EXE):"; \
	  i686-w64-mingw32-size $(APP_EXE) 2>/dev/null || ls -l $(APP_EXE)

nettest: | dist
	$(XCC) $(NET_SRC) -o $(NET_EXE) \
	  $(NT4DEFS) $(NT4WARN) $(NET_INC) \
	  $(NT4LDFLAGS) $(NET_LIBS)
	@echo "built $(NET_EXE):"; \
	  i686-w64-mingw32-size $(NET_EXE) 2>/dev/null || ls -l $(NET_EXE)

mbedtls: $(MBEDTLS_LIB)

$(MBEDTLS_LIB): $(MBEDTLS_OBJ)
	i686-w64-mingw32-ar rcs $@ $(MBEDTLS_OBJ)
	@echo "built $@:"; ls -l $@

$(MBEDTLS_OBJD)/%.o: $(MBEDTLS_DIR)/library/%.c | $(MBEDTLS_OBJD)
	$(XCC) -c $< -o $@ $(NT4DEFS) $(NT4WARN) $(MBEDTLS_DEFS) $(MBEDTLS_INC)

$(MBEDTLS_OBJD):
	@mkdir -p $(MBEDTLS_OBJD)

build:
	@mkdir -p build
dist:
	@mkdir -p dist

clean:
	rm -rf build/* dist/*.exe

run-vm:
	./tools/run-vm.sh
