CC = clang
CSTD = -std=c18
WARNINGS = -Wall -Wextra -Wshadow -pedantic
CPPFLAGS += $(shell pkg-config --cflags raylib) -Iinclude -Isrc -Itests
CFLAGS_RELEASE = $(CSTD) $(WARNINGS) -O3
CFLAGS_DEBUG = $(CSTD) $(WARNINGS) -g -O0 -DDEBUG
CFLAGS_ASAN = $(CFLAGS_DEBUG) -fsanitize=address,undefined -fno-omit-frame-pointer
LDLIBS += $(shell pkg-config --libs raylib) -lm
LDFLAGS_ASAN = -fsanitize=address,undefined

CORE_SRC := $(wildcard lib/*.c)
SRC := $(wildcard src/*.c) $(CORE_SRC)
TEST_NAMES := $(patsubst tests/%_test.c,%,$(wildcard tests/*_test.c))
RELEASE_OBJ := $(patsubst %.c,build/release/%.o,$(SRC))
DEBUG_OBJ := $(patsubst %.c,build/debug/%.o,$(SRC))
ASAN_OBJ := $(patsubst %.c,build/asan/%.o,$(SRC))

.PHONY: all debug asan headless test test-release test-debug test-asan test-timer test-ppu bench asm clean
all: gb
debug: gb-debug
asan: gb-asan
headless: build/release/gb-headless

gb: $(RELEASE_OBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
gb-debug: $(DEBUG_OBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
gb-asan: $(ASAN_OBJ)
	$(CC) $(LDFLAGS) $(LDFLAGS_ASAN) $^ $(LDLIBS) -o $@

define MODE
build/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS_$(2)) $$(CFLAGS) -MMD -MP -c $$< -o $$@

build/$(1)/gb-headless: build/$(1)/tests/core_runner.o $$(patsubst %.c,build/$(1)/%.o,$$(CORE_SRC))
	$$(CC) $$(LDFLAGS) $$(LDFLAGS_$(2)) $$^ $$(LDLIBS) -o $$@

build/$(1)/%_test: build/$(1)/tests/%_test.o $$(patsubst %.c,build/$(1)/%.o,$$(CORE_SRC))
	$$(CC) $$(LDFLAGS) $$(LDFLAGS_$(2)) $$^ $$(LDLIBS) -o $$@

build/$(1)/timer_test: build/$(1)/tests/timer_test.o build/$(1)/lib/timer.o
	$$(CC) $$(LDFLAGS) $$(LDFLAGS_$(2)) $$^ -o $$@

test-$(1): $$(addprefix build/$(1)/,$$(addsuffix _test,$$(TEST_NAMES))) build/$(1)/gb-headless
	@set -e; for test in $$(addprefix build/$(1)/,$$(addsuffix _test,$$(TEST_NAMES))); do UBSAN_OPTIONS=halt_on_error=1 ./$$$$test; done
	UBSAN_OPTIONS=halt_on_error=1 python3 tests/core_check.py build/$(1)/gb-headless
endef

$(eval $(call MODE,release,RELEASE))
$(eval $(call MODE,debug,DEBUG))
$(eval $(call MODE,asan,ASAN))

test: test-release
test-timer: build/release/timer_test
	./build/release/timer_test
test-ppu: build/release/ppu_test
	./build/release/ppu_test
bench: headless
	./build/release/gb-headless --cycles 41943040

asm: $(patsubst %.c,build/release/%.s,$(SRC))
build/release/%.s: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_RELEASE) $(CFLAGS) -S -g -fverbose-asm -o $@ $<

.SECONDARY:
-include $(shell find build -name '*.d' 2>/dev/null)

clean:
	rm -rf build gb gb-debug gb-asan
