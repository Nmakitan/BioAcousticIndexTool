#include "PowerModeTest.h"
#include "AudioPower.h"

#include "logging.h"

#include <SdFat.h>
#include <TimeLib.h>

extern SdFs sd;

PowerModeTest::PowerModeTest(PowerSensor &power, const char *filepath,
                             unsigned settleSec, unsigned logSec, unsigned sampleMs)
    : m_power(power), m_filepath(filepath),
      m_settleSec(settleSec), m_logSec(logSec), m_sampleMs(sampleMs)
{
}

const char *PowerModeTest::modeLabel() const
{
    switch (m_mode)
    {
    case PM_BASELINE:
        return "baseline";
    case PM_AUDIO_OFF:
        return "audio_off";
    default:
        return "idle";
    }
}

void PowerModeTest::start()
{
    DEBUG("PowerModeTest: starting run")
    if (m_running)
        return;

    // Open the log file and write the header if this is a fresh file.
    FsFile f;
    if (f.open(m_filepath, O_WRITE | O_CREAT))
    {
        if (f.size() == 0)
        {
            f.println("timestamp, mode, volts_mV, current_mA, power_mW, soc_pct, capacity_mAh");
        }
        f.close();
    }
    else
    {
        ERROR("PowerModeTest: cannot open log file")
    }

    m_running = true;
    m_mode = (PowerMode)0;
    m_state = SET_MODE;
    m_stateStartMs = millis();
}

void PowerModeTest::stop()
{
    DEBUG("PowerModeTest: stopping run")
    if (!m_running)
        return;

    exitPowerMode(m_mode);
    m_mode = PM_IDLE;
    m_state = IDLE;
    m_running = false;
}

void PowerModeTest::enterPowerMode(PowerMode mode)
{
    DEBUG("PowerModeTest: entering mode '%s'", modeLabel())
    switch (mode)
    {
    case PM_BASELINE:
        AudioPower::on();
        break;
    case PM_AUDIO_OFF:
        AudioPower::off();
        break;
    default:
        break;
    }
}

void PowerModeTest::exitPowerMode(PowerMode mode)
{
    DEBUG("PowerModeTest: exiting mode '%s'", modeLabel())
    switch (mode)
    {
    case PM_AUDIO_OFF:
        // Restore the audio chain so subsequent modes start from a known state.
        AudioPower::on();
        break;
    default:
        break;
    }
}

void PowerModeTest::logSample()
{
    // Re-read the fuel gauge so values are fresh for this sampling tick.
    m_power.process();

    FsFile f;
    if (!f.open(m_filepath, O_WRITE | O_AT_END))
    {
        DEBUG("PowerModeTest: error opening log file")
        return;
    }

    f.printf("%d-%02d-%02d %02d:%02d:%02d, ", year(), month(), day(), hour(), minute(), second());
    f.printf("%s, %d, %d, %d, %d, %d",
             modeLabel(),
             (int)m_power.volts, (int)m_power.current, (int)m_power.power,
             (int)m_power.soc, (int)m_power.capacity);
    f.println();
    f.close();
}

void PowerModeTest::advance()
{
    DEBUG("PowerModeTest: advancing past mode '%s'", modeLabel())

    // Restore prior mode's peripheral state before switching.
    exitPowerMode(m_mode);

    PowerMode next = (PowerMode)((int)m_mode + 1);
    if (next < PM_MODE_COUNT)
    {
        m_mode = next;
        m_state = SET_MODE;
        m_stateStartMs = millis();
    }
    else
    {
        // Run complete.
        DEBUG("PowerModeTest: run complete")
        m_mode = PM_IDLE;
        m_state = IDLE;
        m_running = false;
    }
}

void PowerModeTest::loop()
{
    if (!m_running)
        return;

    unsigned long nowMs = millis();

    switch (m_state)
    {
    case SET_MODE:
        enterPowerMode(m_mode);
        m_state = SETTLE;
        m_stateStartMs = nowMs;
        break;

    case SETTLE:
        if (nowMs - m_stateStartMs >= (unsigned long)m_settleSec * 1000UL)
        {
            DEBUG("PowerModeTest: starting logging window for '%s'", modeLabel())
            m_lastSampleMs = nowMs;
            m_state = LOG;
            m_stateStartMs = nowMs;
        }
        break;

    case LOG:
        if (nowMs - m_stateStartMs >= (unsigned long)m_logSec * 1000UL)
        {
            advance();
        }
        else if (nowMs - m_lastSampleMs >= m_sampleMs)
        {
            m_lastSampleMs = nowMs;
            logSample();
        }
        break;

    default:
        break;
    }
}