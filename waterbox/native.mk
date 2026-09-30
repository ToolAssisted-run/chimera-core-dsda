# native.mk - the native reference: the same dsda-doom, zlib and core sources
# as guest.mk, built for the host, plus the two harnesses (run-native drives the
# exports directly; run-wbx drives core.wbx through the miniBox host exactly as
# the frontend does). Objects land in build/native.
#
# Usage: make -f native.mk -j$(nproc) [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B := $(ROOT)/build/native
MBINCS := -Inative-shim -I$(MB)/source/guest/include -I$(MB)/extern/jsmn

DSDA_CFLAGS := $(DSDA_CFLAGS_COMMON) -g -w
DSDA_CXXFLAGS := $(DSDA_CXXFLAGS_COMMON) -g -w
ZLIB_CFLAGS := $(ZLIB_CFLAGS_COMMON) -w
CORE_CFLAGS := $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -g -Wall -Wno-unused-function -DDSDA_GATE_HOOKS

$(call flags_stamp,$(B),$(DSDA_CFLAGS) | $(DSDA_CXXFLAGS) | $(ZLIB_CFLAGS) | $(CORE_CFLAGS))

DSDA_OBJS := $(patsubst $(DSDA)/%.c,$(B)/dsda/%.o,$(DSDA_C_SRCS)) $(patsubst $(DSDA)/%.cpp,$(B)/dsda/%.o,$(DSDA_CXX_SRCS))
ZLIB_OBJS := $(patsubst $(ZLIB)/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES)))

all: $(B)/run-native $(B)/run-wbx $(WAD_DATA)

$(B)/dsda/%.o: $(DSDA)/%.c $(wildcard compat/*.h) $(PATCH_STAMP) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(DSDA_CFLAGS) -c -o $@ $<

$(B)/dsda/%.o: $(DSDA)/%.cpp $(wildcard compat/*.h) $(PATCH_STAMP) $(B)/flags
	@mkdir -p $(dir $@)
	g++ $(DSDA_CXXFLAGS) -c -o $@ $<

$(B)/zlib/%.o: $(ZLIB)/%.c $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(ZLIB_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/run-native.o: run-native.c gate-harness.h dsda-driver.h dsda-input.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -Wall -DGATE_NATIVE -I. -c -o $@ $<

$(B)/run-native: $(CORE_OBJS) $(DSDA_OBJS) $(ZLIB_OBJS) $(B)/core/run-native.o
	g++ -rdynamic -o $@ $^ $(WRAP_FLAGS) -lm

# run-wbx links the miniBox host library
MBHOST := $(MB)/build/meson-linux/source/host
$(B)/run-wbx: run-wbx.c dsda-input.c gate-harness.h dsda-driver.h dsda-input.h $(B)/flags
	gcc -O2 -Wall -I. -I$(MB)/source/host -o $@ run-wbx.c dsda-input.c $(MBHOST)/libminiboxhost.so -Wl,-rpath,$(MBHOST)

clean:
	rm -rf $(B)

.PHONY: all clean
