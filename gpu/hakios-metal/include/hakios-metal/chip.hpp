// Which Adreno this driver is running on, resolved once from the kernel's
// GET_PARAM chip id, and the per-generation facts the emitters need without
// knowing which family is underneath.
//
// The register tables stay per-generation (a6xx.hpp for the A635 of the SM7325
// and the register-compatible A662 of the SM7475); this header is the single
// place that picks the chip facts by id. The packet layer (pm4.hpp) and the
// buffer/submission model (gpu.hpp) are the same on every family.
//
// PORT (marble): the original driver was written for the A635 (a6xx gen4,
// chip 0x06030500). The POCO F5's GPU reports itself as the "Adreno 725" but
// its SM7475 device-tree chip id is 0x06060201 ("Adreno662v2"), which is the
// SAME a6xx gen4 register family — so this adds it as an a662 variant, not an
// a7xx port. a662 is recognised and its facts exposed; a real submission is
// gated (usable()) until the few A662-specific offsets are confirmed (see
// a662.hpp and PORTING.md).
#pragma once

#include <cstdint>

#include "hakios-metal/a6xx.hpp"
#include "hakios-metal/a662.hpp"

namespace hakios::metal {

enum class ChipGeneration : uint32_t {
    kUnknown = 0,
    kA6xx = 6,     // a6xx gen4: A635 (SM7325) chip 0x06030500, A662 (SM7475) chip 0x060602xx
};

// The generation a kernel-reported chip id belongs to. The speed bin / model
// variant fills the low byte, so match the family (high bits), not the whole word.
inline constexpr ChipGeneration generationOf(uint32_t chipId) {
    if ((chipId & 0xffff0000u) == (a6xx::kChipId & 0xffff0000u)) {
        return ChipGeneration::kA6xx;
    }
    if ((chipId & a662::kChipIdMask) == a662::kChipIdFamily) {
        return ChipGeneration::kA6xx;   // A662 is a6xx gen4 too
    }
    return ChipGeneration::kUnknown;
}

// A friendlier chip label for logging.
inline constexpr const char* chipName(uint32_t chipId) {
    if ((chipId & a662::kChipIdMask) == a662::kChipIdFamily) {
        return "a662 (Adreno 725 / Adreno662v2, SM7475)";
    }
    if ((chipId & 0xffff0000u) == (a6xx::kChipId & 0xffff0000u)) {
        return "a6xx (Adreno 7c+ Gen 3 / A635)";
    }
    return "unknown";
}

inline constexpr const char* generationName(ChipGeneration g) {
    switch (g) {
    case ChipGeneration::kA6xx: return "a6xx gen4";
    default: return "unknown";
    }
}

// Whether a command stream may actually be submitted to this chip. The A635 is
// the reference implementation and works; the A662 shares the family and is
// gated separately until its specific offsets are confirmed.
inline constexpr bool usable(uint32_t chipId) {
    if ((chipId & 0xffff0000u) == (a6xx::kChipId & 0xffff0000u)) {
        return true;   // the original A635 target
    }
    if ((chipId & a662::kChipIdMask) == a662::kChipIdFamily) {
        return a662::kA662RegistersConfirmed;
    }
    return false;
}

// The per-generation facts an emitter needs, one shape for both chips.
struct ChipInfo {
    ChipGeneration generation = ChipGeneration::kUnknown;
    uint32_t chipId = 0;              // the raw id the kernel reported
    const char* name = "unknown";
    uint32_t numSpCores = 0;
    uint32_t fibersPerSp = 0;
    uint32_t csSharedMemSize = 0;
    uint32_t threadsizeBase = 0;
    uint32_t maxWaves = 0;
    uint32_t waveGranularity = 0;
    uint32_t instrCacheSize = 0;
    uint32_t gmemSize = 0;
    bool hasLpac = false;
    bool doubleThreadsize = false;
};

inline constexpr ChipInfo infoFor(uint32_t chipId) {
    if ((chipId & a662::kChipIdMask) == a662::kChipIdFamily) {
        return ChipInfo{ChipGeneration::kA6xx, chipId, "a662/Adreno725",
                        a662::kNumSpCores, a662::kFibersPerSp, a662::kCsSharedMemSize,
                        a662::kThreadsizeBase, a662::kMaxWaves, a662::kWaveGranularity,
                        a662::kInstrCacheSize, a662::kGmemSize,
                        a662::kHasLpac, a662::kSupportsDoubleThreadsize};
    }
    if ((chipId & 0xffff0000u) == (a6xx::kChipId & 0xffff0000u)) {
        return ChipInfo{ChipGeneration::kA6xx, chipId, "a635",
                        a6xx::kNumSpCores, a6xx::kFibersPerSp, a6xx::kCsSharedMemSize,
                        a6xx::kThreadsizeBase, a6xx::kMaxWaves, a6xx::kWaveGranularity,
                        a6xx::kInstrCacheSize, /*a6xx gmem read via kParamGmemSize*/ 512 * 1024,
                        a6xx::kHasLpac, a6xx::kSupportsDoubleThreadsize};
    }
    return ChipInfo{};
}

}  // namespace hakios::metal
