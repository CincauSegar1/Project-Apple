/*
 * Marble Native Hardware Input Driver for Poco F5 (SM7475 / iOS 18)
 *
 * 1. Native Touchscreen: Goodix GT9916R (Berlin-D) on Cape QUPv3 SE4 SPI
 *    - GPIO 16: MOSI
 *    - GPIO 17: MISO
 *    - GPIO 18: CLK
 *    - GPIO 19: CS (Active Low)
 *    - GPIO 20: RESET (Pulsed on startup, then held HIGH)
 *    - GPIO 21: IRQ / INT (Falling edge / Low on contact)
 *
 * 2. Native Physical Buttons: PMIC SPMI Observer (0x0c440000)
 *    - Power Button: PMIC PON (PPID 0x008), reg 0x0810, bit 0
 *    - Volume Down:  PMIC PON (PPID 0x008), reg 0x0810, bit 1
 *    - Volume Up:    PMIC GPIO6 (PPID 0x0c5), reg 0xc510, bit 7
 *
 * Compiled with -mgeneral-regs-only to prevent FP/NEON register exceptions in XNU kernel threads.
 * NO virtual gestures, NO auto-swipe. 100% pure hardware input.
 */

#include <stdint.h>
#include <stdbool.h>

typedef void *(*ml_io_map_fn)(uint64_t phys_addr, uint64_t size);
typedef void (*delay_fn)(uint32_t interval, uint32_t unit_in_ns);
typedef void (*sweet_hid_touch_fn)(int finger, int down, int x, int y);
typedef void (*sweet_hid_key_fn)(int key, int down);

#define ML_IO_MAP_ADDR        0xfffffe00075236ccULL
#define DELAY_ADDR            0xfffffe00073742a4ULL
#define SWEET_HID_TOUCH_ADDR  0xfffffe0007b5a28cULL
#define SWEET_HID_KEY_ADDR    0xfffffe0007b5a1a0ULL
#define PRINTF_ADDR           0xfffffe00073a31d0ULL

#define TLMM_PHYS             0x0f100000ULL
#define TLMM_SIZE             0x20000ULL
#define SPMI_CORE_PA          0x0c400000ULL
#define SPMI_OBS_PA           0x0c440000ULL

#define PIN_MOSI_DEFAULT      16
#define PIN_MISO_DEFAULT      17
#define PIN_CLK               18
#define PIN_CS                19
#define PIN_RST               20
#define PIN_INT               21

#define GOODIX_STATUS_TOUCH   0x80

/*
 * XNU uses Darwin ARM64 ABI where variadic arguments are read from the caller's stack frame.
 * This naked wrapper stores x1-x7 to [sp] before jumping to XNU _printf so format strings work.
 */
__attribute__((naked))
static void xnu_kprint(const char *fmt, ...) {
    __asm__ volatile(
        "sub sp, sp, #64\n"
        "stp x1, x2, [sp]\n"
        "stp x3, x4, [sp, #16]\n"
        "stp x5, x6, [sp, #32]\n"
        "stp x7, x30, [sp, #48]\n"
        "mov x8, #0x31d0\n"
        "movk x8, #0x73a, lsl #16\n"
        "movk x8, #0xfe00, lsl #32\n"
        "movk x8, #0xffff, lsl #48\n"
        "blr x8\n"
        "ldr x30, [sp, #56]\n"
        "add sp, sp, #64\n"
        "ret\n"
    );
}

struct marble_touch_ctx {
    volatile uint32_t *tlmm;
    int pin_miso;
    int pin_mosi;
    int dummy_bytes;
};

static inline void wr(volatile uint32_t *tlmm, uint32_t reg, uint32_t val) {
    tlmm[reg >> 2] = val;
    __asm__ volatile("dmb oshst" ::: "memory");
}

static inline uint32_t rd(volatile uint32_t *tlmm, uint32_t reg) {
    uint32_t v = tlmm[reg >> 2];
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
}

#define GPIO_CFG(n) ((n) * 0x1000)
#define GPIO_IO(n)  ((n) * 0x1000 + 4)

