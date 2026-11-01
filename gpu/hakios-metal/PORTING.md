# hakios-metal — marble / Adreno 725 (SM7475) port

Port of `hakios-metal` from the A635 (Adreno 7c+ Gen 3, a6xx gen4) to the
**Adreno 725** (Snapdragon 7+ Gen 2, POCO F5 "marble", a7xx gen1).

Upstream: hakios-metal targets chip `0x06030500`. This chip is `0x43050a00`.

## What this port adds

- `include/hakios-metal/a7xx.hpp` — the A725 chip facts (chip id, SP/fiber/wave
  geometry, GMEM, alignments) in the same style as `a6xx.hpp`. Values not yet
  confirmed against Mesa's `a7xx.xml` are marked `verify:`.
- `include/hakios-metal/chip.hpp` — a generation dispatch layer. It reads the
  kernel-reported chip id and selects the a6xx or a7xx table, exposing one
  `ChipInfo` for the emitters. `usable()` gates a7xx off until its register
  table is confirmed.
- `Gpu::generation()`, `Gpu::info()`, `Gpu::usable()` in `gpu.hpp`.
- `src/iokit_transport.cpp` — binds the kernel service by trying
  `SweetAdreno630` (Project Apple / SWEET) then `HakiosGPU` (upstream), so the
  same driver attaches to either kernel.
- `src/gpu.cpp` — `surfaceBo()` is dual-ABI: it first asks the SWEET
  `SweetAdreno630` selector `0x71` (`sweet_gpu_surface_bo`, 128 B, which also
  reports the GPU iova), then falls back to hakios's `0x7e` (`HakiosSurface`).
  The rest of the msm ioctl surface (GEM_NEW/INFO/CPU_PREP/SUBMIT/WAIT_FENCE/
  SUBMITQUEUE/GEM_CLOSE at 0x40-0x4b + 0x09) is already selector-compatible
  with `sweet_adreno_abi.h`.

## What is NOT done yet (the real port work)

1. **a7xx register table.** `a6xx.hpp` is 4489 lines of A6xx registers. The
   a7xx generation moves many SP_*/HLSQ_* offsets and adds BV/BR blocks. They
   must be re-tabulated from Mesa `src/freedreno/registers/adreno/a7xx.xml`
   (and `adreno_pm4.xml`, `adreno_common.xml` for the shared fields), the same
   way `tools/check-a6xx.py` checks a6xx.hpp today. Until then
   `a7xx::kA7xxRegistersConfirmed = false` and `usable()` refuses a7xx.
2. **Emitters.** `compute.cpp` and `render.cpp` write a6xx offsets. They need
   an a7xx path (freedreno `a7xx/` in turnip is the reference order).
3. **Kernel driver (the hard part).** This is a *userspace* Metal driver. It
   needs a kernel GPU service. The SWEET kernel already ships `SweetAdreno630`
   (`bsd/dev/sweet_a630.c` + `iokit/Kernel/arm/SweetAdreno.cpp`) — but it is
   written for the **Adreno 630 of the POCO F1 (beryllium)**, a different SoC
   and a different register/SMMU/GMU layout. To light the Adreno 725 it must be
   ported to the SM7475 GPU (marble): new MMIO bases, the A7xx GMU/HFI
   programming, the marble SMMU context, and the clock/DCVS table. Then
   `sweetgpu=8 sweetgpuuc=1` will publish it (as `AGXDeviceUserClient`, which
   Metal's sandbox allows). Do NOT enable `sweetgpu=8` on marble now: the A630
   register poke in the wrong hardware hangs the bus at boot.
4. **GPU firmware (per-device, not redistributable).** The Adreno 725 needs
   `a725_gmu.bin`, `a725_sqe.fw` and a signed zap shader pulled from the phone's
   own Android `/vendor/firmware` (`adb pull`), the same way the A630 flow needs
   `a630_zap.*`. Without the zap the CP never leaves secure mode and the GPU
   cannot run.

## Build / test

Same as upstream: `make test` runs the host tests against `fake_gpu`. The fake
GPU currently models a6xx; extend it (or add an a7xx mode) once the register
table lands. On-device use requires the kernel `HakiosGPU` service and
`bglrender=1`.

## Layout

```
include/hakios-metal/   gpu.hpp chip.hpp a6xx.hpp a7xx.hpp pm4.hpp ...
src/                    PM4/compute/render/texture emitters, iokit_transport
plugin/                 HakiosMetal.m — MTLDevice plugin
tools/                  probe, check-a6xx.py, sweep
tests/                  host tests + shader fixtures
```

Credit: original hakios-metal authors. Marble port for the Bergamota /
Project-Apple kernel (POCO F5).
