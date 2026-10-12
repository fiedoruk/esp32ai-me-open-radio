#pragma once

#include <cstddef>
#include <cstdint>

namespace open_radio {

// Everything about talking to a Bosch BME688 that does not need a bus.
//
// Same split as scd4x.hpp, for the same reason: the register map, the
// calibration parsing and the compensation arithmetic are pure functions,
// host-tested; the device keeps only "write these bytes, read those bytes".
//
// Why not Bosch's own driver: bme68x (BSD-3-Clause) is licence-compatible and
// was the REFERENCE for every formula below — but it wants a platform layer,
// an interface struct and a delay callback for what is, for this firmware, one
// forced measurement every five seconds. The licence was never the obstacle;
// the surface was. BSEC (the "air quality index" library) is a closed binary
// and stays excluded — see air_assessment.hpp for what this project computes
// instead, from arithmetic anyone can check.
//
// The part this speaks to is the BME688 inside M5Stack's ENV Pro unit, address
// 0x77, identity register 0xD0 answering 0x61. The BME680 answers the same
// identity and differs in one register bit and one gas formula; the variant
// register (0xF0) says which is on the wire, and both are handled, because the
// identity byte alone cannot tell them apart.

inline constexpr std::uint8_t kBme688Address = 0x77;
inline constexpr std::uint8_t kBme688ChipIdRegister = 0xD0;
inline constexpr std::uint8_t kBme688ChipId = 0x61;
// 0x00 = BME680 ("low" gas variant), 0x01 = BME688 ("high").
inline constexpr std::uint8_t kBme688VariantRegister = 0xF0;
inline constexpr std::uint8_t kBme688VariantHigh = 0x01;

inline constexpr std::uint8_t kBme688ResetRegister = 0xE0;
inline constexpr std::uint8_t kBme688ResetCommand = 0xB6;

// Calibration coefficients live in three blocks. The layout below mirrors
// Bosch's bme68x driver (the canonical parser); the index comments are the
// absolute register each byte comes from.
inline constexpr std::uint8_t kBme688Coeff1Register = 0x8A;  // 23 bytes
inline constexpr std::size_t kBme688Coeff1Length = 23;
inline constexpr std::uint8_t kBme688Coeff2Register = 0xE1;  // 14 bytes
inline constexpr std::size_t kBme688Coeff2Length = 14;
inline constexpr std::uint8_t kBme688Coeff3Register = 0x00;  // 5 bytes
inline constexpr std::size_t kBme688Coeff3Length = 5;

inline constexpr std::uint8_t kBme688CtrlHumRegister = 0x72;
inline constexpr std::uint8_t kBme688CtrlMeasRegister = 0x74;
inline constexpr std::uint8_t kBme688ConfigRegister = 0x75;
inline constexpr std::uint8_t kBme688CtrlGas1Register = 0x71;
inline constexpr std::uint8_t kBme688ResHeat0Register = 0x5A;
inline constexpr std::uint8_t kBme688GasWait0Register = 0x64;
// Field 0 data: status, pressure, temperature, humidity, and both variants'
// gas registers. One 17-byte read covers 0x1D..0x2D.
inline constexpr std::uint8_t kBme688DataRegister = 0x1D;
inline constexpr std::size_t kBme688DataLength = 17;

// Oversampling: T x2, P x16, H x1 — Bosch's "indoor navigation" profile, the
// highest pressure resolution, which is the reading the tendency arrow lives
// on. Forced mode bits are 0b01.
inline constexpr std::uint8_t kBme688CtrlHumValue = 0x01;                // osrs_h x1
inline constexpr std::uint8_t kBme688CtrlMeasForced =
    static_cast<std::uint8_t>((0x02 << 5) | (0x05 << 2) | 0x01);         // T x2, P x16, forced
// IIR filter coefficient 3: smooths pressure jitter without burying a real
// front; gas and humidity bypass the filter by hardware design.
inline constexpr std::uint8_t kBme688ConfigValue = 0x02 << 2;
// gas_wait_0: multiplier 4 (0b01 << 6) x 25 ms = 100 ms of heater dwell —
// Bosch's recommended starting point for a 300 C plate.
inline constexpr std::uint8_t kBme688GasWait100ms = 0x59;
inline constexpr std::int32_t kBme688HeaterTargetCelsius = 300;

struct Bme688Calibration {
  // Temperature.
  std::uint16_t t1;
  std::int16_t t2;
  std::int8_t t3;
  // Pressure.
  std::uint16_t p1;
  std::int16_t p2;
  std::int8_t p3;
  std::int16_t p4;
  std::int16_t p5;
  std::int8_t p6;
  std::int8_t p7;
  std::int16_t p8;
  std::int16_t p9;
  std::uint8_t p10;
  // Humidity.
  std::uint16_t h1;
  std::uint16_t h2;
  std::int8_t h3;
  std::int8_t h4;
  std::int8_t h5;
  std::uint8_t h6;
  std::int8_t h7;
  // Gas heater.
  std::int8_t gh1;
  std::int16_t gh2;
  std::int8_t gh3;
  std::uint8_t resHeatRange;
  std::int8_t resHeatVal;
  std::int8_t rangeSwErr;
};

inline Bme688Calibration bme688ParseCalibration(const std::uint8_t* coeff1,
                                                const std::uint8_t* coeff2,
                                                const std::uint8_t* coeff3) {
  Bme688Calibration c{};
  const auto s8 = [](std::uint8_t v) { return static_cast<std::int8_t>(v); };
  const auto u16 = [](std::uint8_t msb, std::uint8_t lsb) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(msb) << 8) | lsb);
  };
  const auto s16 = [&u16](std::uint8_t msb, std::uint8_t lsb) {
    return static_cast<std::int16_t>(u16(msb, lsb));
  };
  c.t2 = s16(coeff1[1], coeff1[0]);           // 0x8B / 0x8A
  c.t3 = s8(coeff1[2]);                       // 0x8C
  c.p1 = u16(coeff1[5], coeff1[4]);           // 0x8F / 0x8E
  c.p2 = s16(coeff1[7], coeff1[6]);           // 0x91 / 0x90
  c.p3 = s8(coeff1[8]);                       // 0x92
  c.p4 = s16(coeff1[11], coeff1[10]);         // 0x95 / 0x94
  c.p5 = s16(coeff1[13], coeff1[12]);         // 0x97 / 0x96
  c.p6 = s8(coeff1[15]);                      // 0x99
  c.p7 = s8(coeff1[14]);                      // 0x98
  c.p8 = s16(coeff1[19], coeff1[18]);         // 0x9D / 0x9C
  c.p9 = s16(coeff1[21], coeff1[20]);         // 0x9F / 0x9E
  c.p10 = coeff1[22];                         // 0xA0
  c.h1 = static_cast<std::uint16_t>((static_cast<std::uint16_t>(coeff2[2]) << 4) |
                                    (coeff2[1] & 0x0F));   // 0xE3 / 0xE2 low
  c.h2 = static_cast<std::uint16_t>((static_cast<std::uint16_t>(coeff2[0]) << 4) |
                                    (coeff2[1] >> 4));     // 0xE1 / 0xE2 high
  c.h3 = s8(coeff2[3]);                       // 0xE4
  c.h4 = s8(coeff2[4]);                       // 0xE5
  c.h5 = s8(coeff2[5]);                       // 0xE6
  c.h6 = coeff2[6];                           // 0xE7
  c.h7 = s8(coeff2[7]);                       // 0xE8
  c.t1 = u16(coeff2[9], coeff2[8]);           // 0xEA / 0xE9
  c.gh2 = s16(coeff2[11], coeff2[10]);        // 0xEC / 0xEB
  c.gh1 = s8(coeff2[12]);                     // 0xED
  c.gh3 = s8(coeff2[13]);                     // 0xEE
  c.resHeatVal = s8(coeff3[0]);               // 0x00
  c.resHeatRange = static_cast<std::uint8_t>((coeff3[2] >> 4) & 0x03);  // 0x02
  c.rangeSwErr = static_cast<std::int8_t>(
      static_cast<std::int8_t>(coeff3[4] & 0xF0) / 16);                 // 0x04
  return c;
}

