# guest.mk - core.wbx: the same dsda-doom, zlib and core sources as native.mk,
# built with miniBox's musl/libstdc++ guest toolchain (dsda-doom has C++ parts:
# its UMAPINFO, UDMF and GAMEINFO parsers) and linked at the guest base.
# Objects land in build/guest; core.wbx is checked by miniBox's check-wbx.sh
# (no thread-local storage, no %fs, no red zone) before it counts as built.
#
# Needs miniBox built WITH the C++ guest toolchain:
#   meson setup <miniBox>/build/meson-cpp <miniBox> -Dguest_cpp=true && ninja -C <miniBox>/build/meson-cpp
#
# Usage: make -f guest.mk -j$(nproc) [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B      := $(ROOT)/build/guest
MBUILD := $(MB)/build/meson-cpp
SR     := $(MBUILD)/guest-sysroot
GCCVER := $(shell gcc -dumpfullversion)
# the system's compilers over the guest sysroot, through its specs (musl's
# headers and start files, -mno-red-zone), as the DOSBox-X core's cross file
CC     := gcc -specs $(SR)/lib/musl-gcc.specs
CXX    := g++ -specs $(SR)/lib/musl-gcc.specs

# BizHawk waterbox's frozen guest flags, as miniBox's source/guest/meson.build
# gives them to a guest
WBFLAGS := -fvisibility=hidden -mcmodel=large -mno-red-zone -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
CXXINCS := -I$(SR)/include/c++/$(GCCVER) -I$(SR)/include/c++/$(GCCVER)/x86_64-linux-musl

DSDA_CFLAGS := $(WBFLAGS) $(DSDA_CFLAGS_COMMON) -w
DSDA_CXXFLAGS := $(WBFLAGS) $(DSDA_CXXFLAGS_COMMON) $(CXXINCS) -fexceptions -w
ZLIB_CFLAGS := $(WBFLAGS) $(ZLIB_CFLAGS_COMMON) -w
CORE_CFLAGS := $(WBFLAGS) $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall -Wno-unused-function
CORE_CXXFLAGS := $(WBFLAGS) $(DSDA_CXXFLAGS_COMMON) $(CXXINCS) $(MBINCS) -I. -fexceptions -Wall -Wextra

$(call flags_stamp,$(B),$(CC) | $(CXX) | $(DSDA_CFLAGS) | $(DSDA_CXXFLAGS) | $(ZLIB_CFLAGS) | $(CORE_CFLAGS) | $(CORE_CXXFLAGS))

DSDA_OBJS := $(patsubst $(DSDA)/%.c,$(B)/dsda/%.o,$(DSDA_C_SRCS)) $(patsubst $(DSDA)/%.cpp,$(B)/dsda/%.o,$(DSDA_CXX_SRCS))
ZLIB_OBJS := $(patsubst $(ZLIB)/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES) $(CORE_CXX_NAMES)))

all: $(B)/core.wbx $(WAD_DATA)

$(SR)/lib/libstdc++.a:
	@echo "miniBox's C++ guest toolchain is missing: $(SR)" >&2
	@echo "build it: meson setup $(MBUILD) $(MB) -Dguest_cpp=true && ninja -C $(MBUILD)" >&2
	@false

$(B)/dsda/%.o: $(DSDA)/%.c $(wildcard compat/*.h) $(PATCH_STAMP) $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(DSDA_CFLAGS) -c -o $@ $<

$(B)/dsda/%.o: $(DSDA)/%.cpp $(wildcard compat/*.h) $(PATCH_STAMP) $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CXX) $(DSDA_CXXFLAGS) -c -o $@ $<

$(B)/zlib/%.o: $(ZLIB)/%.c $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(ZLIB_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.cpp $(CORE_HDRS) $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_CXXFLAGS) -c -o $@ $<

# the guest kit's link recipe (the DOSBox-X core's): the large code model's
# --no-relax, the weak pthread pulls libgcc_eh needs, cxxglue for the unwinder
$(B)/core.wbx: $(CORE_OBJS) $(DSDA_OBJS) $(ZLIB_OBJS)
	$(CXX) -static -no-pie -Wl,--eh-frame-hdr -Wl,-O2 -Wl,--no-relax -Wl,-z,stack-size=8388608 \
		-T $(MB)/source/guest/linkscript.T \
		-Wl,-u,pthread_once -Wl,-u,pthread_cond_wait -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
		-o $@.tmp $^ $(MBUILD)/source/guest/cxxglue.c.o $(MBUILD)/source/guest/emulibc.c.o $(WRAP_FLAGS) \
		-L$(SR)/lib -lstdc++ -lm -lgcc -lgcc_eh -lc
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	mv $@.tmp $@

clean:
	rm -rf $(B)

.PHONY: all clean
