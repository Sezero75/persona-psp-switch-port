#include <switch.h>
#include <arm_neon.h>
#include "recomp.h"
#include "dialogs.h"
#include "dialog_font.h"

enum { ScreenWidth = 480, ScreenHeight = 272 };
static Framebuffer display;
static PadState pad;
static bool ready, quitting;
static uint32_t held, pulses, overlay_pulses;
static uint64_t poll_time;
static HidAnalogStickState stick;
static uint64_t dialog_consumed;
extern uint64_t nx_ge_list_ns;
extern void nx_audio_stats(uint64_t *gaps, uint64_t *lost);

static void record_present(uint64_t start_ticks) {
    static uint64_t window_start, ge_start, display_ticks;
    static unsigned frames;
    uint64_t now = armGetSystemTick();
    if (!frames) { window_start = start_ticks; ge_start = nx_ge_list_ns; }
    ++frames;
    display_ticks += now - start_ticks;
    uint64_t ns = armTicksToNs(now - window_start);
    if (ns >= 10000000000ull) {
        uint64_t gaps, lost;
        nx_audio_stats(&gaps, &lost);
        fprintf(stderr, "PERFORMANCE fps=%.1f ge_ms_per_frame=%.1f display_ms_per_frame=%.1f audio_short_gaps_total=%llu audio_dropped_frames_total=%llu\n",
                (double)frames * 1e9 / ns, (double)(nx_ge_list_ns - ge_start) / frames / 1e6,
                (double)armTicksToNs(display_ticks) / frames / 1e6, (unsigned long long)gaps, (unsigned long long)lost);
        frames = 0;
        display_ticks = 0;
    }
}

static uint32_t psp_buttons(uint64_t value) {
    static const struct { uint64_t nx; uint32_t psp; } map[] = {
        {HidNpadButton_Minus, 1}, {HidNpadButton_Plus, 8},
        {HidNpadButton_Up, 0x10}, {HidNpadButton_Right, 0x20},
        {HidNpadButton_Down, 0x40}, {HidNpadButton_Left, 0x80},
        {HidNpadButton_L, 0x100}, {HidNpadButton_R, 0x200},
        {HidNpadButton_X, 0x1000}, {HidNpadButton_A, 0x2000},
        {HidNpadButton_B, 0x4000}, {HidNpadButton_Y, 0x8000},
    };
    uint32_t result = 0;
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (value & map[i].nx) result |= map[i].psp;
    return result;
}

void gui_init(const char *title) {
    (void)title;
    if (ready) return;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    Result rc = framebufferCreate(&display, nwindowGetDefault(), ScreenWidth, ScreenHeight,
                                  PIXEL_FORMAT_RGBA_8888, 2);
    if (R_SUCCEEDED(rc)) {
        rc = framebufferMakeLinear(&display);
        if (R_FAILED(rc)) framebufferClose(&display);
    }
    ready = R_SUCCEEDED(rc);
    if (!ready) {
        fprintf(stderr, "Switch framebuffer setup failed (0x%x)\n", rc);
        quitting = true;
        return;
    }
    atexit(gui_shutdown);
}

void gui_shutdown(void) {
    if (ready) framebufferClose(&display);
    ready = false;
}
void gui_pump(void) {
    if (!ready || quitting) return;
    if (!appletMainLoop()) { quitting = true; return; }
    padUpdate(&pad);
    uint64_t keys = padGetButtons(&pad);
    dialog_consumed &= keys;
    keys &= ~dialog_consumed;
    uint64_t exit_chord = HidNpadButton_Plus | HidNpadButton_Minus;
    if ((keys & exit_chord) == exit_chord) quitting = true;
    held = psp_buttons(keys);
    uint32_t down = psp_buttons(padGetButtonsDown(&pad) & ~dialog_consumed);
    pulses |= down;
    overlay_pulses |= down;
    stick = padGetStickPos(&pad, 0);
    poll_time = armTicksToNs(armGetSystemTick());
}