struct Bme688Raw {
  bool newData = false;
  bool gasValid = false;
  bool heaterStable = false;
  std::uint32_t temperatureAdc = 0;
  std::uint32_t pressureAdc = 0;
  std::uint16_t humidityAdc = 0;
  std::uint16_t gasAdc = 0;
  std::uint8_t gasRange = 0;
};

// One field-0 block, both gas layouts: the BME680 reports gas at 0x2A/0x2B,
// the BME688 at 0x2C/0x2D. Offsets are relative to the 0x1D read.
inline Bme688Raw bme688ParseData(const std::uint8_t* d, bool highVariant) {
  Bme688Raw r;
  r.newData = (d[0] & 0x80U) != 0U;
  r.pressureAdc = (static_cast<std::uint32_t>(d[2]) << 12) |
                  (static_cast<std::uint32_t>(d[3]) << 4) |
                  (static_cast<std::uint32_t>(d[4]) >> 4);
  r.temperatureAdc = (static_cast<std::uint32_t>(d[5]) << 12) |
                     (static_cast<std::uint32_t>(d[6]) << 4) |
                     (static_cast<std::uint32_t>(d[7]) >> 4);
  r.humidityAdc = static_cast<std::uint16_t>((static_cast<std::uint16_t>(d[8]) << 8) | d[9]);
  const std::uint8_t gasMsb = highVariant ? d[15] : d[13];
  const std::uint8_t gasLsb = highVariant ? d[16] : d[14];
  r.gasAdc = static_cast<std::uint16_t>((static_cast<std::uint16_t>(gasMsb) << 2) |
                                        (gasLsb >> 6));
  r.gasRange = gasLsb & 0x0FU;
  r.gasValid = (gasLsb & 0x20U) != 0U;
  r.heaterStable = (gasLsb & 0x10U) != 0U;
  return r;
}

