// The Adreno A7xx chip facts, for the Adreno 725 of the SM7475 (Snapdragon 7+ Gen 2,
// POCO F5 "marble"): Mesa's "a7xx gen1" (A730/A740 family and the cut-down A725),
// chip id 0x43050a00.
//
// PORT NOTES (marble / Adreno 725). The register *numbers* a draw or dispatch
// writes are taken from the A6xx table (a6xx.hpp) wherever freedreno keeps the
// same offsets on gen1 of a7xx; the fields that differ are marked `a7xx:` and
// `verify:` below. The real register XML for this generation is
// src/freedreno/registers/adreno/a7xx.xml in Mesa 25.x; every value here that is
// not yet confirmed against that file carries `verify:` and MUST be checked
// before a real submission is trusted. The packet layer (pm4.hpp: type-4
// register writes, type-7 commands, the parity scheme) is identical on a6xx and
// a7xx and is reused unchanged.
//
// What changes from the A635 (a6xx gen4) to the A725 (a7xx gen1) that the
// driver actually has to know:
//   * a7xx moves several SP_* and HLSQ_* registers and adds the new "BV"/BR
//     blocks; the sysmem sysmem render order changes (see render.cpp a7xx path).
//   * GMEM is larger and the CCU/bank layout differs (kGmemSize, highest bank).
//   * thread/wave geometry and the instruction-cache preload cap differ.
//   * LPAC (the low-power async compute pipe) register block is present on gen4
//     a6xx but re-mapped on a7xx.
//   * the CP_SMMU / TTBR0 programming moved into the kernel's job entirely here:
//     the user client only hands buffers + an IOVA (gpu.hpp), so only the
//     *descriptor-visible* changes matter in this file.
#pragma once

#include <cstdint>

namespace hakios::metal::a7xx {

// --- this chip -----------------------------------------------------------------------------------------------------
// From Mesa's src/freedreno/common/freedreno_devices.py: the SM7475 GPU is the
// "Adreno 725". freedreno has no dedicated a725 entry; the a730/a740 (a7xx_base
// + a7xx_gen1) table is the closest match. chip_id as read back from
// CP_GET_PARAM / the qcom,chipid DT property.

inline constexpr uint32_t kChipId = 0x43050a00;            // a7xx: Adreno 725 (SM7475)  verify: read on device
inline constexpr uint32_t kChipIdMask = 0xffff0000;        // match the family field only (speed bins fill the low word)
inline constexpr uint32_t kChipIdFamily = 0x43050000;      // A725 family pattern (0x4305xxxx)

// freedreno a7xx_base + a7xx_gen1 facts. `verify:` = not yet confirmed for the
// cut-down A725 against real hardware or a7xx.xml.
inline constexpr bool kHasLpac = false;                    // a7xx: gen1 LPAC remapped/absent on A725  verify:
inline constexpr bool kSupportsDoubleThreadsize = true;    // a7xx_base
inline constexpr uint32_t kInstrCacheSize = 127;           // preload cap (units); same hang workaround as a6xx gen4
inline constexpr uint32_t kFibersPerSp = 128 * 2 * 16;     // a7xx: A725 fiber count  verify:
inline constexpr uint32_t kNumSpCores = 2;                 // a7xx: A725 has 2 SP/CP clusters  verify:
inline constexpr uint32_t kCsSharedMemSize = 32 * 1024;    // bytes of SP local store  verify:
inline constexpr uint32_t kThreadsizeBase = 64;            // a7xx_base default
inline constexpr uint32_t kMaxWaves = 16;                  // a7xx_base default max_waves
inline constexpr uint32_t kWaveGranularity = 4;            // a7xx: gen1 waves launch in fours  verify:
inline constexpr uint32_t kRegidNone = 0xfc;               // ir3 INVALID_REG (regid(63,0)) - same ISA family
inline constexpr uint32_t kGmemSize = 1024 * 1024;         // a7xx: A725 GMEM ~1 MiB  verify: kParamGmemSize on device
inline constexpr uint32_t kHighestBankBit = 13;            // a7xx: sysmem bank swizzle bit  verify: kParamHighestBankBit

// 64-bit address-register alignments (a7xx.xml reg64 align=...). Mostly carried
// over from a6xx; the a7xx.xml file is the authority. verify:
inline constexpr uint32_t kSpCsObjStartAlign = 32;         // verify:
inline constexpr uint32_t kSpCsPvtMemAddrAlign = 32;       // verify:
inline constexpr uint32_t kSpCsIboAlign = 16;              // verify:
inline constexpr uint32_t kSpCsTexConstAlign = 64;         // verify:
inline constexpr uint32_t kSpCsTexSampAlign = 16;          // verify:
inline constexpr uint32_t kDelayLoopCycles = 16;           // CP_WAIT_REG_MEM_5 delay (emitter choice)

// --- a7xx register-offset deltas -------------------------------------------------------------------------------------
// The A6xx register *values* a6xx.hpp carries (enum members, field masks) are
// reused by name where a7xx keeps the field; the *offsets* that moved on a7xx
// are what a real port must re-tabulate. Below are the entry points where the
// emitter (compute.cpp / render.cpp) needs a7xx-specific numbers. Each is the
// a6xx offset left UNCHANGED until checked against a7xx.xml — flip `confirmed`
// once verified.
//
// NOTE: shipping a wrong offset to the CP hangs the GPU. These are placeholders
// for the port, not working values. The dispatch/render code must be compiled
// with HAKIOS_METAL_CHIP_A7XX and the a7xx register table before any real
// submission; until then the driver refuses a7xx chip ids (see chip.hpp).

inline constexpr bool kA7xxRegistersConfirmed = false;     // flip to true once a7xx.xml offsets are tabulated

}  // namespace hakios::metal::a7xx
