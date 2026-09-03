#include "AudioPower.h"

#include "logging.h"

#include <Arduino.h>
#include <Wire.h>
#include <Audio.h>

// ---------------------------------------------------------------------------
// SGTL5000 register addresses (16-bit, I2C addr 0x0A).
// Kept local here so we don't pull in private library defines.
// ---------------------------------------------------------------------------
#define SGTL_I2C_ADDR 0x0A

#define SGTL_CHIP_DIG_POWER   0x0002
#define SGTL_CHIP_CLK_CTRL    0x0004
#define SGTL_CHIP_I2S_CTRL    0x0006
#define SGTL_CHIP_SSS_CTRL    0x000A
#define SGTL_CHIP_ADCDAC_CTRL 0x000E
#define SGTL_CHIP_DAC_VOL     0x0010
#define SGTL_CHIP_ANA_HP_CTRL 0x0022
#define SGTL_CHIP_ANA_CTRL    0x0024
#define SGTL_CHIP_LINREG_CTRL 0x0026
#define SGTL_CHIP_REF_CTRL    0x0028
#define SGTL_CHIP_LINE_OUT_CTRL 0x002C
#define SGTL_CHIP_LINE_OUT_VOL  0x002E
#define SGTL_CHIP_ANA_POWER   0x0030
#define SGTL_CHIP_SHORT_CTRL  0x003C

static bool sgtl_write(unsigned int reg, unsigned int val)
{
    Wire.beginTransmission(SGTL_I2C_ADDR);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(val >> 8);
    Wire.write(val & 0xFF);
    return Wire.endTransmission() == 0;
}

// ---------------------------------------------------------------------------
// Power off: stop the I2S (SAI1) receiver, gate its clock, power down
// the SGTL5000 codec via I2C.
// ---------------------------------------------------------------------------
bool AudioPower::off()
{
    DEBUG("AudioPower: disabling audio chain")

    bool ok = true;

    // 1. Stop the SAI1 receiver.  Clearing RCSR stops it from generating
    //    DMA requests, which idles the existing DMA channel (its TCD is
    //    untouched and will resume later).
    I2S1_RCSR = 0;

    // 2. Gate the SAI1 peripheral clock to stop the PLL divider / MCLK
    //    output.  This saves the clock tree power draw.
    CCM_CCGR5 &= ~(((uint32_t)3) << 18);

    // 3. Power down the SGTL5000: analog block (keep only the bandgap
    //    reference rail so we can I2C back in) and all digital blocks.
    if (!sgtl_write(SGTL_CHIP_ANA_POWER, 0x4000))
    {
        ERROR("AudioPower: failed to power down SGTL5000 analog")
        ok = false;
    }
    if (!sgtl_write(SGTL_CHIP_DIG_POWER, 0x0000))
    {
        ERROR("AudioPower: failed to power down SGTL5000 digital")
        ok = false;
    }

    DEBUG("AudioPower: audio chain disabled")
    return ok;
}

// ---------------------------------------------------------------------------
// Power on: ungate the SAI1 clock, re-initialise the SGTL5000 (the
// power-down resets its analog blocks so registers must be replayed),
// then restart the SAI1 receiver.  The DMA TCD is still set up from
// AudioInputI2S::begin() and resumes automatically.
// ---------------------------------------------------------------------------
bool AudioPower::on()
{
    DEBUG("AudioPower: enabling audio chain")

    bool ok = true;

    // 1. Ungate the SAI1 peripheral clock.
    CCM_CCGR5 |= (((uint32_t)3) << 18);

    // 2. Replay the SGTL5000 slave-mode enable sequence (matches
    //    AudioControlSGTL5000::enable() for the non-extMCLK path).
    //    Values taken directly from control_sgtl5000.cpp.

    // Initial power-up with VDDD externally driven at 1.8 V.
    sgtl_write(SGTL_CHIP_ANA_POWER, 0x4060);
    sgtl_write(SGTL_CHIP_LINREG_CTRL, 0x006C);
    sgtl_write(SGTL_CHIP_REF_CTRL, 0x01F2);
    sgtl_write(SGTL_CHIP_LINE_OUT_CTRL, 0x0F22);
    sgtl_write(SGTL_CHIP_SHORT_CTRL, 0x4446);
    sgtl_write(SGTL_CHIP_ANA_CTRL, 0x0137);

    // Full analog power-up (slave mode: no PLL, no VCO).
    sgtl_write(SGTL_CHIP_ANA_POWER, 0x40FF);
    // Digital power-up.
    if (!sgtl_write(SGTL_CHIP_DIG_POWER, 0x0073))
    {
        ERROR("AudioPower: failed to power up SGTL5000 digital")
        ok = false;
    }

    // Wait for internal analog settle (SGTL5000 datasheet: min 400 ms).
    delay(400);

    sgtl_write(SGTL_CHIP_LINE_OUT_VOL, 0x1D1D);
    sgtl_write(SGTL_CHIP_CLK_CTRL, 0x0004);     // 44.1 kHz, 256*Fs
    sgtl_write(SGTL_CHIP_I2S_CTRL, 0x0030);     // SCLK=64*Fs, 16-bit, I2S
    sgtl_write(SGTL_CHIP_SSS_CTRL, 0x0010);     // ADC->I2S, I2S->DAC
    sgtl_write(SGTL_CHIP_ADCDAC_CTRL, 0x0000);  // DAC unmuted
    sgtl_write(SGTL_CHIP_DAC_VOL, 0x3C3C);      // 0 dB
    sgtl_write(SGTL_CHIP_ANA_HP_CTRL, 0x7F7F);  // headphone min
    sgtl_write(SGTL_CHIP_ANA_CTRL, 0x0036);     // zero-cross detectors

    // 3. Restart the SAI1 receiver.  The DMA TCD configured during
    //    AudioInputI2S::begin() is still intact; setting RE/BCE/FRDE
    //    resumes it.
    I2S1_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE;

    DEBUG("AudioPower: audio chain enabled")
    return ok;
}