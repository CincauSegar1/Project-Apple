// Which Adreno this driver is running on, resolved once from the kernel's
// GET_PARAM chip id, and the per-generation facts the emitters need without
// knowing which family is underneath.
//
// The register tables stay per-generation (a6xx.hpp for the A635 of the SM7325,
// a7xx.hpp for the Adreno 725 of the SM7475 / POCO F5 "marble"); this header
// is the single place that picks between them. The packet layer (pm4.hpp) and
// the buffer/submission model (gpu.hpp) are the same on every family and are
// not abstracted here.
//
// PORT (marble): the original driver was written for the A635 (a6xx gen4,
// chip 0x06030500). This adds the A725 (a7xx gen1, chip 0x43050a00) as a second
// target. a7xx support is *scaffolded*: the chip is recognised, its facts are
// exposed, and emitters can query generation(), but a real submission is
// refused (usable() == false) until the a7xx register table is tabulated from
// Mesa's a7xx.xml — see a7xx.hpp and PORTING.md.
#pragma once

#include <cstdint>

#include "hakios-metal/a6xx.hpp"
#include "hakios-metal/a7xx.hpp"

namespace hakios::metal {

enum class ChipGeneration : uint32_t {
    kUnknown = 0,
    kA6xx = 6,     // a6xx gen4: A635 (SM7325), chip 0x06030500
    kA7xx = 7,     // a7xx gen1: A725 (SM7475 / POCO F5 marble), chip 0x43050a00
};

// The generation a kernel-reported chip id belongs to. The speed bin fills the
// low bits, so match the family (high 16 bits), not the whole word.
inline constexpr ChipGeneration generationOf(uint32_t chipId) {
    if ((chipId & 0xffff0000u) == (a6xx::kChipId & 0xffff0000u)) {
        return ChipGeneration::kA6xx;
    }
    if ((chipId & a7xx::kChipIdMask) == a7xx::kChipIdFamily) {
        return ChipGeneration::kA7xx;
    }
    return ChipGeneration::kUnknown;
}

inline constexpr const char* generationName(ChipGeneration g) {
    switch (g) {
    case ChipGeneration::kA6xx: return "a6xx (Adreno 7c+ Gen 3 / A635)";
    case ChipGeneration::kA7xx: return "a7xx (Adreno 725 / SM7475)";
    default: return "unknown";
    }
}

// Whether a command stream may actually be submitted to this chip. a6xx is the
// reference implementation and works; a7xx is recognised but gated off until
// its register table is confirmed (a7xx::kA7xxRegistersConfirmed).
inline constexpr bool usable(ChipGeneration g) {
    if (g == ChipGeneration::kA6xx) {
        return true;
    }
    if (g == ChipGeneration::kA7xx) {
        return a7xx::kA7xxRegistersConfirmed;
    }
    return false;
}

// The per-generation facts an emitter needs, one shape for both families.
struct ChipInfo {
    ChipGeneration generation = ChipGeneration::kUnknown;
    uint32_t chipId = 0;              // the raw id the kernel reported
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
    ChipGeneration g = generationOf(chipId);
    if (g == ChipGeneration::kA6xx) {
        return ChipInfo{g, chipId,
                        a6xx::kNumSpCores, a6xx::kFibersPerSp, a6xx::kCsSharedMemSize,
                        a6xx::kThreadsizeBase, a6xx::kMaxWaves, a6xx::kWaveGranularity,
                        a6xx::kInstrCacheSize, /*a6xx gmem is read via kParamGmemSize*/ 512 * 1024,
                        a6xx::kHasLpac, a6xx::kSupportsDoubleThreadsize};
    }
    if (g == ChipGeneration::kA7xx) {
        return ChipInfo{g, chipId,
                        a7xx::kNumSpCores, a7xx::kFibersPerSp, a7xx::kCsSharedMemSize,
                        a7xx::kThreadsizeBase, a7xx::kMaxWaves, a7xx::kWaveGranularity,
                        a7xx::kInstrCacheSize, a7xx::kGmemSize,
                        a7xx::kHasLpac, a7xx::kSupportsDoubleThreadsize};
    }
    return ChipInfo{};
}

}  // namespace hakios::metal