// Integer compensation, transcribed from Bosch's bme68x driver (BSD-3-Clause,
// the canonical arithmetic). Fixed point throughout — this runs on a core that
// is also mixing audio.

// Returns centi-degrees C and fills t_fine for the other channels.
inline std::int32_t bme688CompensateTemperature(const Bme688Calibration& c,
                                                std::uint32_t adc,
                                                std::int32_t& tFine) {
  const std::int64_t var1 =
      (static_cast<std::int64_t>(adc) >> 3) - (static_cast<std::int64_t>(c.t1) << 1);
  const std::int64_t var2 = (var1 * c.t2) >> 11;
  std::int64_t var3 = ((var1 >> 1) * (var1 >> 1)) >> 12;
  var3 = (var3 * (static_cast<std::int64_t>(c.t3) << 4)) >> 14;
  tFine = static_cast<std::int32_t>(var2 + var3);
  return static_cast<std::int32_t>(((static_cast<std::int64_t>(tFine) * 5) + 128) >> 8);
}

// The compensation formulas divide by quantities derived from p1 (pressure)
// and expect a non-zero t1/h1 scale. A block that arrived with the right
// length but zeros in these fields is not a calibration, it is a bus glitch,
// and the honest answer is "module absent", never a reading.
inline bool bme688CalibrationPlausible(const Bme688Calibration& c) {
  return c.p1 != 0 && c.t1 != 0;
}

// Returns Pascals.
//
// Bosch's integer reference (bme68x.c, calc_pressure) does this in int32 with
// left shifts of signed intermediates; for ADC values the bus can deliver that
// is undefined behaviour (UBSan: "left shift of negative value") and it runs
// BEFORE the plausibility check that would have discarded the sample. The same
// operations in 64-bit give bit-identical results wherever the reference did
// not overflow — the transcription vector below pins that — and defined ones
// everywhere else (audit 2026-09-08).
inline std::int32_t bme688CompensatePressure(const Bme688Calibration& c,
                                             std::uint32_t adc, std::int32_t tFine) {
  using i64 = std::int64_t;
  i64 var1 = (static_cast<i64>(tFine) >> 1) - 64000;
  i64 var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * c.p6) >> 2;
  var2 += (var1 * c.p5) * 2;
  var2 = (var2 >> 2) + (static_cast<i64>(c.p4) * 65536);
  var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) * (static_cast<i64>(c.p3) * 32)) >> 3) +
         ((static_cast<i64>(c.p2) * var1) >> 1);
  var1 >>= 18;
  var1 = ((32768 + var1) * c.p1) >> 15;
  // The reference divides by var1 without a guard; p1 = 0 from a corrupt
  // calibration read would trap the whole task. Zero pascals is outside
  // bme688Plausible(), so the sample is rejected upstream instead.
  if (var1 == 0) return 0;
  i64 pressure = 1048576 - static_cast<i64>(adc);
  pressure = (pressure - (var2 >> 12)) * 3125;
  if (pressure >= 0x40000000) {
    pressure = (pressure / var1) * 2;
  } else {
    pressure = (pressure * 2) / var1;
  }
  // The sensor spans 300-1100 hPa. Past 2e6 Pa this is a corrupt calibration,
  // and past about 8e7 the cube below overflows int64 (t1 = p1 = p10 = 1, rest
  // zero, passes the plausibility check and reaches 6.55e9). No reading.
  if (pressure <= 0 || pressure > 2000000) return 0;
  var1 = (static_cast<i64>(c.p9) * (((pressure >> 3) * (pressure >> 3)) >> 13)) >> 12;
  var2 = ((pressure >> 2) * c.p8) >> 13;
  // With p10 = 30 the int32 cube overflowed from about 1065 hPa — inside the
  // plausibility window and inside real weather.
  const i64 var3 = ((pressure >> 8) * (pressure >> 8) * (pressure >> 8) * c.p10) >> 17;
  pressure += (var1 + var2 + var3 + (static_cast<i64>(c.p7) * 128)) >> 4;
  if (pressure > INT32_MAX || pressure < INT32_MIN) return 0;
  return static_cast<std::int32_t>(pressure);
}

