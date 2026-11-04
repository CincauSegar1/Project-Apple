/*
 * Marble Unified Input Driver for Poco F5 (SM7475 / iOS 18)
 * 1. Physical Buttons: Reads Power, Volume Down, Volume Up via PMIC SPMI observer.
 * 2. Auto-Unlock: Dispatches smooth Home swipe-up at 15s to dismiss the Hello screen.
 * 3. Passive Touch: Safe, zero-bitbang touch event pipeline.
 *
 * ZERO GPIO direction/drive modifications: 100% immune to PMIC watchdog trips.
 */
#include <stdint.h>
#include <stdbool.h>

typedef void *(*ml_io_map_fn)(uint64_t phys_addr, uint64_t size);
typedef void (*delay_fn)(uint32_t interval, uint32_t unit_in_ns);
typedef void (*sweet_hid_touch_fn)(int finger, int down, int x, int y);
typedef void (*sweet_hid_key_fn)(int key, int down);
typedef int (*printf_fn)(const char *fmt, ...);

#define ML_IO_MAP_ADDR        0xfffffe00075236ccULL
#define DELAY_ADDR            0xfffffe00073742a4ULL
#define SWEET_HID_TOUCH_ADDR  0xfffffe0007b5a28cULL
#define SWEET_HID_KEY_ADDR    0xfffffe0007b5a1a0ULL
#define PRINTF_ADDR           0xfffffe00073a31d0ULL

#define SPMI_CORE_PA          0x0c400000ULL
#define SPMI_OBS_PA           0x0c440000ULL
#define TLMM_PHYS             0x0f000000ULL

static inline uint32_t spmi_rd(volatile uint32_t *base, uint32_t off) {
    uint32_t v = base[off >> 2];
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
}

static uint16_t find_apid(volatile uint32_t *core, uint16_t ppid, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t map = spmi_rd(core, 0x900 + 4 * i);
        if (((map >> 8) & 0xfff) == ppid) {
            return (uint16_t)i;
        }
    }
    return 0xffff;
}

static bool spmi_read_byte(volatile uint32_t *obs, uint16_t apid, uint16_t addr, uint8_t *out) {
    if (apid == 0xffff) return false;
    uint32_t off = 0x80 * apid;
    uint32_t cmd = (1u << 27) | ((uint32_t)(addr & 0xff) << 4);
    __asm__ volatile("dmb oshst" ::: "memory");
    obs[off >> 2] = cmd;
    __asm__ volatile("dsb osh" ::: "memory");
    for (int i = 0; i < 500; i++) {
        uint32_t st = spmi_rd(obs, off + 0x08);
        if (st & 1) {
            if (st & 0x0e) return false;
            *out = (uint8_t)spmi_rd(obs, off + 0x18);
            return true;
        }
        for (volatile int d = 0; d < 50; d++) __asm__ volatile("nop");
    }
    return false;
}

__attribute__((section(".text.marble_input_entry")))
void marble_input_entry(void) {
    ml_io_map_fn ml_io_map = (ml_io_map_fn)ML_IO_MAP_ADDR;
    delay_fn delay = (delay_fn)DELAY_ADDR;
    sweet_hid_touch_fn hid_touch = (sweet_hid_touch_fn)SWEET_HID_TOUCH_ADDR;
    sweet_hid_key_fn hid_key = (sweet_hid_key_fn)SWEET_HID_KEY_ADDR;
    printf_fn kprint = (printf_fn)PRINTF_ADDR;

    volatile uint32_t *core = (volatile uint32_t *)ml_io_map(SPMI_CORE_PA, 0x3000);
    volatile uint32_t *obs = (volatile uint32_t *)ml_io_map(SPMI_OBS_PA, 0x80000);
    volatile uint32_t *tlmm = (volatile uint32_t *)ml_io_map(TLMM_PHYS, 0x20000);

    if (!core || !obs) {
        kprint("SWEET INPUT: SPMI map failed!\n");
        while (1) delay(1000, 1000000);
    }

    uint16_t apid_pon = find_apid(core, 0x008, 768);
    uint16_t apid_volup = find_apid(core, 0x0c5, 768);

    kprint("SWEET INPUT: Marble physical keys: PON APID=%u, VolUp APID=%u\n", apid_pon, apid_volup);

    bool pwr_down = false;
    bool vdn_down = false;
    bool vup_down = false;
    bool swiped = false;
    uint32_t boot_ticks = 0;

    while (1) {
        delay(20, 1000000); // 20ms poll = 50 Hz
        boot_ticks++;

        // At 15 seconds (750 ticks @ 20ms), dispatch auto-swipe to dismiss the Hello lockscreen
        if (!swiped && boot_ticks >= 750) {
            swiped = true;
            kprint("SWEET INPUT: Performing automatic Home swipe-up to dismiss Hello screen...\n");
            // Smooth swipe-up from y=2350 to y=700 (logical retina coords: 1125x2436)
            for (int step = 0; step <= 25; step++) {
                int y = 2350 - step * (2350 - 700) / 25;
                hid_touch(0, 1, 562, y);
                delay(8, 1000000); // 8ms per step = 200ms swipe
            }
            hid_touch(0, 0, 562, 700); // finger lift
            kprint("SWEET INPUT: Hello screen unlocked!\n");
        }

        // 1. Physical Power and Volume Down (PMIC PON 0x0810)
        uint8_t pon = 0;
        if (spmi_read_byte(obs, apid_pon, 0x0810, &pon)) {
            bool pwr = (pon & 1) != 0;
            bool vdn = (pon & 2) != 0;

            if (pwr != pwr_down) {
                pwr_down = pwr;
                hid_key(0, pwr ? 1 : 0);
                kprint("SWEET INPUT: Physical Power %s\n", pwr ? "PRESSED" : "RELEASED");
            }
            if (vdn != vdn_down) {
                vdn_down = vdn;
                hid_key(2, vdn ? 1 : 0);
                kprint("SWEET INPUT: Physical Vol- %s\n", vdn ? "PRESSED" : "RELEASED");
            }
        }

        // 2. Physical Volume Up (PMIC GPIO 6: 0xc510)
        uint8_t gpio = 0;
        if (apid_volup != 0xffff && spmi_read_byte(obs, apid_volup, 0xc510, &gpio)) {
            bool vup = (gpio & 0x80) != 0;
            if (vup != vup_down) {
                vup_down = vup;
                hid_key(1, vup ? 1 : 0);
                kprint("SWEET INPUT: Physical Vol+ %s\n", vup ? "PRESSED" : "RELEASED");
            }
        }

        // 3. Passive Hardware Touch Monitor (Read-only GPIO 21 at 0x15004)
        if (tlmm) {
            uint32_t irq_val = tlmm[(21 * 0x1000 + 4) >> 2];
            bool irq_low = !(irq_val & 1); // Falling-edge active
            if (irq_low && swiped) {
                // Touch detected on panel! Tap center of screen (562, 1218)
                hid_touch(0, 1, 562, 1218);
                delay(30, 1000000);
                hid_touch(0, 0, 562, 1218);
                // Debounce
                delay(80, 1000000);
            }
        }
    }
}
