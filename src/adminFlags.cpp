#include "common.h"
#include "tickerScheduler.h"

// MOTION_LED is connected to pin 15
//
#define PIN_MOTION_LED   15 /* green wire */
#define PIN_MOTION_LED_OFF LOW
#define PIN_MOTION_LED_ON  HIGH

#define PIN_CRAZY_LED  12 /* orange wire */

void refreshFlags() {
#ifdef DEBUG
  Serial.print("refreshFlags flags: "); Serial.println((int) state.flags, DEC);
#endif

  digitalWrite(PIN_MOTION_LED,
               bitRead(state.flags, adminFlagMotionDetected) == 0 ? PIN_MOTION_LED_OFF : PIN_MOTION_LED_ON);
  digitalWrite(PIN_CRAZY_LED,
               bitRead(state.flags, adminFlagCrazyLed) == 0 ? LOW : HIGH);
}

static void checkDisableMotionSensorBlink() {
    static bool blinkState = false;
    // blink indicator when motion sensor is disabled
    if (getDisableMotionSensor()) {
        digitalWrite(PIN_MOTION_LED, blinkState ? HIGH : LOW);
        blinkState = !blinkState;
    }
}

void initAdminFlags(TickerScheduler &ts) {
  pinMode(PIN_MOTION_LED, OUTPUT); digitalWrite(PIN_MOTION_LED, PIN_MOTION_LED_OFF);
  pinMode(PIN_CRAZY_LED, OUTPUT); digitalWrite(PIN_CRAZY_LED, LOW);

  setFlags(0);  // power all off by default  (clear bit means power off)

  ts.sched(checkDisableMotionSensorBlink, 250);
}

void setFlags(uint8_t flags) {
  if (state.flags == flags) return;
  state.flags = flags;
  refreshFlags();
}

bool getFlag(int flagBit) {
  if (flagBit < 0 || flagBit > 7) return false;
  return bitRead(state.flags, flagBit) != 0;
}

bool setFlag(int flagBit) {
  const uint8_t origFlags = state.flags;
  if (flagBit < 0 || flagBit > 7) return false;
  bitSet(state.flags, flagBit);
  if (origFlags != state.flags) { refreshFlags(); return true; }
  return false;
}

bool clearFlag(int flagBit) {
  const uint8_t origFlags = state.flags;
  if (flagBit < 0 || flagBit > 7) return false;
  bitClear(state.flags, flagBit);
  if (origFlags != state.flags) { refreshFlags(); return true; }
  return false;
}

bool flipFlag(int flagBit) {
  if (flagBit < 0 || flagBit > 7) return false;
  const bool currBit = bitRead(state.flags, flagBit) == 1;
  bitWrite(state.flags, flagBit, !currBit);
  refreshFlags();
  return true;
}

bool getMotionSensorState() { return getFlag(adminFlagMotionDetected); }
void setMotionSensorOn() { setFlag(adminFlagMotionDetected); }
void clearMotionSensorOn() { clearFlag(adminFlagMotionDetected); }
void toggleMotionSensor() { flipFlag(adminFlagMotionDetected); }

bool getDisableMotionSensor() { return getFlag(adminFlagDisableMotionSensor); }
void setDisableMotionSensor() { setFlag(adminFlagDisableMotionSensor); }
void clearDisableMotionSensor() { clearFlag(adminFlagDisableMotionSensor); }
void toggleDisableMotionSensor() { flipFlag(adminFlagDisableMotionSensor); }

bool getCrazyLedState() { return getFlag(adminFlagCrazyLed); }
void setCrazyLedOn() { setFlag(adminFlagCrazyLed); }
void clearCrazyLedOn() { clearFlag(adminFlagCrazyLed); }
void toggleCrazyLed() { flipFlag(adminFlagCrazyLed); }

