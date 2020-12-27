#include "common.h"
#include "tickerScheduler.h"

// evernote uuid: 839B22A0-3FDE-4C65-8B75-DCE6BA1788D8
// Board: Adafruit HUZZAH ESP8266
// programming speed 921600
// serial speed: Serial.begin(115200);

TickerScheduler ts;
State state;

// ----------------------------------------

void initGlobals() {
    memset(&state, 0, sizeof(state));

    state.initIsDone = false;
}

void  initSpecialPin() {
    // Just for the fun of it, and I think this is really stupid,
    // I connected pin 14 to ground. Let's make sure it is used
    // as input and it should always be LOW.
    pinMode(14, INPUT);
}

void setup() {
#ifdef DEBUG
    Serial.begin(115200); // (921600);
#endif

    // stage 1
    initGlobals();

    // stage 2
    initSpecialPin();
    initAdminFlags(ts);   // should be earlier for led status
    initHeartBeat(ts);

    // stage 3
    initMotionSensor(ts);
    initLights(ts);
    initMyMqtt(ts);

#ifdef DEBUG
    Serial.println("Init finished");
    Serial.print("sizeof(int) == "); Serial.println(sizeof(int), DEC);
    Serial.print("sizeof(unsigned long) == "); Serial.println(sizeof(unsigned long), DEC);
    Serial.print("sizeof(unsigned long long) == "); Serial.println(sizeof(unsigned long long), DEC);
#endif
    state.initIsDone = true;
}

void loop() {
    ts.update();
    myMqttLoop();
}
