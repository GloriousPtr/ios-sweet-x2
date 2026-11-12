/* Realme X2 display helpers: panel is DSI command mode, so a frame must be started by software. */
#include <stdint.h>
#include "console.h"
#include "dpu.h"

#ifndef X2_KICK_DEFAULT
#define X2_KICK_DEFAULT 0
#endif

#define UNUSED      __attribute__((unused))
#define BLACK       0xff000000U
#define MAGENTA     0xffff00ffU

/* clocks / power domain (GCC 0x100000, DISP_CC 0xaf00000) */
#define GCC_DISP_AHB_CBCR   0x0010b00cUL
#define DISPCC          0x0af00000UL
#define MDSS_GDSCR      0x0af03000UL
#define DISPCC_PCLK0        (DISPCC + 0x2004UL)
#define DISPCC_MDP      (DISPCC + 0x200cUL)
#define DISPCC_BYTE0        (DISPCC + 0x2028UL)
#define DISPCC_BYTE0_INTF   (DISPCC + 0x202cUL)
#define DISPCC_AHB      (DISPCC + 0x2080UL)
#define CBCR_OFF        (1u << 31)
#define GDSCR_PWR_ON        (1u << 31)

/* MDP at 0xae00000; block offsets are absolute from there */
#define MDP_BASE    0x0ae00000UL
#define NCTL        6
#define CTL(n)      (MDP_BASE + 0x2000UL + 0x200UL * (n))
#define CTL_TOP     0x014
#define CTL_FLUSH   0x018
#define CTL_START   0x01c
#define PP(n)       (MDP_BASE + 0x71000UL + 0x800UL * (n))
#define PP_TEAR_EN  0x000
#define PP_AUTOREFRESH  0x030
#define INTF_TIMING_EN  0x000
#define INTF_CONFIG 0x004
#define INTF_PANEL_FMT  0x090
#define INTF_STATUS 0x26c

static const UNUSED uint32_t intf_off[4] = { 0x6b000, 0x6b800, 0x6c000, 0x6c800 };
static const UNUSED uint32_t sspp_off[5] = { 0x5000, 0x7000, 0x25000, 0x27000, 0x29000 };

static int kick_mode;   /* 0 none, 1..3 one strategy, 4 try all */

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }

static UNUSED void sleep_ms(unsigned ms)
{
    uint64_t f, t0, t;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(t0) :: "memory");
    uint64_t ticks = f / 1000 * ms;
    do {
        __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(t) :: "memory");
    } while (t - t0 < ticks);
}

/* 0 = safe to touch the display controller, else the first gate that failed */
static int dpu_gates(void)
{
    if (rd(GCC_DISP_AHB_CBCR) & CBCR_OFF)
        return 1;
    if (!(rd(MDSS_GDSCR) & GDSCR_PWR_ON))
        return 2;
    if (rd(DISPCC_AHB) & CBCR_OFF)
        return 3;
    if (rd(DISPCC_MDP) & CBCR_OFF)
        return 4;
    return 0;
}

/* 1: CTL_START on CTL0. 2: pingpong 0 autorefresh. 3: CTL_START on every configured CTL. */
void dpu_kick(int s)
{
    if (s < 1 || s > 3 || dpu_gates())
        return;
    dsb();
    if (s == 2) {
        wr(PP(0) + PP_AUTOREFRESH, (1u << 31) | 1u);
    } else if (s == 3) {
        for (unsigned n = 0; n < NCTL; n++)
            if (rd(CTL(n) + CTL_TOP))
                wr(CTL(n) + CTL_START, 1);
    } else {
        wr(CTL(0) + CTL_START, 1);
    }
    dsb();
}

void dpu_kick_auto(void)
{
    if (kick_mode == 4) {
        dpu_kick(1);
        dpu_kick(2);
        dpu_kick(3);
    } else if (kick_mode) {
        dpu_kick(kick_mode);
    }
}

