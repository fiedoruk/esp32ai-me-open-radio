#pragma once

#include <M5Unified.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <new>

#include "open_radio/air_assessment.hpp"
#include "open_radio/alert_engine.hpp"
#include "open_radio/bme688.hpp"
#include "open_radio/scd4x.hpp"
#include "open_radio/sensor_snapshot.hpp"

namespace open_radio::public_candidate {

// The task that talks to whatever is plugged into Port A, and the only code in
// this firmware allowed to touch that bus.
//
// Everything here is about containment. An optional sensor a person bought
// separately must never be able to take down a radio: not by wedging the bus,
// not by holding a line low, not by answering slowly, and not by failing in a
// way that reaches the thirty-second loop watchdog. So it runs on its own task,
// on core 0 with the network work, and publishes into a snapshot the owner loop
// reads without waiting (open_radio/sensor_snapshot.hpp).
//
// It uses M5Unified's OWN external bus, M5.Ex_I2C, and not a TwoWire of its
// own. That distinction cost a bench session and is worth writing down.
//
// M5Unified assigns I2C_NUM_1 to the INTERNAL bus — the PMIC, the real-time
// clock, the touch controller and the accelerometer on G21/G22 — and I2C_NUM_0
// to Port A. The first version of this task constructed `TwoWire bus_{1}` and
// called begin(32, 33), which does not create a second bus: it reconfigures the
// internal peripheral onto Port A's pins. The first two writes succeeded
// because the port had just been taken, and then every write failed as the
// library used port 1 for touch and power, both owners fighting over the same
// hardware. Measured 2026-07-29: probe fine, start fine, then `asked=0` for
// ever.
//
// Which is exactly the failure this file's own comment warned about — a wedged
// sensor taking the screen and the power management with it — arrived at from
// the inside. Using Ex_I2C means there is one owner of Port A and it is the
// library that assigned it. That is also why the PM2.5 module was rejected: it
// sits on the internal bus by design and no amount of care moves it (docs/120).
//
// 100 kHz, not 400. Neither the SCD4x nor the BME688 needs the speed, and a
// passive hub plus two 20 cm cables is enough capacitance to round the edges at
// the higher rate (docs/125 §8).

// Set while the lab console is streaming a framebuffer. Arduino's Serial is not
// synchronised between FreeRTOS tasks, so a sensor line printed from core 0
// during a screenshot interleaves INSIDE a line of hex and destroys it —
// measured 2026-07-29: one mangled line, 24 pixels short of a frame, which the
// host correctly refused rather than padding with black.
//
// Readings are not lost, only their logging: the task keeps measuring and
// publishing throughout, and a screenshot takes a second or two.
inline std::atomic<bool> gSerialQuiet{false};

inline constexpr int kPortASdaPin = 32;
inline constexpr int kPortASclPin = 33;
inline constexpr std::uint32_t kSensorBusHz = 100000;
inline constexpr std::uint8_t kEnvProAddress = 0x77;

class SensorTask {
 public:
  void begin(SensorSnapshot* snapshot) {
    if (snapshot == nullptr || handle_ != nullptr) return;
    snapshot_ = snapshot;
    // 4 KB is ample: no TLS, no allocation in the loop, and the deepest call is
    // an I2C transaction. Core 0 keeps it away from the Arduino loop entirely.
    if (xTaskCreatePinnedToCore(trampoline, "sensors", 4096, this, 1, &handle_, 0) != pdPASS) {
      Serial.println("sensor stage=task-failed");
      handle_ = nullptr;
    }
  }

 private:
  static void trampoline(void* argument) { static_cast<SensorTask*>(argument)->run(); }

  // Port A has no 5 V until it is asked for. The firmware shipped with
  // output_power=false, so the port has been dead since 0.1 — which is why no
  // module could ever have worked without this line, and why it is measured
  // rather than assumed.
  void powerPortA() {
    M5.Power.setExtOutput(true, m5::ext_PA);
    // The module's regulator and decoupling need a moment before it will answer.
    vTaskDelay(pdMS_TO_TICKS(250));
    // Reported, never gated on. M5Unified's per-port measurement is implemented
    // for the M5PowerHub only — on a Core2 it falls through and returns 0.0,
    // which the first version of this code read as "no power" and used to
    // refuse to look at the bus at all. Measured on hardware 2026-07-29: rail
    // enabled, volts 0.00, sensor never probed.
    //
    // The lesson is worth more than the fix: an unmeasurable quantity is not a
    // zero, and a value that cannot fail is not a check. The real proof that
    // Port A has power is that something on it answers, which is the next line
    // of this task and costs nothing extra.
    const float volts = M5.Power.getExtVoltage(m5::ext_PA);
    Serial.printf("sensor rail enabled=%d reported_volts=%.2f measurable=%s\n",
                  M5.Power.getExtOutput() ? 1 : 0, static_cast<double>(volts),
                  volts > 0.0f ? "yes" : "no-on-this-board");
  }

  // A slave holding SDA low after a reset or a hot unplug is not fixed by a
  // timeout — the bus is stuck until the device is clocked out of whatever
  // state it is in. Toggling SCL up to sixteen times with SDA released lets it
  // finish the byte it thinks it is sending, then a stop condition frees the
  // bus (docs/125 §8).
  static void clearBus() {
    pinMode(kPortASdaPin, INPUT_PULLUP);
    pinMode(kPortASclPin, OUTPUT);
    for (int pulse = 0; pulse < 16 && digitalRead(kPortASdaPin) == LOW; ++pulse) {
      digitalWrite(kPortASclPin, LOW);
      delayMicroseconds(5);
      digitalWrite(kPortASclPin, HIGH);
      delayMicroseconds(5);
    }
    // Stop: SDA low to high while SCL is high.
    pinMode(kPortASdaPin, OUTPUT);
    digitalWrite(kPortASdaPin, LOW);
    delayMicroseconds(5);
    digitalWrite(kPortASclPin, HIGH);
    delayMicroseconds(5);
    digitalWrite(kPortASdaPin, HIGH);
    delayMicroseconds(5);
  }

