CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Werror -O2
BUILD_DIR := ../../build/firmware-common
CONTRACT_TARGET := $(BUILD_DIR)/firmware-contract-tests
PARITY_TARGET := $(BUILD_DIR)/service-parity-tests
HOSTILE_TARGET := $(BUILD_DIR)/hostile-input-tests
RUNTIME_TARGET := $(BUILD_DIR)/runtime-orchestrator-tests
INGRESS_TARGET := $(BUILD_DIR)/runtime-ingress-tests
UI_TARGET := $(BUILD_DIR)/ui-controller-tests
BLUETOOTH_PROFILE_TARGET := $(BUILD_DIR)/bluetooth-profile-gate-tests
BLUETOOTH_LISTEN_TARGET := $(BUILD_DIR)/bluetooth-listen-policy-tests
BLUETOOTH_DEFERRAL_TARGET := $(BUILD_DIR)/bluetooth-retry-deferral-tests
WIFI_PORTAL_TARGET := $(BUILD_DIR)/wifi-portal-policy-tests
NOISE_TARGET := $(BUILD_DIR)/local-noise-source-tests
IMAGE_DIMS_TARGET := $(BUILD_DIR)/image-dims-tests
STATION_BINDING_TARGET := $(BUILD_DIR)/station-binding-tests
SLOT_MIGRATION_TARGET := $(BUILD_DIR)/station-slots-migration-tests
UPGRADE_POLICY_TARGET := $(BUILD_DIR)/upgrade-policy-tests
SENSOR_SNAPSHOT_TARGET := $(BUILD_DIR)/sensor-snapshot-tests
SCD4X_TARGET := $(BUILD_DIR)/scd4x-tests
AIR_TARGET := $(BUILD_DIR)/air-assessment-tests
ALERT_TARGET := $(BUILD_DIR)/alert-engine-tests
BME688_TARGET := $(BUILD_DIR)/bme688-tests
TOUCH_TARGET := $(BUILD_DIR)/touch-target-audit
# The slot store is a device header: it needs the generated catalog and a host
# stub of Arduino Preferences. Worth the extra include path — an upgrade that
# silently drops a user's stations is invisible until someone complains.
SLOT_MIGRATION_INCLUDES = -Itests/stubs -I../public-candidate/src $(INCLUDES)
UI_TRACE_TARGET := $(BUILD_DIR)/ui-controller-trace
NOISE_PROBE_TARGET := $(BUILD_DIR)/local-noise-source-probe
TARGETS := $(CONTRACT_TARGET) $(PARITY_TARGET) $(HOSTILE_TARGET) $(RUNTIME_TARGET) $(INGRESS_TARGET) $(UI_TARGET) $(BLUETOOTH_PROFILE_TARGET) $(BLUETOOTH_LISTEN_TARGET) $(BLUETOOTH_DEFERRAL_TARGET) $(WIFI_PORTAL_TARGET) $(NOISE_TARGET) $(IMAGE_DIMS_TARGET) $(STATION_BINDING_TARGET) $(SLOT_MIGRATION_TARGET) $(UPGRADE_POLICY_TARGET) $(SENSOR_SNAPSHOT_TARGET) $(SCD4X_TARGET) $(AIR_TARGET) $(ALERT_TARGET) $(BME688_TARGET) $(TOUCH_TARGET)
BLUETOOTH_PREFILL_TARGET := $(BUILD_DIR)/bluetooth-prefill-policy-tests
TARGETS += $(BLUETOOTH_PREFILL_TARGET)
BLUETOOTH_PULL_TARGET := $(BUILD_DIR)/bluetooth-pull-watchdog-tests
TARGETS += $(BLUETOOTH_PULL_TARGET)
INCLUDES := -Iinclude -I../generated

.PHONY: test test-san trace probe clean

# The same nineteen suites under AddressSanitizer + UBSan, no recovery: an
# overflow or a signed-arithmetic UB aborts the run instead of "working" on
# the host. Added after the 2026-09-08 audit found three such defects in
# sensor arithmetic that the plain build never reported. Separate build dir
# so the two flag sets never share objects.
test-san:
	$(MAKE) test BUILD_DIR=../../build/firmware-common-san \
	  CXXFLAGS="-std=c++17 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"

