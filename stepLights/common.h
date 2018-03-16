#ifndef _COMMON_H

#define _COMMON_H

#include <inttypes.h>

// FIXME: turn debug off
// #define DEBUG 1

// // FWD decls... adminFlags
void initAdminFlags();
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

void checkDisableMotionSensorBlink();  // blink indicator when motion sensor is disabled
bool getDisableMotionSensor();  // also see getMotionSensorOperState()
void setDisableMotionSensor();
void clearDisableMotionSensor();
void toggleDisableMotionSensor();

bool getMotionSensorOperState() { return getMotionSensorState() && !getDisableMotionSensor(); }

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
void initMotionSensor();
void updateMotionTick1Sec();
void debugPrintMotionSensor();



// FWD decls... lights
void initLights();
void lightsFastTick(); // 100ms
void lights1SecTick();
void lights5SecTick();
void lights10SecTick();
void lights1MinTick();
void lights5MinTick();

void parseSetLightModeMessage(const char* message);
const char* getLightOperInfo();

// FWD decls...heartBeat
void initHeartBeat();
void heartBeatTick();

// FWS decls... mqtt_client
void initMyMqtt();
void myMqttLoop();
void mqtt1SecTick();
void mqtt10MinTick();
void sendOperState();
void sendOperLightMode();

typedef struct {
    bool initIsDone;

    uint8_t flags;   // see AdminFlag enum
    uint32_t lightModeChanges;  // see lights.ino

    // This counter will fake a motion pin read to be HIGH
    int overrideMotionPinCountdownSeconds;

    MotionInfo motionInfo;
  
    unsigned long next100time;    // 250 milliseconds timer
    unsigned long next250time;    // 250 milliseconds timer
    unsigned long next500time;    // 500 milliseconds timer
    unsigned long next1000time;   // 1 second timer
    unsigned long next5000time;   // 5 second timer
    unsigned long next10000time;  // 10 second timer
    unsigned long next25000time;  // 25 second timer
    unsigned long next1mintime;   // 1 min timer
    unsigned long next5mintime;   // 5 min timer
    unsigned long next10mintime;  // 10 min timer
} State;

extern State state;

#endif // _COMMON_H
