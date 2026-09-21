CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -pedantic

ROOT := ..
BUILD := $(ROOT)/build/renderer
INCLUDES := -Iinclude -Igenerated
# Host harness only: the sourced CO2 thresholds live in the firmware tree and
# are not copied here. The renderer library itself never sees this path.
HOST_INCLUDES := $(INCLUDES) -I$(ROOT)/firmware/common/include
CORE := src/renderer.cpp
HOST := host/main.cpp
TEST := tests/renderer_test.cpp
HOST_BINARY := $(BUILD)/open-radio-render
TEST_BINARY := $(BUILD)/renderer-tests
LAB_BINARY := $(BUILD)/open-radio-render-lab
LOGO_PACK_DIR := $(ROOT)/assets-local/station-logos

.PHONY: all host host-lab test determinism

all: host test

$(BUILD):
	mkdir -p $(BUILD)

$(HOST_BINARY): $(CORE) $(HOST) include/open_radio/renderer.hpp generated/ui_contract.hpp generated/icon_masks.hpp generated/fixture_now_playing.hpp generated/inter_font.hpp generated/kiara_sprite.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HOST_INCLUDES) $(CORE) $(HOST) -o $(HOST_BINARY)

$(TEST_BINARY): $(CORE) $(TEST) include/open_radio/renderer.hpp generated/ui_contract.hpp generated/icon_masks.hpp generated/fixture_now_playing.hpp generated/inter_font.hpp generated/kiara_sprite.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(CORE) $(TEST) -o $(TEST_BINARY)

host: $(HOST_BINARY)

# Private hardware-lab render lane: compiles the baked station logo pack
# behind OPEN_RADIO_HAS_LOGO_PACK. Requires the gitignored, rc1-lock
# excluded header at assets-local/station-logos/station_logo_pack.hpp, produced
# by `node scripts/generate-logo-pack.mjs`. The public build never sees it.
host-lab: | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HOST_INCLUDES) -DOPEN_RADIO_HAS_LOGO_PACK=1 -I$(LOGO_PACK_DIR) $(CORE) $(HOST) -o $(LAB_BINARY)

test: $(TEST_BINARY)
	$(TEST_BINARY)

determinism:
	node $(ROOT)/scripts/check-renderer-determinism.mjs
