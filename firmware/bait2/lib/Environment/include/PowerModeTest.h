#ifndef BAIT2_POWERMODETEST_H
#define BAIT2_POWERMODETEST_H

#include "PowerSensor.h"

#include <SdFat.h>

enum PowerMode
{
    PM_BASELINE = 0,
    PM_AUDIO_OFF,
    PM_MODE_COUNT,
    PM_IDLE
};

class PowerModeTest
{
public:
    // settleSec - time (s) to let regulators/current stabilise after switching mode.
    // logSec    - time (s) of fuel gauge sampling after settling.
    // sampleMs  - interval (ms) between fuel gauge readings during logging.
    PowerModeTest(PowerSensor &power, const char *filepath,
                  unsigned settleSec = 60, unsigned logSec = 120, unsigned sampleMs = 1000);

    void start();
    void stop();
    void loop();

    bool isRunning() const { return m_running; }
    const char *modeLabel() const;

private:
    enum State
    {
        IDLE,
        SET_MODE,
        SETTLE,
        LOG
    };

    void enterPowerMode(PowerMode mode);
    void exitPowerMode(PowerMode mode);
    void logSample();
    void advance();

    PowerSensor &m_power;
    const char *m_filepath;

    unsigned m_settleSec;
    unsigned m_logSec;
    unsigned m_sampleMs;

    PowerMode m_mode = PM_IDLE;
    State m_state = IDLE;

    bool m_running = false;
    unsigned long m_stateStartMs = 0;
    unsigned long m_lastSampleMs = 0;
};

#endif // BAIT2_POWERMODETEST_H