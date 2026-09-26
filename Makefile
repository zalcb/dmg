CC = clang
CSTD = -std=c18
WARNINGS = -Wall -Wextra -Wshadow -pedantic
CPPFLAGS += -Isrc -Itests
RAYLIB_CFLAGS = $(shell pkg-config --cflags raylib)
RAYLIB_LIBS = $(shell pkg-config --libs raylib)
CFLAGS_RELEASE = $(CSTD) $(WARNINGS) -O3
CFLAGS_DEBUG = $(CSTD) $(WARNINGS) -g -O0 -DDEBUG
CFLAGS_ASAN = $(CFLAGS_DEBUG) -fsanitize=address,undefined -fno-omit-frame-pointer
LDLIBS += -lm
LDFLAGS_ASAN = -fsanitize=address,undefined

CORE_SRC := $(filter-out %_test.c,$(wildcard core/*/*.c))
SRC := $(wildcard src/*.c) $(CORE_SRC)
TEST_SRC := $(wildcard tests/*_test.c core/*/*_test.c)
TEST_NAMES := $(patsubst %_test.c,%,$(notdir $(TEST_SRC)))
RELEASE_OBJ := $(patsubst %.c,build/release/%.o,$(SRC))
DEBUG_OBJ := $(patsubst %.c,build/debug/%.o,$(SRC))
ASAN_OBJ := $(patsubst %.c,build/asan/%.o,$(SRC))

.PHONY: all debug asan headless test test-release test-debug test-asan test-timer test-ppu bench asm compile-commands clean
all: gb
debug: gb-debug
asan: gb-asan
headless: build/release/gb-headless

gb: $(RELEASE_OBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) $(RAYLIB_LIBS) -o $@
gb-debug: $(DEBUG_OBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) $(RAYLIB_LIBS) -o $@
gb-asan: $(ASAN_OBJ)
	$(CC) $(LDFLAGS) $(LDFLAGS_ASAN) $^ $(LDLIBS) $(RAYLIB_LIBS) -o $@

$(filter %/src/main.o,$(RELEASE_OBJ) $(DEBUG_OBJ) $(ASAN_OBJ)): CPPFLAGS += $(RAYLIB_CFLAGS)

define MODE
build/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS_$(2)) $$(CFLAGS) -MMD -MP -c $$< -o $$@

build/$(1)/gb-headless: build/$(1)/tests/core_runner.o $$(patsubst %.c,build/$(1)/%.o,$$(CORE_SRC))
	$$(CC) $$(LDFLAGS) $$(LDFLAGS_$(2)) $$^ $$(LDLIBS) -o $$@

test-$(1): $$(addprefix build/$(1)/,$$(addsuffix _test,$$(TEST_NAMES))) build/$(1)/gb-headless
	@set -e; for test in $$(addprefix build/$(1)/,$$(addsuffix _test,$$(TEST_NAMES))); do UBSAN_OPTIONS=halt_on_error=1 ./$$$$test; done
	UBSAN_OPTIONS=halt_on_error=1 python3 tests/core_check.py build/$(1)/gb-headless
endef

$(eval $(call MODE,release,RELEASE))
$(eval $(call MODE,debug,DEBUG))
$(eval $(call MODE,asan,ASAN))

define TEST
build/$(1)/$(notdir $(3:.c=)): build/$(1)/$(3:.c=.o) $$(patsubst %.c,build/$(1)/%.o,$$(CORE_SRC))
	$$(CC) $$(LDFLAGS) $$(LDFLAGS_$(2)) $$^ $$(LDLIBS) -o $$@
endef

$(foreach source,$(TEST_SRC),$(eval $(call TEST,release,RELEASE,$(source))))
$(foreach source,$(TEST_SRC),$(eval $(call TEST,debug,DEBUG,$(source))))
$(foreach source,$(TEST_SRC),$(eval $(call TEST,asan,ASAN,$(source))))

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

build/release/src/main.s: CPPFLAGS += $(RAYLIB_CFLAGS)

compile-commands:
	python3 -c 'import json,os,shlex,sys; cc=shlex.split(sys.argv[1]); flags=shlex.split(sys.argv[2]); json.dump([{"directory":os.getcwd(),"arguments":cc+flags+["-c",f],"file":f} for f in sys.argv[3:]],open("compile_commands.json","w"),indent=2)' "$(CC)" "$(CPPFLAGS) $(RAYLIB_CFLAGS) $(CFLAGS_RELEASE) $(CFLAGS)" $(SRC) $(TEST_SRC) tests/core_runner.c

.SECONDARY:
-include $(shell find build -name '*.d' 2>/dev/null)

clean:
	rm -rf build gb gb-debug gb-asan