// Returns milli-percent relative humidity (0..100000). Same rule as the
// pressure path: Bosch's int32 intermediates overflow for ADC values the bus
// can deliver, so the arithmetic is 64-bit and the reference vector pins the
// in-range result.
inline std::int32_t bme688CompensateHumidity(const Bme688Calibration& c,
                                             std::uint16_t adc, std::int32_t tFine) {
  using i64 = std::int64_t;
  const i64 tempScaled = ((static_cast<i64>(tFine) * 5) + 128) >> 8;
  const i64 var1 =
      static_cast<i64>(adc) - (static_cast<i64>(c.h1) * 16) -
      (((tempScaled * c.h3) / 100) >> 1);
  const i64 var2 =
      (static_cast<i64>(c.h2) *
       (((tempScaled * c.h4) / 100) +
        (((tempScaled * ((tempScaled * c.h5) / 100)) >> 6) / 100) + (1 << 14))) >> 10;
  const i64 var3 = var1 * var2;
  i64 var4 = (static_cast<i64>(c.h6) * 128);
  var4 = (var4 + ((tempScaled * c.h7) / 100)) >> 4;
  const i64 var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
  const i64 var6 = (var4 * var5) >> 1;
  i64 humidity = (((var3 + var6) >> 10) * 1000) >> 12;
  if (humidity > 100000) humidity = 100000;
  if (humidity < 0) humidity = 0;
  return static_cast<std::int32_t>(humidity);
}

// Gas resistance in Ohms. The BME688 ("high" variant) formula; the BME680 uses
// a lookup-constant pair this deliberately does not carry — if a BME680 ever
// answers 0xF0 with 0x00, its gas channel reports absent rather than wrong.
inline std::int32_t bme688GasResistanceOhm(std::uint16_t gasAdc, std::uint8_t gasRange) {
  const std::uint32_t var1 = UINT32_C(262144) >> gasRange;
  std::int32_t var2 = static_cast<std::int32_t>(gasAdc) - 512;
  var2 *= 3;
  var2 = 4096 + var2;
  const std::uint32_t gas =
      (UINT32_C(10000) * var1) / static_cast<std::uint32_t>(var2);
  return static_cast<std::int32_t>(gas * 100);
}

// The heater set-point register value for a target plate temperature at a
// given ambient, from the calibration words.
inline std::uint8_t bme688HeaterResistance(const Bme688Calibration& c,
                                           std::int32_t targetCelsius,
                                           std::int32_t ambientCelsius) {
  if (targetCelsius > 400) targetCelsius = 400;
  const std::int32_t var1 = ((ambientCelsius * c.gh3) / 1000) * 256;
  const std::int32_t var2 =
      (c.gh1 + 784) *
      (((((c.gh2 + 154009) * targetCelsius * 5) / 100) + 3276800) / 10);
  const std::int32_t var3 = var1 + (var2 / 2);
  const std::int32_t var4 = var3 / (c.resHeatRange + 4);
  const std::int32_t var5 = (131 * c.resHeatVal) + 65536;
  const std::int32_t heatrResX100 = ((var4 / var5) - 250) * 34;
  return static_cast<std::uint8_t>((heatrResX100 + 50) / 100);
}

// The same plausibility discipline as the SCD4x: arithmetic turns any bits into
// a number, so the bounds say whether the WIRE is to be believed. Indoor cube:
// -10..60 C, 300..1100 hPa, 0..100 %.
inline bool bme688Plausible(std::int32_t temperatureCenti, std::int32_t pressurePa,
                            std::int32_t humidityMilli) {
  if (temperatureCenti < -1000 || temperatureCenti > 6000) return false;
  if (pressurePa < 30000 || pressurePa > 110000) return false;
  if (humidityMilli < 0 || humidityMilli > 100000) return false;
  return true;
}

}  // namespace open_radio