static void gpio_cfg(volatile uint32_t *tlmm, int pin, int func, int pull, int drive, bool out) {
    uint32_t off = GPIO_CFG(pin);
    uint32_t cfg = rd(tlmm, off);
    cfg &= ~0x3ffu;
    cfg |= (pull & 3) | ((func & 0xf) << 2) | (((drive / 2) - 1) << 6) | (out ? (1 << 9) : 0);
    wr(tlmm, off, cfg);
}

static void gpio_set(volatile uint32_t *tlmm, int pin, bool val) {
    wr(tlmm, GPIO_IO(pin), val ? 2 : 0);
}

static bool gpio_get(volatile uint32_t *tlmm, int pin) {
    return (rd(tlmm, GPIO_IO(pin)) & 1) != 0;
}

static void spi_delay(void) {
    for (volatile int i = 0; i < 800; i++) {
        __asm__ volatile("nop");
    }
}

static void spi_transfer_byte(struct marble_touch_ctx *ctx, uint8_t out, uint8_t *in) {
    uint8_t rx = 0;
    for (int b = 7; b >= 0; b--) {
        gpio_set(ctx->tlmm, ctx->pin_mosi, (out >> b) & 1);
        spi_delay();
        gpio_set(ctx->tlmm, PIN_CLK, 1);
        spi_delay();
        rx = (rx << 1) | (gpio_get(ctx->tlmm, ctx->pin_miso) ? 1 : 0);
        gpio_set(ctx->tlmm, PIN_CLK, 0);
        spi_delay();
    }
    if (in) *in = rx;
}

static bool goodix_read(struct marble_touch_ctx *ctx, uint32_t address, uint8_t *data, uint32_t length) {
    uint8_t tx[12];
    tx[0] = 0xf1;
    tx[1] = (uint8_t)(address >> 24);
    tx[2] = (uint8_t)(address >> 16);
    tx[3] = (uint8_t)(address >> 8);
    tx[4] = (uint8_t)address;
    int dummy = (ctx->dummy_bytes > 0) ? ctx->dummy_bytes : 3;
    for (int i = 0; i < dummy; i++) {
        tx[5 + i] = 0xff;
    }
    int hdr_len = 5 + dummy;

    gpio_set(ctx->tlmm, PIN_CS, 0);
    for (volatile int d = 0; d < 2000; d++) __asm__ volatile("nop");
    for (int i = 0; i < hdr_len; i++) {
        spi_transfer_byte(ctx, tx[i], 0);
    }
    for (uint32_t i = 0; i < length; i++) {
        spi_transfer_byte(ctx, 0x00, &data[i]);
    }
    for (volatile int d = 0; d < 2000; d++) __asm__ volatile("nop");
    gpio_set(ctx->tlmm, PIN_CS, 1);
    for (volatile int d = 0; d < 2000; d++) __asm__ volatile("nop");
    return true;
}

static bool goodix_write(struct marble_touch_ctx *ctx, uint32_t address, const uint8_t *data, uint32_t length) {
    uint8_t tx[5];
    tx[0] = 0xf0;
    tx[1] = (uint8_t)(address >> 24);
    tx[2] = (uint8_t)(address >> 16);
    tx[3] = (uint8_t)(address >> 8);
    tx[4] = (uint8_t)address;

    gpio_set(ctx->tlmm, PIN_CS, 0);
    for (volatile int d = 0; d < 2000; d++) __asm__ volatile("nop");
    for (int i = 0; i < 5; i++) {
        spi_transfer_byte(ctx, tx[i], 0);
    }
    for (uint32_t i = 0; i < length; i++) {
        spi_transfer_byte(ctx, data[i], 0);
    }
    for (volatile int d = 0; d < 2000; d++) __asm__ volatile("nop");
    gpio_set(ctx->tlmm, PIN_CS, 1);
    for (volatile int d = 0; d < 2000; d++) __asm__ volatile("nop");
    return true;
}

static uint16_t find_apid(volatile uint32_t *core, uint16_t ppid, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t map = rd(core, 0x2000 + 4 * i);
        if (((map >> 8) & 0xfff) == ppid) {
            return (uint16_t)i;
        }
    }
    return 0xffff;
}

