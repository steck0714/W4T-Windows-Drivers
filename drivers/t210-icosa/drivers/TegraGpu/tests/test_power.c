/* SPDX-License-Identifier: GPL-2.0-only */
#include "../platform/t210_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Software model of the Erista GPU power path (not hardware). Register
 * accesses outside the whitelist abort, so an extra write is a test failure. */
struct model {
    uint32_t hidrev, clocks, reset, out5, clamp, flush;
    uint8_t gpio6, ame, vout, dvs, ctrl1, ctrl2;
    unsigned int writes;
    bool fail_voltage_once, fail_gpio, fail_mc, fail_reset, fail_out5;
    bool fail_clamp_set, fail_reset_set, unreadable_mc_status;
    bool rail_on; /* derived: EN pin high and enable bit set */
};

static uint32_t read32(void *opaque, uint32_t address)
{
    struct model *m = opaque;
    switch (address) {
    case 0x70000804: return m->hidrev;
    case 0x60006280: return m->clocks;
    case 0x6000628c: return m->reset;
    case 0x6000667c: return m->out5;
    case 0x7000e6d4: return m->clamp;
    case 0x70019970: return m->flush;
    case 0x70019974: return m->unreadable_mc_status ? UINT32_MAX : m->fail_mc ? 0 : m->flush;
    default: assert(!"unexpected register read"); return 0;
    }
}

static void write32(void *opaque, uint32_t address, uint32_t value)
{
    struct model *m = opaque;
    ++m->writes;
    switch (address) {
    case 0x60006284: m->clocks |= value; break;
    case 0x60006288: m->clocks &= ~value; break;
    case 0x60006290: if (!m->fail_reset_set) m->reset |= value; break;
    case 0x60006294:
        assert(!(m->clocks & (1u << 24))); /* upstream gate/reset ordering */
        if (!m->fail_reset) m->reset &= ~value;
        break;
    case 0x6000667c: if (!m->fail_out5) m->out5 = value; break;
    case 0x7000e6d4:
        if ((value & 1) && m->fail_clamp_set) break;
        if (!(value & 1)) {
            /* The rail must be on and 800 mV programmed before unclamp. */
            assert((m->gpio6 & 0x0b) == 0x09 && (m->vout & 0x80) && (m->vout & 0x7f) == 31);
            assert((m->clocks & 0x21000000) == 0x21000000);
            assert(m->reset & (1u << 24));
        }
        m->clamp = value;
        break;
    case 0x70019970: m->flush = value; break;
    default: assert(!"unexpected register write");
    }
}

static bool read_pmic(void *opaque, uint8_t address, uint8_t reg, uint8_t *value)
{
    struct model *m = opaque;
    if (address == 0x3c) {
        if (reg == 0x3c) *value = m->gpio6;
        else if (reg == 0x40) *value = m->ame;
        else assert(!"unexpected MAX77620 read");
    } else {
        assert(address == 0x1c);
        switch (reg) {
        case 0: *value = m->vout; break;
        case 1: *value = m->dvs; break;
        case 2: *value = m->ctrl1; break;
        case 3: *value = m->ctrl2; break;
        default: assert(!"unexpected MAX77621 read");
        }
    }
    return true;
}

static bool write_pmic(void *opaque, uint8_t address, uint8_t reg, uint8_t value)
{
    struct model *m = opaque;
    ++m->writes;
    if (address == 0x3c) {
        if (reg == 0x3c) {
            if (m->fail_gpio && (value & 8)) return false;
            m->gpio6 = value;
        } else if (reg == 0x40) m->ame = value;
        else assert(!"unexpected MAX77620 write");
        return true;
    }
    assert(address == 0x1c);
    switch (reg) {
    case 0:
        if (m->fail_voltage_once) { m->fail_voltage_once = false; return false; }
        m->vout = value; break;
    case 1: m->dvs = value; break;
    case 2: m->ctrl1 = value; break;
    case 3: m->ctrl2 = value; break;
    default: assert(!"unexpected MAX77621 write");
    }
    return true;
}

static void delay_us(void *opaque, uint32_t us)
{
    (void)opaque;
    assert(us == 2 || us == 10 || us == 5000);
}

static void check_restored(const struct model *m, const struct model *i)
{
    assert(m->gpio6 == i->gpio6 && m->ame == i->ame && m->vout == i->vout);
    assert(m->dvs == i->dvs && m->ctrl1 == i->ctrl1 && m->ctrl2 == i->ctrl2);
}