static UNUSED void hex_off(char *b, unsigned off)
{
    static const char hex[] = "0123456789ABCDEF";
    b[0] = '+';
    b[1] = hex[(off >> 8) & 0xf];
    b[2] = hex[(off >> 4) & 0xf];
    b[3] = hex[off & 0xf];
    b[4] = '=';
    b[5] = 0;
}

static UNUSED void row2(const char *name, uintptr_t base, unsigned o1, unsigned o2)
{
    char a[6], b[6];
    hex_off(a, o1);
    hex_off(b, o2);
    con_hex2(name, a, rd(base + o1), b, rd(base + o2));
}

static UNUSED void dump_page(void)
{
    con_clear(BLACK);
    con_str("X2 DPU", "state after kick test");
    con_hex2("CLK", "ahb=", rd(GCC_DISP_AHB_CBCR), "mdp=", rd(DISPCC_MDP));
    con_hex2("CLK", "pcl=", rd(DISPCC_PCLK0), "byt=", rd(DISPCC_BYTE0));
    con_hex2("CLK", "bif=", rd(DISPCC_BYTE0_INTF), "gdsc=", rd(MDSS_GDSCR));
    con_skip();
    row2("CTL0", CTL(0), CTL_TOP, CTL_FLUSH);
    row2("CTL0", CTL(0), CTL_START, 0x000);
    row2("CTL1", CTL(1), CTL_TOP, CTL_FLUSH);
    row2("CTL2", CTL(2), CTL_TOP, CTL_FLUSH);
    row2("PP0", PP(0), PP_TEAR_EN, PP_AUTOREFRESH);
    row2("PP1", PP(1), PP_TEAR_EN, PP_AUTOREFRESH);
    row2("INTF1", MDP_BASE + intf_off[1], INTF_TIMING_EN, INTF_CONFIG);
    row2("INTF1", MDP_BASE + intf_off[1], INTF_PANEL_FMT, INTF_STATUS);
    con_skip();
    for (unsigned i = 0; i < 5; i++) {
        char name[6] = { 'S', 'S', 'P', 'P', (char)('0' + i), 0 };
        uintptr_t b = MDP_BASE + sspp_off[i];
        row2(name, b, 0x00, 0x0c);  /* src size, out size */
        row2(name, b, 0x14, 0x24);  /* src0 address, y stride */
        row2(name, b, 0x30, 0x38);  /* src format, op mode */
    }
}

static UNUSED void step(uint32_t color, int strategy)
{
    con_clear(color);
    dpu_kick(strategy);
    sleep_ms(3000);
}

/* Read-only: show the raw display clock/power registers (dispcc only after GCC says its bus clock is on). */
static UNUSED void gate_report(void)
{
    uint32_t gcc = rd(GCC_DISP_AHB_CBCR);
    con_hex2("GCC", "ahb=", gcc, 0, 0);
    if (gcc & CBCR_OFF) {
        con_str("GATE", "gcc display ahb reports off");
        return;
    }
    con_hex2("MDSS", "gdsc=", rd(MDSS_GDSCR), "ahb=", rd(DISPCC_AHB));
    con_hex2("MDSS", "mdp=", rd(DISPCC_MDP), "pcl=", rd(DISPCC_PCLK0));
    con_hex2("MDSS", "byt=", rd(DISPCC_BYTE0), "bif=", rd(DISPCC_BYTE0_INTF));
}

int x2_display_init(void)
{
#if X2_KICK_DEFAULT == 9
    /* The panel shows framebuffer writes by itself: never touch the MDP, just report and go on. */
    con_str("X2 DPU", "no kick; clock/power regs:");
    gate_report();
    sleep_ms(20000);
    kick_mode = 0;
    return 0;
#else
    int g = dpu_gates();
    if (g)
        return g;
#if X2_KICK_DEFAULT >= 1 && X2_KICK_DEFAULT <= 3
    kick_mode = X2_KICK_DEFAULT;
#else
    kick_mode = 0;
    step(WHITE, 0);
    step(GREEN, 1);
    step(RED, 2);
    step(MAGENTA, 3);
    kick_mode = 4;
    dump_page();
    sleep_ms(60000);
#endif
    return 0;
#endif
}