static bool spmi_read_byte(volatile uint32_t *obs, uint16_t apid, uint16_t addr, uint8_t *out) {
    if (apid == 0xffff) return false;
    uint32_t off = 0x20 * apid;
    uint32_t cmd = (1u << 27) | ((uint32_t)(addr & 0xff) << 4);
    __asm__ volatile("dmb oshst" ::: "memory");
    obs[off >> 2] = cmd;
    __asm__ volatile("dsb osh" ::: "memory");
    for (int i = 0; i < 400; i++) {
        uint32_t st = rd(obs, off + 0x08);
        if (st & 1) {
            if (st & 0x0e) return false;
            *out = (uint8_t)rd(obs, off + 0x18);
            return true;
        }
        for (volatile int d = 0; d < 40; d++) __asm__ volatile("nop");
    }
    return false;
}

static inline uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct contact_state {
    bool down;
    uint32_t x;
    uint32_t y;
};

__attribute__((section(".text.marble_touch_entry")))
void marble_touch_entry(void) {
    ml_io_map_fn ml_io_map = (ml_io_map_fn)ML_IO_MAP_ADDR;
    delay_fn delay = (delay_fn)DELAY_ADDR;
    sweet_hid_touch_fn hid_touch = (sweet_hid_touch_fn)SWEET_HID_TOUCH_ADDR;
    sweet_hid_key_fn hid_key = (sweet_hid_key_fn)SWEET_HID_KEY_ADDR;

    struct marble_touch_ctx ctx;
    ctx.pin_mosi = PIN_MOSI_DEFAULT;
    ctx.pin_miso = PIN_MISO_DEFAULT;
    ctx.dummy_bytes = 4;

    // Wait 2s for early boot and SpringBoard/backboardd stabilization
    xnu_kprint("SWEET TOUCH: waiting 2s for early boot stabilization...\n");
    for (int s = 0; s < 2; s++) {
        delay(1000, 1000000);
    }

    // Map SPMI for Physical Power & Volume Buttons
    volatile uint32_t *spmi_core = (volatile uint32_t *)ml_io_map(SPMI_CORE_PA, 0x3000);
    volatile uint32_t *spmi_obs = (volatile uint32_t *)ml_io_map(SPMI_OBS_PA, 0x80000);
    uint16_t apid_pon = 0xffff;
    uint16_t apid_volup = 0xffff;
    if (spmi_core && spmi_obs) {
        apid_pon = find_apid(spmi_core, 0x008, 768);
        apid_volup = find_apid(spmi_core, 0x0c5, 768);
        xnu_kprint("SWEET KEYS: Physical buttons ready (PON APID=%u, VolUp APID=%u)\n", (uint32_t)apid_pon, (uint32_t)apid_volup);
    }

    // Map Cape TLMM for Goodix GT9916R SPI Touchscreen
    xnu_kprint("SWEET TOUCH: initializing Marble Goodix GT9916R on Cape SPI (TLMM 0x%08x)...\n", (uint32_t)TLMM_PHYS);
    ctx.tlmm = (volatile uint32_t *)ml_io_map(TLMM_PHYS, TLMM_SIZE);
    if (!ctx.tlmm) {
        xnu_kprint("SWEET TOUCH: failed to map Cape TLMM!\n");
        while (1) delay(1000, 1000000);
    }

    // Hold GPIO 20 (Reset) HIGH to preserve firmware loaded by ABL
    gpio_cfg(ctx.tlmm, PIN_RST, 0, 3, 8, true);
    gpio_set(ctx.tlmm, PIN_RST, 1);
    delay(10, 1000000);

    // Configure SPI pins: CLK, CS, INT
    xnu_kprint("SWEET TOUCH: ABL TLMM: 16=0x%x 17=0x%x 18=0x%x 19=0x%x 20=0x%x 21=0x%x\n",
               rd(ctx.tlmm, GPIO_CFG(16)), rd(ctx.tlmm, GPIO_CFG(17)),
               rd(ctx.tlmm, GPIO_CFG(18)), rd(ctx.tlmm, GPIO_CFG(19)),
               rd(ctx.tlmm, GPIO_CFG(20)), rd(ctx.tlmm, GPIO_CFG(21)));

    gpio_cfg(ctx.tlmm, PIN_CLK, 0, 0, 6, true);
    gpio_cfg(ctx.tlmm, PIN_CS, 0, 3, 6, true);
    gpio_cfg(ctx.tlmm, PIN_INT, 0, 3, 6, false);
    gpio_set(ctx.tlmm, PIN_CS, 1);
    gpio_set(ctx.tlmm, PIN_CLK, 0);

    gpio_set(ctx.tlmm, 16, 1);
    bool m16_hi = gpio_get(ctx.tlmm, 16);
    gpio_set(ctx.tlmm, 16, 0);
    bool m16_lo = gpio_get(ctx.tlmm, 16);
    gpio_set(ctx.tlmm, 18, 1);
    bool c18_hi = gpio_get(ctx.tlmm, 18);
    gpio_set(ctx.tlmm, 18, 0);
    bool c18_lo = gpio_get(ctx.tlmm, 18);
    gpio_set(ctx.tlmm, 19, 1);
    bool cs_hi = gpio_get(ctx.tlmm, 19);
    gpio_set(ctx.tlmm, 19, 0);
    bool cs_lo = gpio_get(ctx.tlmm, 19);
    xnu_kprint("SWEET TOUCH: pin test: 16=(%d,%d) 18=(%d,%d) 19=(%d,%d) INT=%d\n",
               m16_hi?1:0, m16_lo?1:0, c18_hi?1:0, c18_lo?1:0, cs_hi?1:0, cs_lo?1:0,
               gpio_get(ctx.tlmm, PIN_INT) ? 1 : 0);

    // Adaptive probe: test both pin assignments (16=MOSI/17=MISO or 17=MOSI/16=MISO)
    // and both dummy-byte lengths (3 and 4 bytes)
    uint8_t version[32];
    bool probed = false;
    uint8_t spi_mode = 1;

    for (int pin_cfg = 0; pin_cfg < 2 && !probed; pin_cfg++) {
        if (pin_cfg == 0) {
            ctx.pin_mosi = 16;
            ctx.pin_miso = 17;
        } else {
            ctx.pin_mosi = 17;
            ctx.pin_miso = 16;
        }
        gpio_cfg(ctx.tlmm, ctx.pin_mosi, 0, 0, 6, true);
        gpio_cfg(ctx.tlmm, ctx.pin_miso, 0, 0, 6, false);
        gpio_set(ctx.tlmm, ctx.pin_mosi, 0);

        // Send SPI mode select command
        goodix_write(&ctx, 0xc900, &spi_mode, 1);
        delay(5, 1000000);

        for (int d = 3; d <= 4; d++) {
            ctx.dummy_bytes = d;
            uint32_t addrs[3] = {0x10068, 0x10014, 0x13400};
            for (int a = 0; a < 3 && !probed; a++) {
                uint32_t addr = addrs[a];
                for (int i = 0; i < 32; i++) version[i] = 0;
                if (goodix_read(&ctx, addr, version, 28)) {
                    xnu_kprint("SWEET TOUCH: probe mosi=%d miso=%d d=%d a=0x%05x -> %02x %02x %02x %02x\n",
                               ctx.pin_mosi, ctx.pin_miso, d, addr,
                               (uint32_t)version[0], (uint32_t)version[1], (uint32_t)version[2], (uint32_t)version[3]);
                    if ((version[0] >= '0' && version[0] <= '9') ||
                        (version[0] >= 'A' && version[0] <= 'Z') ||
                        (version[0] >= 'a' && version[0] <= 'z')) {
                        xnu_kprint("SWEET TOUCH: Goodix GT9916R found! IC=%.6s FW=%.8s (mosi=%d miso=%d dummy=%d)\n",
                                   (const char *)version, (const char *)version + 10, ctx.pin_mosi, ctx.pin_miso, d);
                        probed = true;
                        break;
                    }
                }
            }
        }
    }

    if (!probed) {
        xnu_kprint("SWEET TOUCH: fallback to default pinout (mosi=16 miso=17 dummy=4)\n");
        ctx.pin_mosi = 16;
        ctx.pin_miso = 17;
        ctx.dummy_bytes = 4;
        gpio_cfg(ctx.tlmm, 16, 0, 0, 6, true);
        gpio_cfg(ctx.tlmm, 17, 0, 0, 6, false);
        goodix_write(&ctx, 0xc900, &spi_mode, 1);
    }

    uint32_t touch_address = 0x13400;
    uint32_t cfg_addr = (ctx.dummy_bytes == 4) ? 0x10068 : 0x10070;
    uint8_t info_hdr[2];
    if (goodix_read(&ctx, cfg_addr, info_hdr, 2)) {
        uint16_t info_len = le16(info_hdr);
        if (info_len > 40 && info_len < 512) {
            uint8_t info[256];
            if (goodix_read(&ctx, cfg_addr, info, sizeof(info))) {
                uint32_t offset = 2 + 16 + 10 + 4;
                for (int i = 0; i < 5 && offset < sizeof(info); i++) {
                    offset += 1 + 2 * info[offset];
                }
                if (offset + 48 < sizeof(info)) {
                    uint32_t parsed_addr = le32(info + offset + 44);
                    if (parsed_addr >= 0x10000 && parsed_addr <= 0x80000) {
                        touch_address = parsed_addr;
                    }
                }
            }
        }
    }
    if (touch_address == 0 || touch_address < 0x10000 || touch_address > 0x80000) {
        touch_address = 0x13400;
    }
    xnu_kprint("SWEET TOUCH: report address 0x%05x\n", touch_address);

    struct contact_state prev_contacts[10];
    for (int i = 0; i < 10; i++) {
        prev_contacts[i].down = false;
        prev_contacts[i].x = 0;
        prev_contacts[i].y = 0;
    }

    uint8_t report[8 + 8 * 10];
    uint8_t zero = 0;
    uint32_t report_count = 0;
    uint32_t idle_ticks = 0;
    uint32_t key_ticks = 0;
    uint32_t alt_ticks = 0;
    uint32_t spmi_err_count = 0;
    bool pwr_down = false;
    bool vdn_down = false;
    bool vup_down = false;
    bool int_last = true;

    // Send wake-up Power button press to wake up SpringBoard and display
    hid_key(0, 1);
    delay(50, 1000000);
    hid_key(0, 0);

    xnu_kprint("SWEET TOUCH: Marble Goodix native hardware polling active (125 Hz). Touch & Physical Keys ON!\n");

    while (1) {
        delay(8, 1000000); // 8ms sleep = 125 Hz
        idle_ticks++;
        key_ticks++;
        alt_ticks++;

        // Physical Buttons Polling (every 64ms = 8 ticks)
        if (spmi_obs && spmi_err_count < 5 && key_ticks >= 8) {
            key_ticks = 0;
            uint8_t pon = 0;
            if (apid_pon != 0xffff && spmi_read_byte(spmi_obs, apid_pon, 0x0810, &pon)) {
                spmi_err_count = 0;
                bool pwr = (pon & 1) != 0;
                bool vdn = (pon & 2) != 0;
                if (pwr != pwr_down) {
                    pwr_down = pwr;
                    hid_key(0, pwr ? 1 : 0);
                    xnu_kprint("SWEET KEYS: Physical Power %s\n", pwr ? "PRESSED" : "RELEASED");
                }
                if (vdn != vdn_down) {
                    vdn_down = vdn;
                    hid_key(2, vdn ? 1 : 0);
                    xnu_kprint("SWEET KEYS: Physical Vol- %s\n", vdn ? "PRESSED" : "RELEASED");
                }
            } else if (apid_pon != 0xffff) {
                spmi_err_count++;
            }
            uint8_t gpio = 0;
            if (apid_volup != 0xffff && spmi_read_byte(spmi_obs, apid_volup, 0xc510, &gpio)) {
                bool vup = (gpio & 0x80) != 0;
                if (vup != vup_down) {
                    vup_down = vup;
                    hid_key(1, vup ? 1 : 0);
                    xnu_kprint("SWEET KEYS: Physical Vol+ %s\n", vup ? "PRESSED" : "RELEASED");
                }
            }
        }

        // Monitor INT pin (GPIO 21)
        bool int_now = gpio_get(ctx.tlmm, PIN_INT);
        if (int_now != int_last) {
            int_last = int_now;
            xnu_kprint("SWEET TOUCH: INT level -> %d\n", int_now ? 1 : 0);
        }

        // Touch Screen Polling: read touch report every tick (125 Hz)
        if (!goodix_read(&ctx, touch_address, report, 8)) {
            continue;
        }

        uint8_t status = report[0];
        if (status != 0 && status != 0xff && report_count < 20) {
            report_count++;
            xnu_kprint("SWEET TOUCH: raw report: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                       (uint32_t)report[0], (uint32_t)report[1], (uint32_t)report[2], (uint32_t)report[3],
                       (uint32_t)report[4], (uint32_t)report[5], (uint32_t)report[6], (uint32_t)report[7]);
        }

        // If inactive or invalid status:
        if (status == 0 || status == 0xff || !(status & 0xf0)) {
            if (status != 0 && status != 0xff) {
                goodix_write(&ctx, touch_address, &zero, 1);
            }
            // If no activity on current address for 250 ticks (~2 sec), cycle through known Goodix addresses
            if (status == 0 && alt_ticks > 250) {
                alt_ticks = 0;
                if (touch_address == 0x13400) touch_address = 0x10400;
                else if (touch_address == 0x10400) touch_address = 0x10008;
                else touch_address = 0x13400;
            }
            for (int i = 0; i < 10; i++) {
                if (prev_contacts[i].down) {
                    prev_contacts[i].down = false;
                    hid_touch(i, 0, prev_contacts[i].x, prev_contacts[i].y);
                }
            }
            continue;
        }

        // Active touch event detected! Lock onto this address:
        alt_ticks = 0;
        uint8_t count = report[2] & 0x0f;
        if (count > 10) count = 10;
        if (count == 0) count = 1;

        if (count > 0) {
            goodix_read(&ctx, touch_address + 8, report + 8, count * 8);
        }

        goodix_write(&ctx, touch_address, &zero, 1);

        bool active[10];
        for (int i = 0; i < 10; i++) active[i] = false;

        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *pt = report + 8 + i * 8;
            uint8_t id = pt[0] >> 4;
            if (id >= 10) id = pt[0] & 0x0f;
            if (id >= 10) continue;

            uint32_t raw_x = le16(pt + 2);
            uint32_t raw_y = le16(pt + 4);

            if (raw_x > 1080) raw_x = 1080;
            if (raw_y > 2400) raw_y = 2400;

            uint32_t mapped_x = (raw_x * 1125) / 1080;
            uint32_t mapped_y = (raw_y * 2436) / 2400;
            if (mapped_x > 1125) mapped_x = 1125;
            if (mapped_y > 2436) mapped_y = 2436;

            active[id] = true;
            prev_contacts[id].x = mapped_x;
            prev_contacts[id].y = mapped_y;
            prev_contacts[id].down = true;

            hid_touch((int)id, 1, (int)mapped_x, (int)mapped_y);

            if (report_count < 10) {
                xnu_kprint("SWEET TOUCH: contact %u down at %u,%u (raw %u,%u)\n",
                           (uint32_t)id, mapped_x, mapped_y, raw_x, raw_y);
            }
        }

        for (int i = 0; i < 10; i++) {
            if (prev_contacts[i].down && !active[i]) {
                prev_contacts[i].down = false;
                hid_touch(i, 0, prev_contacts[i].x, prev_contacts[i].y);
                if (report_count < 10) {
                    xnu_kprint("SWEET TOUCH: contact %u up\n", (uint32_t)i);
                }
            }
        }
        report_count++;
    }

    while (1) {
        delay(1000, 1000000);
    }
}