int main(void)
{
    /* Hekate state at OS entry: GPIO6 input/open-drain (EN low), GPU rail
     * configured at POR defaults, GPU in reset and clamped. */
    const struct model initial = {
        .hidrev = 0x00012114, .clocks = 0x40, .reset = 0x01000000,
        .out5 = 0x00fc1234, .clamp = 0x81, .flush = 0x80,
        .gpio6 = 0x02, .ame = 0x00, .vout = 0x3f, .dvs = 0x3f,
        .ctrl1 = 0x03, .ctrl2 = 0xc1
    };
    struct model model = initial;
    struct tg_gpu_power state = {0};
    struct tg_gpu_power_io io = { &model, read32, write32, read_pmic, write_pmic, delay_us };

    assert(tg_gpu_max77621_code(800000) == 31);
    assert(tg_gpu_max77621_code(1000000) == 63);
    assert(tg_gpu_max77621_code(1100000) == 79);
    assert(tg_gpu_max77621_code(606250) == 0 && tg_gpu_max77621_code(100) == 0);
    assert(tg_gpu_max77621_code(5000000) == 127);

    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_OK);
    assert(state.ready && state.owned && state.stage == TG_GPU_STAGE_READY);
    assert((model.gpio6 & 0x0b) == 0x09 && model.ctrl1 == 0xb0 && model.ctrl2 == 0xc1);
    assert(model.clamp == 0x80 && model.reset == 0);
    assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_OK);
    assert(!state.owned && !state.ready);
    check_restored(&model, &initial);
    assert(model.clocks == initial.clocks && model.out5 == initial.out5);
    assert(model.clamp == initial.clamp && model.reset == initial.reset);
    assert(model.flush == initial.flush);

    /* Mariko (major revision 2) must be refused without touching anything. */
    model = initial; memset(&state, 0, sizeof(state));
    model.hidrev = 0x00012124;
    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_UNSUPPORTED_BOARD);
    assert(model.writes == 0 && !state.owned);

    /* Wrong PMIC address is a caller bug. */
    model = initial;
    assert(tg_gpu_power_up(&io, 0x33, &state) == TG_GPU_POWER_BAD_ARGUMENT);
    assert(model.writes == 0);

    /* OUT5 still at the Hekate reset divider. */
    model = initial; model.out5 = 0x00030000; model.reset = 0x41e12019; model.clamp = 1;
    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_OK);
    assert((model.out5 & 0xff070000u) == 0x02070000u);
    assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_OK);
    assert(model.out5 == 0x00030000 && model.reset == 0x41e12019 && model.clamp == 1);
    check_restored(&model, &initial);

    model = initial; memset(&state, 0, sizeof(state));
    model.reset = 0; /* an already owned GPU must not be reset */
    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_BUSY);
    assert(model.writes == 0 && !state.owned);

    model = initial; model.fail_voltage_once = true;
    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_PMIC_ERROR);
    assert(state.owned && !state.ready && state.stage == TG_GPU_STAGE_RAIL);
    assert((model.gpio6 & 8) == 0); /* EN never raised */
    assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_OK);
    check_restored(&model, &initial);

    model = initial; memset(&state, 0, sizeof(state)); model.fail_gpio = true;
    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_PMIC_ERROR);
    assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_OK);
    check_restored(&model, &initial);

    model = initial; memset(&state, 0, sizeof(state)); model.fail_out5 = true;
    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_CLOCK_ERROR);
    assert(state.stage == TG_GPU_STAGE_CLOCKS && !state.ready);
    assert(model.clamp == initial.clamp && model.reset == initial.reset);
    assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_OK);
    check_restored(&model, &initial);

    model = initial; memset(&state, 0, sizeof(state)); model.fail_reset = true;
    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_RESET_ERROR);
    assert(!state.ready);
    assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_OK);
    check_restored(&model, &initial);

    model = initial; memset(&state, 0, sizeof(state)); model.fail_mc = true;
    assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_MC_TIMEOUT);
    assert(state.owned && !state.ready);
    assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_MC_TIMEOUT);
    assert(state.owned && (model.gpio6 & 8)); /* rail stays up while MC may be live */
    model.fail_mc = false;
    assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_OK);

    /* Faults after a successful power-up must not release the rail/arena. */
    for (unsigned failure = 0; failure < 3; ++failure) {
        model = initial; memset(&state, 0, sizeof(state));
        assert(tg_gpu_power_up(&io, 0x1c, &state) == TG_GPU_POWER_OK);
        model.fail_clamp_set = failure == 0;
        model.fail_reset_set = failure == 1;
        model.unreadable_mc_status = failure == 2;
        assert(tg_gpu_power_down(&io, &state) ==
               (failure == 2 ? TG_GPU_POWER_MC_TIMEOUT : TG_GPU_POWER_RESET_ERROR));
        assert(state.owned && (model.flush & 4) && (model.gpio6 & 8));
        model.fail_clamp_set = model.fail_reset_set = model.unreadable_mc_status = false;
        assert(tg_gpu_power_down(&io, &state) == TG_GPU_POWER_OK && !state.owned);
        check_restored(&model, &initial);
        assert(model.flush == initial.flush);
    }
    puts("T210 power ordering and unwind tests passed (model, not hardware).");
    return 0;
}
