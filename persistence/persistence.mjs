import {crc32, stableStringify} from "./checksum.mjs";
import {migrateConfig, validateCurrentConfig} from "./config-contract.mjs";

export const WritePhases = Object.freeze({
  BEFORE_WRITE: "BEFORE_WRITE",
  DURING_PAYLOAD: "DURING_PAYLOAD",
  BEFORE_COMMIT_MARKER: "BEFORE_COMMIT_MARKER",
  AFTER_COMMIT: "AFTER_COMMIT"
});

const rejectionReasons = Object.freeze({
  EMPTY: "EMPTY",
  ENVELOPE_INVALID: "ENVELOPE_INVALID",
  UNCOMMITTED: "UNCOMMITTED",
  CHECKSUM_MISMATCH: "CHECKSUM_MISMATCH",
  PAYLOAD_PARSE_ERROR: "PAYLOAD_PARSE_ERROR"
});

function emptyStorage() {
  return {slots: {A: null, B: null}};
}

function validEnvelope(slot, slotId) {
  return slot && typeof slot === "object" && slot.slotId === slotId && Number.isInteger(slot.generation) && slot.generation >= 1 && typeof slot.payloadText === "string" && typeof slot.checksum === "string" && (slot.commitMarker === "COMMITTED" || slot.commitMarker === null) && Number.isInteger(slot.writtenAtMs) && slot.writtenAtMs >= 0;
}

export function inspectSlot(slot, slotId, allowedStationIds = []) {
  if (slot === null || slot === undefined) return {valid: false, slotId, reason: rejectionReasons.EMPTY};
  if (!validEnvelope(slot, slotId)) return {valid: false, slotId, reason: rejectionReasons.ENVELOPE_INVALID};
  if (slot.commitMarker !== "COMMITTED") return {valid: false, slotId, reason: rejectionReasons.UNCOMMITTED};
  if (crc32(slot.payloadText) !== slot.checksum) return {valid: false, slotId, reason: rejectionReasons.CHECKSUM_MISMATCH};
  let parsed;
  try {
    parsed = JSON.parse(slot.payloadText);
  } catch {
    return {valid: false, slotId, reason: rejectionReasons.PAYLOAD_PARSE_ERROR};
  }
  const migration = migrateConfig(parsed, allowedStationIds);
  if (!migration.valid) return {valid: false, slotId, reason: migration.reason};
  return {
    valid: true,
    slotId,
    generation: slot.generation,
    config: migration.config,
    configSchemaVersion: migration.config.schemaVersion,
    migratedFromVersion: migration.migratedFromVersion
  };
}

export function selectBootConfig(storage = emptyStorage(), allowedStationIds = []) {
  const inspections = ["A", "B"].map(slotId => inspectSlot(storage?.slots?.[slotId], slotId, allowedStationIds));
  const valid = inspections.filter(item => item.valid).sort((left, right) => right.generation - left.generation || left.slotId.localeCompare(right.slotId));
  const rejections = inspections.filter(item => !item.valid).map(({slotId, reason}) => ({slotId, reason}));
  if (!valid.length) {
    return {
      result: {schemaVersion: 1, status: "SAFE_MODE", selectedSlotId: null, selectedGeneration: null, configSchemaVersion: null, migratedFromVersion: null, rejections},
      config: null
    };
  }
  const selected = valid[0];
  return {
    result: {
      schemaVersion: 1,
      status: "BOOTABLE",
      selectedSlotId: selected.slotId,
      selectedGeneration: selected.generation,
      configSchemaVersion: selected.configSchemaVersion,
      migratedFromVersion: selected.migratedFromVersion,
      rejections
    },
    config: structuredClone(selected.config)
  };
}

export function createCommittedSlot(slotId, generation, config, writtenAtMs) {
  const payloadText = stableStringify(config);
  return {slotId, generation, payloadText, checksum: crc32(payloadText), commitMarker: "COMMITTED", writtenAtMs};
}

export function applyAtomicWrite(storage, config, phase, nowMs, allowedStationIds = []) {
  if (!Object.values(WritePhases).includes(phase)) throw new Error(`unknown write phase: ${phase}`);
  if (!Number.isInteger(nowMs) || nowMs < 0) throw new Error("nowMs must be a non-negative integer");
  const validation = validateCurrentConfig(config, allowedStationIds);
  if (!validation.valid) throw new Error(`invalid current config: ${validation.errors.join("; ")}`);
  const baseStorage = storage?.slots ? structuredClone(storage) : emptyStorage();
  const current = selectBootConfig(baseStorage, allowedStationIds);
  const generation = (current.result.selectedGeneration || 0) + 1;
  const targetSlotId = current.result.selectedSlotId === "A" ? "B" : "A";
  if (phase === WritePhases.BEFORE_WRITE) {
    return {storage: baseStorage, write: {status: "INTERRUPTED", phase, targetSlotId, generation}};
  }
  const fullSlot = createCommittedSlot(targetSlotId, generation, config, nowMs);
  let nextSlot;
  if (phase === WritePhases.DURING_PAYLOAD) {
    nextSlot = {...fullSlot, payloadText: fullSlot.payloadText.slice(0, Math.max(1, Math.floor(fullSlot.payloadText.length / 2))), commitMarker: null};
  } else if (phase === WritePhases.BEFORE_COMMIT_MARKER) {
    nextSlot = {...fullSlot, commitMarker: null};
  } else {
    nextSlot = fullSlot;
  }
  const nextStorage = baseStorage;
  nextStorage.slots[targetSlotId] = nextSlot;
  return {
    storage: nextStorage,
    write: {status: phase === WritePhases.AFTER_COMMIT ? "COMMITTED" : "INTERRUPTED", phase, targetSlotId, generation}
  };
}
