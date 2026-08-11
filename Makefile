# The DLL is always a 32-bit Windows binary. Linux and MSYS2 use the same
# MinGW-w64 cross-compiler; HOST_CXX builds a host-runnable registry test.
CXX := i686-w64-mingw32-g++

# Build products are isolated so the proxy can be copied directly into the game
# directory without carrying object files or maps.
TARGET := out/clientpatch/dinput8.dll
RELEASE_ASSET := out/distribution/Commonwealth-GA-Client-Patches-x86.dll
OBJ_DIR := obj/clientpatch
DEF_FILE := data/dinput8.def
TEST_TARGET := out/tests/feature_registry_test
ifeq ($(OS),Windows_NT)
TEST_TARGET := $(TEST_TARGET).exe
HOST_CXX ?= $(CXX)
else
HOST_CXX ?= g++
endif

# Keep this list explicit: every compiled feature and infrastructure component
# is visible during review, and no directory-wide wildcard can pull in code.
SOURCES := \
	lib/detours/modules.cpp \
	lib/detours/disasm.cpp \
	lib/detours/detours.cpp \
	src/Proxy/DInput8Proxy.cpp \
	src/Utils/SehStub/SehStub.cpp \
	src/Utils/Logger/Logger/FileLogger.cpp \
	src/Utils/ClientLogDirectory/ClientLogDirectory.cpp \
	src/Utils/CrashHandler/CrashHandler.cpp \
	src/Utils/ClientExecutableGuard/ClientExecutableGuard.cpp \
	src/Handshake/FeatureRegistry.cpp \
	src/Handshake/FeatureHandshakePatch.cpp \
	src/ClientPatches/MorphRebuildPerformance/MorphRebuildPerformancePatch.cpp \
	src/ClientPatches/ScopedWeaponVisibility/ScopedWeaponVisibilityPatch.cpp \
	src/ClientPatches/SpectatorNameplates/SpectatorNameplateDraw.cpp \
	src/ClientPatches/SpectatorNameplates/SpectatorNameplateSettings.cpp \
	src/ClientPatches/SpectatorNameplates/SpectatorNameplatesPatch.cpp \
	src/ClientPatches/UI/FovSlider/FovSliderPatch.cpp \
	src/Handshake/ClientFeatureRegistry.cpp \
	src/EntryPoint/DllMain.cpp

OBJECTS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
LINK_RSP := $(OBJ_DIR)/link.rsp

CPPFLAGS := -I. -I./lib/detours
CXXFLAGS := -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type
TEST_CXXFLAGS := -std=c++17 -Wall -Wextra -Werror
LDFLAGS := -shared -static -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup
LDLIBS := -lkernel32 -luser32 -ladvapi32 -lpsapi

# Debug retains symbols and a linker map. Release strips symbols while keeping
# the always-on patch-status and crash channels selected in DllMain.cpp.
ifdef GA_CLIENT_DEBUG
CXXFLAGS += -O0 -g3 -DGA_CLIENT_DEBUG
LDFLAGS += -Wl,-Map=$(TARGET).map
else
CXXFLAGS += -O2 -DNDEBUG
LDFLAGS += -s
endif

.PHONY: all debug release package-release test clientpatch clean cleanclientpatch

all: clientpatch

debug:
	# Mode targets clean first so objects cannot leak flags across builds.
	$(MAKE) cleanclientpatch
	$(MAKE) GA_CLIENT_DEBUG=1 clientpatch

release:
	$(MAKE) cleanclientpatch
	$(MAKE) clientpatch

package-release: release
	@mkdir -p $(dir $(RELEASE_ASSET))
	cp $(TARGET) $(RELEASE_ASSET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clientpatch: $(TARGET)

$(TARGET): $(OBJECTS) $(DEF_FILE)
	@mkdir -p $(dir $@)
	# A response file avoids Windows command-line length limits at link time.
	$(file >$(LINK_RSP),$(OBJECTS) $(DEF_FILE))
	$(CXX) $(LDFLAGS) -o $@ @$(LINK_RSP) $(LDLIBS)
	@rm -f $(LINK_RSP)

$(OBJ_DIR)/lib/detours/%.o: lib/detours/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -w -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TEST_TARGET): \
	tests/feature_registry_test.cpp \
	src/Handshake/FeatureRegistry.cpp \
	src/Handshake/FeatureRegistry.hpp \
	src/Handshake/FeatureMagic.hpp
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CPPFLAGS) $(TEST_CXXFLAGS) \
		tests/feature_registry_test.cpp src/Handshake/FeatureRegistry.cpp \
		-o $@

cleanclientpatch:
	rm -rf $(OBJ_DIR) out/clientpatch

clean: cleanclientpatch
	rm -rf out/tests out/distribution

-include $(DEPS)