  // An address that acknowledges is not a part that is there. Measured
  // 2026-07-29: with only an SCD41 attached, a bare address probe reported a
  // BME688 at 0x77 as well. Presence is therefore a READING — the chip's own
  // identity register — and absence is the honest default.
  bool present(std::uint8_t address) {
    if (!M5.Ex_I2C.start(address, false, kSensorBusHz)) return false;
    M5.Ex_I2C.stop();
    return true;
  }

  // The BME688 answers 0x61 in register 0xD0. Anything else at that address is
  // something else, and something else is not a sensor this firmware knows how
  // to read.
  bool envProPresent() {
    if (!present(kEnvProAddress)) return false;
    const std::uint8_t chipId = M5.Ex_I2C.readRegister8(kEnvProAddress, 0xD0, kSensorBusHz);
    if (chipId != 0x61) {
      Serial.printf("sensor env_pro address_answered chip_id=0x%02x expected=0x61\n",
                    static_cast<unsigned>(chipId));
      return false;
    }
    return true;
  }

  bool write(std::uint8_t address, const std::uint8_t* bytes, std::size_t length) {
    if (!M5.Ex_I2C.start(address, false, kSensorBusHz)) return false;
    const bool written = M5.Ex_I2C.write(bytes, length);
    return M5.Ex_I2C.stop() && written;
  }

  // Every SCD4x command has an execution time before its answer exists — 1 ms
  // for the short ones by the datasheet. Reading immediately after the write
  // gets a NACK, which looks exactly like an absent sensor: measured on
  // hardware 2026-07-29, the part probed and started cleanly and then returned
  // nothing at all for fifty seconds. Five milliseconds is comfortably past the
  // requirement and costs nothing on a five-second cadence.
  std::size_t read(std::uint8_t address, std::uint8_t* out, std::size_t length) {
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!M5.Ex_I2C.start(address, true, kSensorBusHz)) return 0;
    const bool got = M5.Ex_I2C.read(out, length);
    M5.Ex_I2C.stop();
    return got ? length : 0;
  }

  bool writeRegister(std::uint8_t address, std::uint8_t reg, std::uint8_t value) {
    const std::uint8_t frame[2] = {reg, value};
    return write(address, frame, 2);
  }

  std::size_t readRegisters(std::uint8_t address, std::uint8_t reg,
                            std::uint8_t* out, std::size_t length) {
    if (!write(address, &reg, 1)) return 0;
    return read(address, out, length);
  }

  // Reads the calibration blocks, learns the gas variant, programs the
  // measurement profile. Returns false when any step refuses — and false means
  // the module stays Absent, because a BME688 that cannot hand over its
  // calibration cannot hand over anything the compensation could use.
  bool envProInitialise() {
    writeRegister(kBme688Address, kBme688ResetRegister, kBme688ResetCommand);
    vTaskDelay(pdMS_TO_TICKS(10));
    std::uint8_t variantId = 0xFF;
    if (readRegisters(kBme688Address, kBme688VariantRegister, &variantId, 1) != 1) {
      return false;
    }
    envProHighVariant_ = variantId == kBme688VariantHigh;
    std::uint8_t coeff1[kBme688Coeff1Length] = {0};
    std::uint8_t coeff2[kBme688Coeff2Length] = {0};
    std::uint8_t coeff3[kBme688Coeff3Length] = {0};
    if (readRegisters(kBme688Address, kBme688Coeff1Register, coeff1, sizeof coeff1) != sizeof coeff1 ||
        readRegisters(kBme688Address, kBme688Coeff2Register, coeff2, sizeof coeff2) != sizeof coeff2 ||
        readRegisters(kBme688Address, kBme688Coeff3Register, coeff3, sizeof coeff3) != sizeof coeff3) {
      return false;
    }
    envProCalibration_ = bme688ParseCalibration(coeff1, coeff2, coeff3);
    // A calibration block that transferred in full can still be nonsense (a
    // half-reset part, a bus glitch that read as zeros). p1 is a divisor in the
    // pressure compensation; zero there is a division by zero before the
    // plausibility check ever runs (Codex F10, 2026-09-08).
    if (!bme688CalibrationPlausible(envProCalibration_)) {
      Serial.println("sensor env_pro calibration=implausible treating=absent");
      return false;
    }
    if (!writeRegister(kBme688Address, kBme688ConfigRegister, kBme688ConfigValue) ||
        !writeRegister(kBme688Address, kBme688CtrlHumRegister, kBme688CtrlHumValue) ||
        !writeRegister(kBme688Address, kBme688GasWait0Register, kBme688GasWait100ms)) {
      return false;
    }
    // The heater set-point needs an ambient guess; 25 C is inside a degree of
    // any room this cube lives in, and the register is re-derived from the
    // real reading on every cycle below.
    const std::uint8_t heat =
        bme688HeaterResistance(envProCalibration_, kBme688HeaterTargetCelsius, 25);
    if (!writeRegister(kBme688Address, kBme688ResHeat0Register, heat)) return false;
    // run_gas sits at bit 5 on the BME688 and bit 4 on the BME680; nb_conv 0.
    const std::uint8_t runGas = envProHighVariant_ ? 0x20 : 0x10;
    if (!writeRegister(kBme688Address, kBme688CtrlGas1Register, runGas)) return false;
    envProStartedMs_ = millis();
    Serial.printf("sensor env_pro init variant=%s heat_reg=%u\n",
                  envProHighVariant_ ? "bme688" : "bme680",
                  static_cast<unsigned>(heat));
    return true;
  }

