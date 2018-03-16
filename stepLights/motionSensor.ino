#include "common.h"

static const byte motionPin = 16;  /* blue wire */

void initMotionSensor() {
    pinMode(motionPin, INPUT);
    updateMotionTick1Sec();  // initial
}

void updateMotionTick1Sec() {
    static int initializationCountdown = 11;
    const bool currMotionDetected =
        state.overrideMotionPinCountdownSeconds > 0 ||
        (!getDisableMotionSensor() && digitalRead(motionPin) == HIGH);

    if (state.overrideMotionPinCountdownSeconds > 0) {
        --state.overrideMotionPinCountdownSeconds;
#ifdef DEBUG
            Serial.print("current overrideMotionPinCountdownSeconds: ");
            Serial.println(state.overrideMotionPinCountdownSeconds);
#endif
    }

    // during initializationCountdown, simply do nothing other than
    // mess with the lastChange counters
    if (initializationCountdown > 0) {
        state.motionInfo.lastChangedSec =
            state.motionInfo.lastChangedMin = --initializationCountdown;
        state.motionInfo.lastChangedHour = 255;
#ifdef DEBUG
            Serial.print("motion initializationCountdown: "); Serial.println(initializationCountdown);
#endif
        return;
    }

    // Note: NOT using getMotionSensorOperState(), to keep bit set to what we need.
    //       Factoring getDisableMotionSensor() in is already taken care when 
    //       currMotionDetected is intantiated.
    if (getMotionSensorState() == currMotionDetected) {
        if (++state.motionInfo.lastChangedSec > 59) {
            state.motionInfo.lastChangedSec = 0;
            if (++state.motionInfo.lastChangedMin > 59) {
                state.motionInfo.lastChangedMin = 0;
                if (state.motionInfo.lastChangedHour < 254) ++state.motionInfo.lastChangedHour;
            }
        }
    } else {
        memset(&state.motionInfo, 0, sizeof(state.motionInfo));
        toggleMotionSensor();
        debugPrintMotionSensor();
    }
}

void debugPrintMotionSensor() {
#ifdef DEBUG
  Serial.print("Motion: ");
  Serial.print(getMotionSensorState() ? "y " : "n "); Serial.print(getDisableMotionSensor() ? "[DISABLED] " : "");
  Serial.print(state.motionInfo.lastChangedHour); Serial.print(":");
  Serial.print(state.motionInfo.lastChangedMin); Serial.print(":");
  Serial.print(state.motionInfo.lastChangedSec);
  Serial.println("");
#endif  // #ifdef DEBUG
}