static inline unsigned expand(unsigned value, unsigned bits) {
    return (value * 255u + ((1u << bits) - 1u)/2) / ((1u << bits) - 1u);
}
static inline uint32_t unpack16(uint16_t p, int fmt) {
    unsigned r, g, b;
    if (fmt == 2) {
        r = (p & 15) * 17; g = ((p >> 4) & 15) * 17; b = ((p >> 8) & 15) * 17;
    } else {
        r = expand(p & 31, 5);
        g = fmt == 0 ? expand((p >> 5) & 63, 6) : expand((p >> 5) & 31, 5);
        b = expand((p >> (fmt == 0 ? 11 : 10)) & 31, 5);
    }
    return 0xff000000u | r | (g << 8) | (b << 16);
}
int gui_present(uint32_t address, int fmt, uint32_t stride) {
    if (!ready || quitting || !g_mem || !address || fmt < 0 || fmt > 3) return 0;
    if (stride == 0) stride = 512;
    if (stride < ScreenWidth || stride > 2048) return 0;
    uint32_t bpp = fmt == 3 ? 4 : 2;
    uint32_t span = ((ScreenHeight-1)*stride + ScreenWidth)*bpp;
    if (!sr_inrange_n(address, span)) return 0;
    const uint64_t start_ticks = armGetSystemTick();
    uint32_t pitch;
    uint8_t *pixels = framebufferBegin(&display, &pitch);
    if (!pixels) return 0;
    const uint32_t phys = SR_PHYS(address);
    const bool linear = (phys >= 0x04000000 && phys + span <= 0x04200000) ||
                        (phys >= SR_RAM_BASE && phys + span <= 0x0c000000);
    if (fmt == 3 && linear) {
        const uint8_t *src = SR_HOST_LINEAR(phys);
        const uint32x4_t alpha = vdupq_n_u32(0xff000000u);
        for (unsigned y = 0; y < ScreenHeight; ++y) {
            uint8_t *dst = pixels + y * pitch;
            for (unsigned x = 0; x < ScreenWidth * 4; x += 16) {
                uint32x4_t pixel = vreinterpretq_u32_u8(vld1q_u8(src + x));
                vst1q_u8(dst + x, vreinterpretq_u8_u32(vorrq_u32(pixel, alpha)));
            }
            src += stride * 4;
        }
    } else for (unsigned y=0; y<ScreenHeight; ++y) {
        uint32_t *dst = (uint32_t *)(pixels + y*pitch);
        uint32_t row = address + y*stride*bpp;
        for (unsigned x=0; x<ScreenWidth; ++x) {
            if (fmt == 3) {
                // Scanout reads pixel storage even where CPU reads address a GE status port.
                uint32_t pixel;
                memcpy(&pixel, SR_HOST(row+x*4), sizeof(pixel));
                dst[x] = pixel | 0xff000000u;
            } else {
                dst[x] = unpack16(sr_r16(row+x*2), fmt);
            }
        }
    }
    framebufferEnd(&display);
    record_present(start_ticks);
    gui_pump();
    return 1;
}
int gui_on(void) { return ready; }
int gui_interactive(void) { return ready; }
int gui_quit_requested(void) { return quitting; }
int gui_stop_requested(void) { return quitting; }
uint32_t gui_buttons(void) { return held | pulses; }
uint32_t gui_consume_button_pulses(void) { uint32_t v=pulses; pulses=0; return v; }
uint32_t gui_pad_pulses_take(void) { uint32_t v=overlay_pulses; overlay_pulses=0; return v; }
uint64_t gui_input_poll_ns(void) { return poll_time; }
int gui_pad_present(void) { return padIsConnected(&pad); }
int gui_stick_dead_zone(void) { return 16; }
void gui_analog(uint8_t *x, uint8_t *y) {
    if (x) *x = (uint8_t)((stick.x + 32768)/256);
    if (y) *y = (uint8_t)((32767 - stick.y)/256);
}
void gui_ace_vblank(uint32_t frame) { (void)frame; }
int gui_headless_acceptance_requested(void) { return 0; }
int gui_headless_acceptance_ready(void) { return 0; }
int gui_headless_acceptance_complete(void) { return 0; }
uint32_t gui_headless_acceptance_presented_frames(void) { return 0; }
int gui_pause_presenter(void) { return 0; }
int gui_pause_busy_reason(void) { return quitting ? SR_PAUSE_REFUSED_STOPPING : 0; }
void gui_pause_enter(void) {}
void gui_pause_leave(void) {}
SrPauseFrame gui_pause_frame(void) { return SR_PAUSE_FRAME_RESUME; }