  // One forced measurement: trigger, wait out the conversion and the heater
  // dwell, read field 0, compensate. Returns false on any wire refusal or an
  // implausible result.
  bool envProMeasure(std::int32_t ambientCelsius, std::int32_t& temperatureCenti,
                     std::int32_t& pressureCentiHpa, std::int32_t& humidityCenti,
                     std::int32_t& gasOhm) {
    const std::uint8_t heat = bme688HeaterResistance(
        envProCalibration_, kBme688HeaterTargetCelsius, ambientCelsius);
    writeRegister(kBme688Address, kBme688ResHeat0Register, heat);
    if (!writeRegister(kBme688Address, kBme688CtrlMeasRegister, kBme688CtrlMeasForced)) {
      return false;
    }
    // T x2 + P x16 + H x1 is ~90 ms of conversion, the heater dwell 100 ms.
    vTaskDelay(pdMS_TO_TICKS(220));
    std::uint8_t data[kBme688DataLength] = {0};
    for (int attempt = 0; attempt < 5; ++attempt) {
      if (readRegisters(kBme688Address, kBme688DataRegister, data, sizeof data) != sizeof data) {
        return false;
      }
      if ((data[0] & 0x80U) != 0U) break;
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    const Bme688Raw raw = bme688ParseData(data, envProHighVariant_);
    if (!raw.newData) return false;
    std::int32_t tFine = 0;
    temperatureCenti =
        bme688CompensateTemperature(envProCalibration_, raw.temperatureAdc, tFine);
    const std::int32_t pressurePa =
        bme688CompensatePressure(envProCalibration_, raw.pressureAdc, tFine);
    const std::int32_t humidityMilli =
        bme688CompensateHumidity(envProCalibration_, raw.humidityAdc, tFine);
    if (!bme688Plausible(temperatureCenti, pressurePa, humidityMilli)) return false;
    pressureCentiHpa = pressurePa;  // Pa == centi-hPa, the snapshot's unit.
    humidityCenti = humidityMilli / 10;
    // Gas only when the heater reached its plate temperature and only on the
    // 688 formula; a 680's channel reports absent rather than wrong.
    gasOhm = (raw.gasValid && raw.heaterStable && envProHighVariant_)
                 ? bme688GasResistanceOhm(raw.gasAdc, raw.gasRange)
                 : kAbsentValue;
    return true;
  }

  void publish(SensorStatus status) {
    SensorReading reading = pending_;
    reading.status = status;
    reading.observedAtMs = millis();
    snapshot_->publish(0, reading);
    publishDerived(status);
  }

  // Everything the screen shows that is not a reading. Recomputed on every
  // publication because it costs a subtraction and a divide; the history behind
  // it only advances once a minute, which is the resolution at which "how fast
  // is this rising" means anything on a sensor that answers every five seconds.
  void publishDerived(SensorStatus status) {
    AirDerived derived;
    if (status != SensorStatus::Ok) {
      snapshot_->publishDerived(derived);
      return;
    }
    // The CO2 half is skipped without a CO2 reading; the history and everything
    // drawn from it is NOT. An ENV Pro on its own has no ppm and still has three
    // hours of pressure, and returning early here is what would have made the
    // tendency arrow permanently blank on exactly the module that feeds it.
    if (pending_.co2Ppm != kAbsentValue) {
      derived.band = static_cast<std::uint8_t>(co2Band(pending_.co2Ppm));
      derived.co2CeilingPpm = ceilingPpm_;
    }
    if (pending_.temperatureCentiCelsius != kAbsentValue &&
        pending_.humidityCentiPercent != kAbsentValue) {
      derived.dewPointCentiCelsius = dewPointCentiCelsius(
          pending_.temperatureCentiCelsius, pending_.humidityCentiPercent);
    }

    const std::uint32_t minute = millis() / 60000U;
    if (history_ == nullptr) {
      void* slab = heap_caps_malloc(sizeof(AirHistory),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (slab != nullptr) history_ = new (slab) AirHistory();
    }
    if (history_ != nullptr) {
      history_->record(minute, pending_.co2Ppm, pending_.pressureCentiHectopascal,
                       pending_.gasResistanceOhm);
    }
    // Three hours, which is the interval every barometer tendency has been
    // quoted over since the practice began, and the reason kAirHistorySamples is
    // 180 rather than something rounder.
    derived.pressureChangeCentiHpa3h =
        history_ != nullptr ? history_->pressureChangeCentiHectopascal(180)
                            : kAbsentValue;
    // Departure from the running median, as a percentage of it. Signed: the
    // element's resistance FALLS as reducing gas rises, so a negative departure
    // is the direction worth noticing, and the screen keeps the sign rather than
    // deciding here what it means.
    const std::int32_t gasBaseline =
        history_ != nullptr ? history_->gasBaselineOhm() : INT32_MIN;
    // GATED on the burn-in: a baseline built while the element is still
    // climbing would call the whole climb "cleaning". This gate has now been
    // lost twice — first to a silent text-replace no-op, then to the v74
    // revert, which took it down with the ICY change it had been committed
    // beside. Single-topic commits exist so a rollback removes one idea.
    if (gasBurnInDone_ && gasBaseline > 0 &&
        pending_.gasResistanceOhm != kAbsentValue) {
      // In 64-bit: the gas formula emits up to 102 400 000 ohm, so the
      // difference times 100 overflows int32 for exactly the swings worth
      // noticing — the same class as the delta*200 fix of 2026-07-30 a few
      // lines below (audit 2026-09-08, V2-02 / Codex F18).
      derived.gasDeparturePercent = static_cast<std::int32_t>(
          (static_cast<std::int64_t>(pending_.gasResistanceOhm) - gasBaseline) * 100 /
          gasBaseline);
    }
    if (!gasBurnInDone_ && envProStartedMs_ != 0 &&
        pending_.gasResistanceOhm != kAbsentValue) {
      derived.gasWarmupMinutes =
          static_cast<std::int32_t>((millis() - envProStartedMs_) / 60000U);
    }
    // Fifteen minutes of separation. Shorter and a single breath near the cube
    // becomes a trend; longer and airing a room takes too long to register.
    const std::int32_t ratePerMinute =
        history_ != nullptr ? history_->co2RatePpmPerMinute(15)
                            : AirHistory::kNoTrend;
    // Temperature slope over the same window, kept here rather than in the
    // history because the history stores what the AIR did and this is a
    // property of the module measuring it.
    if (pending_.temperatureCentiCelsius != kAbsentValue) {
      if (lastTemperatureMinute_ != minute && lastTemperatureCenti_ != kAbsentValue &&
          minute > lastTemperatureMinute_) {
        temperatureRatePerMinute_ =
            (pending_.temperatureCentiCelsius - lastTemperatureCenti_) /
            static_cast<std::int32_t>(minute - lastTemperatureMinute_);
      }
      if (lastTemperatureMinute_ != minute) {
        lastTemperatureMinute_ = minute;
        lastTemperatureCenti_ = pending_.temperatureCentiCelsius;
      }
    }
    if (ratePerMinute != AirHistory::kNoTrend) {
      derived.co2RatePpmPerHour = ratePerMinute * 60;
      // Only the next threshold ABOVE the present reading, and only while the
      // air is heading towards it. A projection towards a level already passed
      // would print "in 0 minutes" forever.
      for (const std::int32_t threshold : {1000, 1400, 2500}) {
        if (pending_.co2Ppm >= threshold) continue;
        const std::int32_t minutes =
            minutesToCo2Level(pending_.co2Ppm, ratePerMinute, threshold);
        if (minutes < 0) break;
        derived.co2NextThresholdPpm = threshold;
        derived.co2MinutesToNext = minutes;
        break;
      }
    }
    // Events: the things worth saying that no single reading shows. Wired here
    // rather than on the interface core because they need the history, and the
    // history lives with the task that fills it.
    //
    // Note what an event is and is not. The gas element is non-selective: it
    // reports that something reducing increased, and CANNOT name it. So the
    // strongest honest statement this hardware makes is "check what is
    // happening", never "there is carbon monoxide" — that needs a sensor this
    // device has not got and a certification it does not hold, and a person who
    // believes otherwise will not fit the alarm that would have saved them.
    AirEventInputs inputs;
    inputs.co2RatePpmPerMinute = ratePerMinute;
    inputs.temperatureRateCentiPerMinute = temperatureRatePerMinute_;
    inputs.gasResistanceOhm = pending_.gasResistanceOhm;
    inputs.gasBaselineOhm = gasBaseline;
    inputs.primaryTemperatureCentiCelsius = pending_.temperatureCentiCelsius;
    inputs.secondaryTemperatureCentiCelsius = secondaryTemperatureCentiCelsius_;
    const AirEvent event = detectAirEvent(inputs);
    derived.event = static_cast<std::uint8_t>(event);
    // Logged on every change, because an event that only ever appeared on a
    // screen nobody was looking at leaves no way to find out afterwards what
    // the room did.
    // Stack headroom to the local log once a minute (nothing leaves the device;
    // "telemetry" is the wrong word for it and the product has none): the
    // 4096-byte figure was chosen, never
    // measured, and docs/133 does not allow a trim without the measurement.
    // ESP-IDF's high-water mark is in BYTES (portSTACK_TYPE is uint8_t) —
    // noted because the vanilla-FreeRTOS words trap nearly corrupted the
    // arithmetic during the 2026-07-30 review.
    if (++stackReportCounter_ >= 12) {
      stackReportCounter_ = 0;
      if (!gSerialQuiet.load(std::memory_order_relaxed)) {
        Serial.printf("sensor stack_high_water_bytes=%u of=4096\n",
                      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        Serial.printf("sensor gas_state dep=%ld warm=%ld stable_min=%u done=%d\n",
                      static_cast<long>(derived.gasDeparturePercent),
                      static_cast<long>(derived.gasWarmupMinutes),
                      static_cast<unsigned>(gasStableMinutes_),
                      gasBurnInDone_ ? 1 : 0);
      }
    }
    if (event != lastEvent_) {
      if (!gSerialQuiet.load(std::memory_order_relaxed))
        Serial.printf("sensor event=%u co2_rate_per_min=%ld gas_ohm=%ld gas_baseline=%ld\n",
                    static_cast<unsigned>(event), static_cast<long>(ratePerMinute),
                    static_cast<long>(pending_.gasResistanceOhm),
                    static_cast<long>(inputs.gasBaselineOhm));
      lastEvent_ = event;
    }
    // The alert channel: one evaluation per publication, which is the five
    // second cadence its dwell counts are written against. Everything about
    // WHEN the device speaks is in here rather than at the fifteen places that
    // used to each own a threshold and none of the rate limiting.
    AlertInputs alertInputs;
    alertInputs.co2Ppm = pending_.co2Ppm;
    alertInputs.co2CeilingPpm = ceilingPpm_;
    alertInputs.co2RatePpmPerMinute = ratePerMinute;
    alertInputs.co2MinutesToNext = derived.co2MinutesToNext;
    alertInputs.temperatureCentiCelsius = pending_.temperatureCentiCelsius;
    alertInputs.temperatureRateCentiPerMinute = temperatureRatePerMinute_;
    alertInputs.humidityCentiPercent = pending_.humidityCentiPercent;
    alertInputs.dewPointCentiCelsius = derived.dewPointCentiCelsius;
    alertInputs.pressureChangeCentiHpa3h = derived.pressureChangeCentiHpa3h;
    alertInputs.gasResistanceOhm = pending_.gasResistanceOhm;
    // The alert path honours the same burn-in gate as the published departure:
    // without this, a humidity swing against a mid-climb median could voice a
    // gas alert while the screen says "calibrating" — two answers from one
    // number. No baseline, no gas rules; the engine's gasKnown handles the rest.
    alertInputs.gasBaselineOhm = gasBurnInDone_ ? inputs.gasBaselineOhm : kAbsentValue;
    alertInputs.secondaryTemperatureCentiCelsius = secondaryTemperatureCentiCelsius_;
    alertInputs.readingAgeSeconds = 0;
    const AlertVerdict verdict =
        alerts_.evaluate(alertInputs, alertPolicy_, millis() / 1000U);
    derived.alertId = static_cast<std::uint8_t>(verdict.id);
    derived.alertLevel = static_cast<std::uint8_t>(verdict.level);
    if (verdict.id != lastAlert_) {
      if (!gSerialQuiet.load(std::memory_order_relaxed))
        Serial.printf("alert id=%u level=%u co2=%ld rh=%ld dp3h=%ld\n",
                      static_cast<unsigned>(verdict.id),
                      static_cast<unsigned>(verdict.level),
                      static_cast<long>(pending_.co2Ppm),
                      static_cast<long>(pending_.humidityCentiPercent),
                      static_cast<long>(derived.pressureChangeCentiHpa3h));
      lastAlert_ = verdict.id;
    }
    snapshot_->publishDerived(derived);
  }

  void run() {
    powerPortA();
    clearBus();
    M5.Ex_I2C.begin(I2C_NUM_0, kPortASdaPin, kPortASclPin);

    const bool scd4x = present(kScd4xAddress);
    bool envPro = envProPresent();
    Serial.printf("sensor probe scd4x=%d env_pro=%d bus=%uHz\n", scd4x ? 1 : 0,
                  envPro ? 1 : 0, static_cast<unsigned>(kSensorBusHz));
    if (envPro && !envProInitialise()) {
      // Identity answered but the calibration would not come over: that is a
      // module this arithmetic must not touch, not a module to guess at.
      Serial.println("sensor env_pro init=failed");
      envPro = false;
    }
    if (!scd4x && !envPro) {
      // Nothing at either address is the normal state of a cube with nothing
      // attached. The rail stays on: a module plugged in later is a hot-plug
      // this firmware does not support, but leaving the port dead would
      // guarantee it.
      publish(SensorStatus::Absent);
      handle_ = nullptr;
      vTaskDelete(nullptr);
      return;
    }
    if (!scd4x) {
      // ENV Pro alone: no CO2 half at all. The measurement loop below carries
      // both parts and each of its steps is guarded, so it runs correctly with
      // either one missing — the guards are the same ones a mid-session cable
      // failure exercises.
      publish(SensorStatus::Absent);
      publishEnvPro(SensorStatus::Warming);
      runMeasurements(false, true);
      return;
    }

    // Stop first, and wait. An SCD4x keeps measuring across a reset of the
    // host, so on any boot after the first it is already in periodic mode — and
    // in that mode it refuses configuration commands. Measured 2026-07-29:
    // asc_off=0 on the second boot, silently, which would have shipped a sensor
    // whose calibration walks its own baseline down with no error and no
    // symptom (docs/125 §3). The datasheet allows 500 ms for the stop to take
    // effect.
    const auto stop = scd4xFrame(Scd4xCommand::StopPeriodicMeasurement);
    write(kScd4xAddress, stop.data(), stop.size());
    vTaskDelay(pdMS_TO_TICKS(600));

    // Which of the two parts is on the wire. The Unit CO2L carries an SCD41 and
    // measures to 5,000 ppm; the cheaper Unit CO2 carries an SCD40 and stops at
    // 2,000 — and stopping is exactly what it does, silently, holding 2,000
    // while the room goes higher. The screen has to be able to tell a reading
    // from a ceiling, so the firmware has to know which part it has.
    //
    // Asked while stopped, because like every other configuration command this
    // one is refused during periodic measurement. A part too old to implement
    // it leaves the variant Unknown, which is given the LOWER ceiling.
    {
      const auto ask = scd4xFrame(Scd4xCommand::GetSensorVariant);
      std::uint8_t answer[3] = {0, 0, 0};
      if (write(kScd4xAddress, ask.data(), ask.size())) {
        vTaskDelay(pdMS_TO_TICKS(2));
        if (read(kScd4xAddress, answer, 3) == 3) {
          variant_ = scd4xDecodeVariant(answer, 3);
        }
      }
      ceilingPpm_ = scd4xCeilingPpm(variant_);
      Serial.printf("sensor scd4x variant=%s ceiling_ppm=%ld\n",
                    variant_ == Scd4xVariant::Scd41   ? "scd41"
                    : variant_ == Scd4xVariant::Scd40 ? "scd40"
                                                      : "unknown",
                    static_cast<long>(ceilingPpm_));
      if (variant_ == Scd4xVariant::Unknown) {
        // An address that acknowledges is not a part. Measured 2026-07-30:
        // 0x62 ACKed with no module attached at all, and the old code carried
        // on into a measurement loop against nobody. The variant probe is the
        // SCD4x's identity check — both parts M5Stack ships answer it — so a
        // silent probe means a ghost, and a ghost is Absent.
        Serial.println("sensor scd4x address_answered variant_probe=failed treating=absent");
        publish(SensorStatus::Absent);
        if (envPro) {
          publishEnvPro(SensorStatus::Warming);
          runMeasurements(false, true);
        } else {
          handle_ = nullptr;
          vTaskDelete(nullptr);
        }
        return;
      }
    }

    // Temperature offset: READ and reported, never written from here.
    //
    // The part reads warm — it heats itself and sits in a shell. Measured on
    // this project's hardware 2026-07-29: a room the owner measured at 23.4-24
    // settled at 25.16 on the sensor, so about 1.4 degrees beyond the factory
    // default, and 29 before it was thermally settled.
    //
    // The first attempt at fixing that put the correction in a compile-time
    // constant and persisted it to the sensor's EEPROM. The owner caught what
    // that means for a system anybody downloads and flashes: the number would
    // have travelled inside the OS image and been written into whatever sensor
    // happened to be attached, permanently, on every boot. One person's
    // enclosure calibrated everybody's hardware.
    //
    // A temperature offset belongs to a specific sensor in a specific shell in
    // a specific spot. It is therefore a DEVICE SETTING, set by an explicit act
    // — "type what your thermometer says" — stored in this cube's own
    // configuration, and applied to the sensor in volatile memory at each boot.
    // Nothing permanent is done to hardware the user bought, nothing personal
    // travels in a published binary, and moving the sensor to another cube
    // correctly means calibrating again, because the enclosure changed.
    //
    // What IS universal, and therefore does belong in the firmware: the
    // self-heating of THIS MODULE MODEL. An SCD41 in an M5Stack Unit CO2L shell
    // on a Grove lead beside a Core2 runs warm by about the same amount for
    // everybody with that combination — it is a property of the part and its
    // enclosure, not of anyone's room. Sensirion's factory 4.00 is a generic
    // figure for an unknown enclosure; this one is measured for the enclosure
    // people actually buy.
    //
    // Derived 2026-07-29: settled reading 25.16 against an owner reference of
    // 23.4-24, with the sensor carrying the factory 400. 400 + (2516 - 2370)
    // gives 546, and the owner's own uncertainty puts it between 516 and 576.
    // ONE unit measured ONCE, which is thin for a shipped default and better
    // than a generic number for a different enclosure; the ENV Pro gives an
    // independent check.
    //
    // Applied VOLATILE, every boot, never persisted. The sensor keeps whatever
    // its manufacturer put in it, so the same part moved to somebody else's
    // system behaves as they intend, and no published image ever writes to
    // hardware the user bought.
    static constexpr std::int32_t kUnitCo2LSelfHeatingCenti = 546;

    // The read stays: knowing what offset a sensor is carrying is the first
    // thing anyone debugging a wrong temperature needs.
    {
      const auto query = scd4xFrame(Scd4xCommand::GetTemperatureOffset);
      std::uint8_t answer[3] = {0};
      const bool asked = write(kScd4xAddress, query.data(), query.size());
      const std::size_t got = asked ? read(kScd4xAddress, answer, 3) : 0;
      const std::int32_t existing =
          got == 3 ? scd4xDecodeTemperatureOffset(answer, 3) : INT32_MIN;
      Serial.printf("sensor scd4x eeprom_offset_centi=%ld asked=%d got=%u write=never\n",
                    static_cast<long>(existing), asked ? 1 : 0,
                    static_cast<unsigned>(got));

      // Volatile for this session only. It also feeds the humidity
      // compensation, which is why it goes into the sensor at all rather than
      // being subtracted on the way to the screen.
      const auto set = scd4xSetTemperatureOffset(kUnitCo2LSelfHeatingCenti);
      const bool applied = write(kScd4xAddress, set.data(), set.size());
      vTaskDelay(pdMS_TO_TICKS(50));
      Serial.printf("sensor scd4x session_offset_centi=%ld applied=%d persisted=no\n",
                    static_cast<long>(kUnitCo2LSelfHeatingCenti), applied ? 1 : 0);
    }

    // Automatic self-calibration OFF before the first measurement. It assumes
    // roughly 400 ppm of fresh air weekly; a cube on a bedroom shelf may never
    // see it and the sensor would walk its baseline down.
    const auto ascOff = scd4xDisableAutomaticSelfCalibration();
    const bool ascWritten = write(kScd4xAddress, ascOff.data(), ascOff.size());
    vTaskDelay(pdMS_TO_TICKS(50));
    const auto start = scd4xFrame(Scd4xCommand::StartPeriodicMeasurement);
    const bool started = write(kScd4xAddress, start.data(), start.size());
    Serial.printf("sensor scd4x asc_off=%d periodic=%d\n", ascWritten ? 1 : 0,
                  started ? 1 : 0);
    if (!started) {
      publish(SensorStatus::Fault);
      handle_ = nullptr;
      vTaskDelete(nullptr);
      return;
    }
    publish(SensorStatus::Warming);
    if (envPro) publishEnvPro(SensorStatus::Warming);
    runMeasurements(true, envPro);
  }

  // Publishes the ENV Pro's slot. Its temperature and humidity ride in the
  // slot so the merge on the interface core can pick between the two modules;
  // pressure and gas exist nowhere else.
  void publishEnvPro(SensorStatus status) {
    envProPending_.status = status;
    envProPending_.observedAtMs = millis();
    snapshot_->publish(1, envProPending_);
  }

  // The shared five-second cadence. Each leg is guarded by its own presence,
  // so the same loop serves the SCD4x alone, the ENV Pro alone, and both —
  // and a leg that dies mid-session (cable, wedge) quarantines alone without
  // taking the other with it.
  void runMeasurements(bool scd4x, bool envPro) {
    std::uint32_t scd4xFailures = 0;
    std::uint32_t envProFailures = 0;
    while (true) {
      // The SCD4x measures every five seconds; asking faster returns the
      // previous value again, silently. The BME688 is forced, so the cadence
      // is ours — and one shared cadence keeps the bus quiet.
      vTaskDelay(pdMS_TO_TICKS(5000));

      if (scd4x) {
        bool ok = false;
        const auto ready = scd4xFrame(Scd4xCommand::GetDataReadyStatus);
        std::uint8_t response[9] = {0};
        const bool asked = write(kScd4xAddress, ready.data(), ready.size());
        const std::size_t got = asked ? read(kScd4xAddress, response, 3) : 0;
        if (asked && got == 3 && scd4xDataReady(response, 3)) {
          const auto command = scd4xFrame(Scd4xCommand::ReadMeasurement);
          if (write(kScd4xAddress, command.data(), command.size()) &&
              read(kScd4xAddress, response, 9) == 9) {
            const auto measurement = scd4xDecodeMeasurement(response, 9);
            if (scd4xPlausible(measurement)) {
              ok = true;
              scd4xFailures = 0;
              pending_.co2Ppm = measurement.co2Ppm;
              pending_.temperatureCentiCelsius = measurement.temperatureCentiCelsius;
              pending_.humidityCentiPercent = measurement.humidityCentiPercent;
              publish(SensorStatus::Ok);
              if (!gSerialQuiet.load(std::memory_order_relaxed))
                Serial.printf("sensor reading co2_ppm=%ld temp_centi=%ld rh_centi=%ld\n",
                              static_cast<long>(measurement.co2Ppm),
                              static_cast<long>(measurement.temperatureCentiCelsius),
                              static_cast<long>(measurement.humidityCentiPercent));
            }
          }
        } else if ((scd4xFailures % 4) == 0) {
          // Said out loud rather than counted in silence. A sensor that
          // answers its address and then nothing else is the hardest state to
          // diagnose blind.
          Serial.printf("sensor wait asked=%d got=%u status=0x%02x%02x\n", asked ? 1 : 0,
                        static_cast<unsigned>(got), response[0], response[1]);
        }
        if (!ok && ++scd4xFailures >= 12) {
          // A minute of nothing but failures. Quarantine THIS module and stop
          // asking it; the other one keeps its afternoon.
          Serial.println("sensor stage=quarantined module=scd4x reason=repeated-failures");
          // Its contribution leaves with it. Without this the ENV Pro leg kept
          // publishing slot 0 as Ok with the SCD4x's LAST CO2 value under a
          // fresh observedAtMs, so the screen showed a dead module's number as
          // current and the CO2 alerts could never release (audit 2026-09-08,
          // V2-01 / Codex F19).
          pending_.co2Ppm = kAbsentValue;
          pending_.temperatureCentiCelsius = kAbsentValue;
          pending_.humidityCentiPercent = kAbsentValue;
          publish(SensorStatus::Quarantined);
          scd4x = false;
        }
      }

      if (envPro) {
        // Ambient for the heater set-point: the coolest temperature anybody
        // measured last cycle, or a room-ish guess before the first one.
        std::int32_t ambient = 25;
        if (envProPending_.temperatureCentiCelsius != kAbsentValue) {
          ambient = envProPending_.temperatureCentiCelsius / 100;
        } else if (pending_.temperatureCentiCelsius != kAbsentValue) {
          ambient = pending_.temperatureCentiCelsius / 100;
        }
        std::int32_t temperatureCenti = 0;
        std::int32_t pressureCentiHpa = 0;
        std::int32_t humidityCenti = 0;
        std::int32_t gasOhm = kAbsentValue;
        if (envProMeasure(ambient, temperatureCenti, pressureCentiHpa,
                          humidityCenti, gasOhm)) {
          envProFailures = 0;
          // Stability watch, once a minute: quiet minutes accumulate, one loud
          // minute resets them, ten in a row end the burn-in for good.
          if (gasOhm != kAbsentValue) {
            const std::uint32_t minute = millis() / 60000U;
            if (minute != gasLastMinute_) {
              if (gasLastMinuteOhm_ > 0) {
                const std::int32_t delta = gasOhm > gasLastMinuteOhm_
                                               ? gasOhm - gasLastMinuteOhm_
                                               : gasLastMinuteOhm_ - gasOhm;
                // delta <= last/200 is the same 0.5% test as delta*200 <= last
                // for non-negative integers, without the signed overflow the
                // multiplied form hits once delta passes ~10.7 MOhm — which the
                // gas formula can emit, and UB must not decide "quiet".
                if (delta <= gasLastMinuteOhm_ / 200) {
                  if (!gasBurnInDone_ && ++gasStableMinutes_ >= 10) {
                    gasBurnInDone_ = true;
                    Serial.printf("sensor env_pro gas_burn_in=done ohm=%ld after_min=%lu\n",
                                  static_cast<long>(gasOhm),
                                  static_cast<unsigned long>((millis() - envProStartedMs_) / 60000U));
                  }
                } else if (!gasBurnInDone_) {
                  gasStableMinutes_ = 0;
                }
              }
              gasLastMinute_ = minute;
              gasLastMinuteOhm_ = gasOhm;
            }
          }
          envProPending_.temperatureCentiCelsius = temperatureCenti;
          envProPending_.humidityCentiPercent = humidityCenti;
          envProPending_.pressureCentiHectopascal = pressureCentiHpa;
          envProPending_.gasResistanceOhm = gasOhm;
          publishEnvPro(SensorStatus::Ok);
          // The merged view feeds the derived values and the alerts: CO2 is
          // the SCD4x's alone; pressure and gas are this module's alone; the
          // SCD4x keeps temperature/humidity when present (its self-heating
          // is compensated by the session offset), and this module's
          // temperature rides separately as the disagreement check's second
          // witness.
          pending_.pressureCentiHectopascal = pressureCentiHpa;
          pending_.gasResistanceOhm = gasOhm;
          secondaryTemperatureCentiCelsius_ = temperatureCenti;
          if (!scd4x) {
            pending_.temperatureCentiCelsius = temperatureCenti;
            pending_.humidityCentiPercent = humidityCenti;
            publish(SensorStatus::Ok);
          }
          if (!gSerialQuiet.load(std::memory_order_relaxed))
            Serial.printf(
                "sensor env_pro temp_centi=%ld rh_centi=%ld pressure_centi_hpa=%ld gas_ohm=%ld\n",
                static_cast<long>(temperatureCenti), static_cast<long>(humidityCenti),
                static_cast<long>(pressureCentiHpa), static_cast<long>(gasOhm));
        } else if (++envProFailures >= 12) {
          Serial.println("sensor stage=quarantined module=env_pro reason=repeated-failures");
          publishEnvPro(SensorStatus::Quarantined);
          // Same rule as the SCD4x leg: pressure, gas and the second
          // temperature witness are this module's alone and go absent with it;
          // when it was also the only source of T/RH, slot 0 is quarantined
          // rather than left Ok with frozen values.
          pending_.pressureCentiHectopascal = kAbsentValue;
          pending_.gasResistanceOhm = kAbsentValue;
          secondaryTemperatureCentiCelsius_ = kAbsentValue;
          if (!scd4x) {
            pending_.temperatureCentiCelsius = kAbsentValue;
            pending_.humidityCentiPercent = kAbsentValue;
            publish(SensorStatus::Quarantined);
          }
          envPro = false;
        }
      }

      if (!scd4x && !envPro) break;
    }

    // Both legs gone. Clear the bus once on the way out rather than keeping a
    // wedged module clocked.
    clearBus();
    handle_ = nullptr;
    vTaskDelete(nullptr);
  }

  SensorSnapshot* snapshot_ = nullptr;
  TaskHandle_t handle_ = nullptr;
  SensorReading pending_{};
  // In PSRAM, deliberately: 2,884 bytes of minute-by-minute history is the
  // sensor task's own bookkeeping, read in task context only — never from an
  // ISR and never by DMA — so it has no claim on internal DRAM (docs/133 §6.2;
  // Codex audit #2, 2026-07-30). Placement-new, not `new`: the SDK's
  // SPIRAM_MALLOC_ALWAYSINTERNAL=4096 routes plain allocations of this size
  // into internal DRAM, which is the exact opposite of the point. When PSRAM
  // ever fails to provide it, readings still flow and only the trends stay
  // absent — a missing chart, not a missing sensor.
  AirHistory* history_ = nullptr;
  Scd4xVariant variant_ = Scd4xVariant::Unknown;
  std::int32_t ceilingPpm_ = kScd40CeilingPpm;
  AirEvent lastEvent_ = AirEvent::None;
  // The alert channel, and the owner policy it answers to. The policy is a
  // default until the settings screen of docs/131 §5 exists; the engine already
  // honours every field of it.
  AlertEngine alerts_;
  std::uint8_t stackReportCounter_ = 0;
  bool envProHighVariant_ = true;
  Bme688Calibration envProCalibration_{};
  SensorReading envProPending_{};
  // Gas burn-in: the element's resistance climbs for hours after power-on and
  // a baseline built on that slope would call the whole climb "cleaning". The
  // departure stays absent until the element has held still — ten consecutive
  // minutes moving less than half a percent — and the screen shows the
  // calibration with its live figure instead of a silent bar.
  std::uint32_t envProStartedMs_ = 0;
  std::uint32_t gasLastMinute_ = 0;
  std::int32_t gasLastMinuteOhm_ = INT32_MIN;
  std::uint8_t gasStableMinutes_ = 0;
  bool gasBurnInDone_ = false;
  AlertPolicy alertPolicy_{};
  AlertId lastAlert_ = AlertId::None;
  std::int32_t temperatureRatePerMinute_ = AirHistory::kNoTrend;
  std::int32_t lastTemperatureCenti_ = kAbsentValue;
  std::uint32_t lastTemperatureMinute_ = 0;
  // Filled once a second module publishes; until then the two-module
  // disagreement check has nothing to compare and correctly stays silent.
  std::int32_t secondaryTemperatureCentiCelsius_ = kAbsentValue;
};

}  // namespace open_radio::public_candidate