test: $(TARGETS)
	$(CONTRACT_TARGET)
	$(PARITY_TARGET)
	$(HOSTILE_TARGET)
	$(RUNTIME_TARGET)
	$(INGRESS_TARGET)
	$(UI_TARGET)
	$(BLUETOOTH_PROFILE_TARGET)
	$(BLUETOOTH_LISTEN_TARGET)
	$(BLUETOOTH_DEFERRAL_TARGET)
	$(BLUETOOTH_PREFILL_TARGET)
	$(BLUETOOTH_PULL_TARGET)
	$(WIFI_PORTAL_TARGET)
	$(NOISE_TARGET)
	$(IMAGE_DIMS_TARGET)
	$(STATION_BINDING_TARGET)
	$(SLOT_MIGRATION_TARGET)
	$(UPGRADE_POLICY_TARGET)
	$(SENSOR_SNAPSHOT_TARGET)
	$(SCD4X_TARGET)
	$(AIR_TARGET)
	$(ALERT_TARGET)
	$(BME688_TARGET)
	$(TOUCH_TARGET)

$(CONTRACT_TARGET): tests/firmware_contract_tests.cpp include/open_radio/firmware_contract.hpp include/open_radio/spsc_byte_ring.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(PARITY_TARGET): tests/service_parity_tests.cpp include/open_radio/service_contracts.hpp include/open_radio/service_adapters.hpp ../generated/service_golden_vectors.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(HOSTILE_TARGET): tests/hostile_input_tests.cpp include/open_radio/service_contracts.hpp include/open_radio/service_adapters.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(RUNTIME_TARGET): tests/runtime_orchestrator_tests.cpp include/open_radio/runtime_orchestrator.hpp ../generated/runtime_limits.hpp ../generated/runtime_soak_vectors.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(INGRESS_TARGET): tests/runtime_ingress_tests.cpp include/open_radio/runtime_ingress.hpp include/open_radio/runtime_service_bridge.hpp ../generated/runtime_ingress_limits.hpp ../generated/runtime_ingress_vectors.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(TOUCH_TARGET): tests/touch_target_audit.cpp include/open_radio/ui_controller.hpp include/open_radio/service_contracts.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/touch_target_audit.cpp -o $(TOUCH_TARGET)

$(UI_TARGET): tests/ui_controller_tests.cpp include/open_radio/ui_controller.hpp include/open_radio/service_contracts.hpp include/open_radio/service_adapters.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BLUETOOTH_PROFILE_TARGET): tests/bluetooth_profile_gate_tests.cpp include/open_radio/bluetooth_profile_gate.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BLUETOOTH_LISTEN_TARGET): tests/bluetooth_listen_policy_tests.cpp include/open_radio/bluetooth_listen_policy.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BLUETOOTH_DEFERRAL_TARGET): tests/bluetooth_retry_deferral_tests.cpp include/open_radio/bluetooth_retry_deferral.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BLUETOOTH_PREFILL_TARGET): tests/bluetooth_prefill_policy_tests.cpp include/open_radio/bluetooth_prefill_policy.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BLUETOOTH_PULL_TARGET): tests/bluetooth_pull_watchdog_tests.cpp include/open_radio/bluetooth_pull_watchdog.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(WIFI_PORTAL_TARGET): tests/wifi_portal_policy_tests.cpp include/open_radio/wifi_portal_policy.hpp include/open_radio/ui_controller.hpp include/open_radio/service_contracts.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(NOISE_TARGET): tests/local_noise_source_tests.cpp include/open_radio/local_noise_source.hpp include/open_radio/firmware_contract.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(IMAGE_DIMS_TARGET): tests/image_dims_tests.cpp include/open_radio/image_dims.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

trace: $(UI_TRACE_TARGET)

$(UI_TRACE_TARGET): tests/ui_controller_trace.cpp include/open_radio/ui_controller.hpp include/open_radio/service_contracts.hpp include/open_radio/service_adapters.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(STATION_BINDING_TARGET): tests/station_binding_tests.cpp include/open_radio/station_binding.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(SLOT_MIGRATION_TARGET): tests/station_slots_migration_tests.cpp tests/stubs/Preferences.h ../public-candidate/src/station_slots.hpp include/open_radio/station_binding.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SLOT_MIGRATION_INCLUDES) $< -o $@

$(UPGRADE_POLICY_TARGET): tests/upgrade_policy_tests.cpp include/open_radio/upgrade_policy.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(SENSOR_SNAPSHOT_TARGET): tests/sensor_snapshot_tests.cpp include/open_radio/sensor_snapshot.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(SCD4X_TARGET): tests/scd4x_tests.cpp include/open_radio/scd4x.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(AIR_TARGET): tests/air_assessment_tests.cpp include/open_radio/air_assessment.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

probe: $(NOISE_PROBE_TARGET)
	$(NOISE_PROBE_TARGET)

$(NOISE_PROBE_TARGET): tests/local_noise_source_probe.cpp include/open_radio/local_noise_source.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

clean:
	rm -rf $(BUILD_DIR)

$(ALERT_TARGET): tests/alert_engine_tests.cpp include/open_radio/alert_engine.hpp include/open_radio/air_assessment.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BME688_TARGET): tests/bme688_tests.cpp include/open_radio/bme688.hpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