static void draw_text(uint8_t *pixels, uint32_t pitch, int x, int y, const char *text,
                      size_t length, uint32_t color) {
    for (size_t i = 0; i < length && text[i] && x + 8 <= ScreenWidth; ++i, x += 8) {
        unsigned c = (unsigned char)text[i];
        const unsigned char *glyph = dialog_font[c >= 32 && c <= 126 ? c - 32 : '?' - 32];
        for (int row = 0; row < 14; ++row) {
            uint32_t *dst = (uint32_t *)(pixels + (y + row) * pitch) + x;
            for (unsigned col = 0; col < 8; ++col)
                if (glyph[row] & (1u << col)) dst[col] = color;
        }
    }
}

static int draw_message(uint8_t *pixels, uint32_t pitch, const char *p) {
    int y = 32;
    const size_t width = 56;
    while (*p && y <= 172) {
        size_t length = strcspn(p, "\r\n");
        if (length > width) {
            length = width;
            while (length && p[length] != ' ') --length;
            if (!length) length = width;
        }
        draw_text(pixels, pitch, 12, y, p, length, 0xffffffff);
        y += 14;
        p += length;
        while (*p == ' ') ++p;
        if (*p == '\r') ++p;
        if (*p == '\n') ++p;
    }
    return y + 5;
}

int nx_choose(const char *title, const char *message, const char *const *choices,
              size_t count, size_t initial, int can_cancel) {
    if (!ready || !count) return -1;
    size_t selected = initial < count ? initial : 0;
    pulses = overlay_pulses = held = 0;
    padUpdate(&pad);
    int answer = -1;
    while (!quitting && appletMainLoop()) {
        uint32_t pitch = 0;
        uint8_t *pixels = framebufferBegin(&display, &pitch);
        if (!pixels) break;
        for (int y = 0; y < ScreenHeight; ++y) {
            uint32_t *row = (uint32_t *)(pixels + y * pitch);
            for (int x = 0; x < ScreenWidth; ++x) row[x] = 0xff251d19u;
        }
        draw_text(pixels, pitch, 12, 8, title, 56, 0xffaddfffu);
        int top = draw_message(pixels, pitch, message);
        size_t visible = (size_t)((245 - top) / 19);
        size_t first = selected < visible ? 0 : selected + 1 - visible;
        for (size_t i = first; i < count && i < first + visible; ++i) {
            int y = top + (int)(i - first) * 19;
            if (i == selected) draw_text(pixels, pitch, 12, y, ">", 1, 0xffaddfffu);
            draw_text(pixels, pitch, 30, y, choices[i], 54, i == selected ? 0xffaddfffu : 0xffffffffu);
        }
        draw_text(pixels, pitch, 12, 250, can_cancel ? "B  Select     A  Back" : "B  Select", 55, 0xffcfbdb8u);
        framebufferEnd(&display);
        padUpdate(&pad);
        uint64_t keys = padGetButtons(&pad);
        const uint64_t exit_keys = HidNpadButton_Plus | HidNpadButton_Minus;
        if ((keys & exit_keys) == exit_keys) break;
        uint64_t down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_B) { answer = (int)selected; goto finished; }
        if (can_cancel && (down & HidNpadButton_A)) goto finished;
        if (down & (HidNpadButton_Up | HidNpadButton_Left))
            selected = selected ? selected - 1 : count - 1;
        if (down & (HidNpadButton_Down | HidNpadButton_Right))
            selected = (selected + 1) % count;
    }
    quitting = true;
finished:
    dialog_consumed = padGetButtons(&pad);
    return answer;
}
