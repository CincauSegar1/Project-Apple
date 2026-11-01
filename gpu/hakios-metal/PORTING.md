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

## What is NOT done yet (the real port work)

1. **a7xx register table.** `a6xx.hpp` is 4489 lines of A6xx registers. The
   a7xx generation moves many SP_*/HLSQ_* offsets and adds BV/BR blocks. They
   must be re-tabulated from Mesa `src/freedreno/registers/adreno/a7xx.xml`
   (and `adreno_pm4.xml`, `adreno_common.xml` for the shared fields), the same
   way `tools/check-a6xx.py` checks a6xx.hpp today. Until then
   `a7xx::kA7xxRegistersConfirmed = false` and `usable()` refuses a7xx.
2. **Emitters.** `compute.cpp` and `render.cpp` write a6xx offsets. They need
   an a7xx path (freedreno `a7xx/` in turnip is the reference order).
3. **Kernel driver.** This is a *userspace* Metal driver. It needs the kernel's
   `HakiosGPU` IOService (`IOServiceMatching("HakiosGPU")`) which maps the GPU
   MMIO and feeds a ringbuffer. The SWEET kernel has no such service — a
   `SweetAdreno` driver must be written for the SM7475 GPU (irq, SMMU/TTBR0,
   CP ringbuffer, GPMU/AOP power-up). Without it `openKernelGpu` returns
   nullptr and the plugin never attaches.
4. **GPU power-up.** SM7475 Adreno 725 needs the GMU/AOP firmware handshake +
   clock/voltage rail bring-up before the CP responds.

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
