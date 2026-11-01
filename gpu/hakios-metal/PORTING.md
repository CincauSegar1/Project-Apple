# hakios-metal — marble / Adreno 662v2 "Adreno 725" (SM7475) port

Port of `hakios-metal` to the **POCO F5 "marble"** GPU. The chip is marketed as
the **Adreno 725**, but the SM7475 device tree reports it as:

```
qcom,chipid    = 0x06060201
qcom,gpu-model = "Adreno662v2" / "Adreno644v1"   (vk-device-id 0x060602xx)
```

i.e. the **a6xx gen4 register family** — the same family the driver already
targets for the A635 (0x06030500). That makes this port much closer than a
from-scratch a7xx port: the `a6xx.hpp` register table mostly carries over and
only the chip facts + the SM7475 memory map differ.

## What this port adds

- `include/hakios-metal/a662.hpp` — the A662v2 chip facts: chip id
  `0x06060201`, GPU_ID 725, and the **real SM7475 MMIO map** (GPU core
  `0x3d00000`, gpucc `0x3d90000`, qsmmu `0x3da0000`, rscc `0x3de0000`,
  cx_dbgc `0x3d61000`, qdss `0x10900000`, IRQs SPI `0x12c`/`0x11e`) taken from
  `marble.dts`. The A630/beryllium addresses baked into the kernel's
  SweetAdreno bring-up are different — this header is the substitution table.
- `include/hakios-metal/chip.hpp` — chip-id dispatch (`ChipGeneration`,
  `chipName`, `chipInfo`) so the driver picks a635 vs a662 facts by the
  kernel-reported chip id.
- `src/iokit_transport.cpp` — binds `SweetAdreno630` (Project Apple) then
  `HakiosGPU` (upstream).
- `src/gpu.cpp` — dual-ABI `surfaceBo()` (SWEET `0x71` + hakios `0x7e`).

## GPU firmware (from the marble vendor ROM)

Available and redistributable-into-the-image:

| File | Source | Use |
|---|---|---|
| `a662_gmu.bin` | `/vendor/firmware/a662_gmu.bin` | GMU (power mgmt) firmware |
| `a662_zap.mdt` + `.b00/.b01/.b02` | `/vendor/firmware/` | **signed zap shader** — required for the CP to leave secure mode |
| `a660_sqe.fw` | `/vendor/firmware/a660_sqe.fw` | SQE (command engine) firmware |

These go in the iOS root at `/usr/local/share/firmware/` (or embedded in the
ADT `/sweet-gpu` node via `tools/adt-gpufw.py` / `tools/mkadt.py`), the same
paths `sweet_a630.c` already searches.

## What is NOT done yet (the real kernel port)

The **userspace** driver is wired end-to-end. The remaining work is the
kernel-side `SweetAdreno` bring-up for SM7475 — `bsd/dev/sweet_a630.c` is
written for the POCO F1's Adreno 630 (beryllium): different MMIO bases, the
qsmmu-v500 (not arm-smmu), the A662 GMU/HFI programming, and the marble
clock/DCVS table (`qcom,gpu-pwrlevels-*` in the dts). Until that exists,
`sweetgpu=8` on marble must NOT be set — it would poke A630 addresses into
SM7475 hardware and hang the bus at boot.

To light the GPU:

1. Port the register/base addresses (a662.hpp has them) + the GMU/zap/SQE
   load sequence to the SM7475.
2. Boot with `sweetgpu=8 sweetgpuuc=1` → the kernel publishes
   `AGXDeviceUserClient` (the class Metal's sandbox allows).
3. This driver then opens `SweetAdreno630`, sees chip id `0x06060201`,
   reports `a662/Adreno725` through `Gpu::info()`, and submits PM4.

`a662::kA662RegistersConfirmed` in `a662.hpp` gates a662 submissions; flip it
once the A662-specific offsets are verified against a downstream kgsl header.

## Build / test

`make test` runs the host tests against `fake_gpu` (currently models a6xx —
the right family for A662). On-device use needs the kernel service and
`bglrender=1`.

Credit: original hakios-metal authors (github.com/oosexgang/hakios-metal).
Marble port for the Bergamota / Project-Apple kernel (POCO F5).
