#ifndef _COMMON_H

#define _COMMON_H

#include <inttypes.h>
#include <Arduino.h>

// TODO FIXME: turn debug off
//#define DEBUG 1
//#define NO_MOTION_SENSOR 1

class TickerScheduler;

// // FWD decls... adminFlags
void initAdminFlags(TickerScheduler &ts);
void setFlags(uint8_t flags);
bool getFlag(int flagBit);
bool setFlag(int flagBit);
bool clearFlag(int flagBit);
bool flipFlag(int flagBit);

typedef enum {
    adminFlagMotionDetected = 0,    // oper
    adminFlagDisableMotionSensor,   // admin
    adminFlagCrazyLed,              // admin
    adminFlagCount = 8
} AdminFlag;

// Wrapper to flag functions above
bool getMotionSensorState();  // also see getMotionSensorOperState()
void setMotionSensorOn();
void clearMotionSensorOn();
void toggleMotionSensor();

// void checkDisableMotionSensorBlink();  // blink indicator when motion sensor is disabled
bool getDisableMotionSensor();  // also see getMotionSensorOperState()
void setDisableMotionSensor();
void clearDisableMotionSensor();
void toggleDisableMotionSensor();

inline bool getMotionSensorOperState() { return getMotionSensorState() && !getDisableMotionSensor(); }

bool getCrazyLedState();
void setCrazyLedOn();
void clearCrazyLedOn();
void toggleCrazyLed();

// FWD decls... motionSensor
struct MotionInfo {
  uint8_t lastChangedSec;     // 0 to 60
  uint8_t lastChangedMin;     // 0 to 60
  uint8_t lastChangedHour;    // 0 to 255
};
void initMotionSensor(TickerScheduler &ts);
void updateMotionTick1Sec();

// FWD decls... lights
void initLights(TickerScheduler &ts);

void parseSetLightModeMessage(const char* message);
const char* getLightOperInfo();

// FWD decls...heartBeat
void initHeartBeat(TickerScheduler &ts);

// FWS decls... mqtt_client
void initMyMqtt(TickerScheduler &ts);
void myMqttLoop();
void sendOperState();
void sendOperLightMode();

typedef struct {
    bool initIsDone;

    uint8_t flags;   // see AdminFlag enum
    uint32_t lightModeChanges;  // see lights.ino

    // This counter will fake a motion pin read to be HIGH
    int overrideMotionPinCountdownSeconds;

    MotionInfo motionInfo;
} State;

extern State state;

#endif // _COMMON_H
