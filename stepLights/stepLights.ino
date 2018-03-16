#include "common.h"

// evernote uuid: 839B22A0-3FDE-4C65-8B75-DCE6BA1788D8
// Board: Adafruit HUZZAH ESP8266
// programming speed 921600
// serial speed: Serial.begin(115200);

// FWDs
void updateNextTime(unsigned long *nextTime, unsigned long increment);

State state;

// ----------------------------------------

void initGlobals() {
    memset(&state, 0, sizeof(state));

    state.initIsDone = false;

    updateNextTime(&state.next100time, 100);
    updateNextTime(&state.next250time, 250);
    updateNextTime(&state.next500time, 500);
    updateNextTime(&state.next1000time, 1000);
    updateNextTime(&state.next5000time, 5000);
    updateNextTime(&state.next10000time, 10000);
    updateNextTime(&state.next25000time, 25000);
    updateNextTime(&state.next1mintime, 60000);
    updateNextTime(&state.next5mintime, (60000 * 5));
    updateNextTime(&state.next10mintime, (60000 * 10));
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
    initAdminFlags();   // should be earlier for led status
    initHeartBeat();

    // stage 3
    initMotionSensor();
    initLights();
    initMyMqtt();

#ifdef DEBUG
    Serial.println("Init finished");
    Serial.print("sizeof(int) == "); Serial.println(sizeof(int), DEC);
    Serial.print("sizeof(unsigned long) == "); Serial.println(sizeof(unsigned long), DEC);
    Serial.print("sizeof(unsigned long long) == "); Serial.println(sizeof(unsigned long long), DEC);
#endif
    state.initIsDone = true;

}

void loop() {
    const unsigned long now = millis();

    while (true) {
        myMqttLoop();

        // check it it is time to do something interesting. Note that now will wrap and we will
        // get a 'blib' every 50 days. Who cares, right?
        //
        if (state.next100time <= now) {
            heartBeatTick();
            lightsFastTick();

            updateNextTime(&state.next100time, 100); continue;
        } else if (state.next250time <= now) {
            checkDisableMotionSensorBlink();

            updateNextTime(&state.next250time, 250); continue;
        } else if (state.next500time <= now) {
            updateNextTime(&state.next500time, 500); continue;
        } else if (state.next1000time <= now) {   // 1 sec
            mqtt1SecTick();
            updateMotionTick1Sec();
            lights1SecTick();
            // debugPrintMotionSensor();

            updateNextTime(&state.next1000time, 1000); continue;
        } else if (state.next5000time <= now) {   // 5 secs
            lights5SecTick();

            updateNextTime(&state.next5000time, 5000); continue;
        } else if (state.next10000time <= now) {  // 10 secs
            lights10SecTick();

            updateNextTime(&state.next10000time, 10000); continue;
        } else if (state.next25000time <= now) {  // 25 seconds
            updateNextTime(&state.next25000time, 25000); continue;
        } else if (state.next1mintime <= now) {  // 1 min
            lights1MinTick();

            updateNextTime(&state.next1mintime, 60000); continue;
        } else if (state.next5mintime <= now) {  // 5 mins
            lights5MinTick();

            updateNextTime(&state.next5mintime, (60000 * 5)); continue;
        } else if (state.next10mintime <= now) {  // 10 mins
            mqtt10MinTick();

            updateNextTime(&state.next10mintime, (60000 * 10)); continue;
        }

        break;
    } // while
}

// -----

void updateNextTime(unsigned long *nextTimePtr, unsigned long increment) {
    unsigned long nextTime;

    while (true) {
        const unsigned long now = millis();
        nextTime = now + increment;

        // handle wrap by sleeping
        if (nextTime < now) {
#ifdef DEBUG
            Serial.print("updateNextTime hit wrap");
#endif
            delay(1000);
            continue;
        }

        break;
    }
    *nextTimePtr = nextTime;
}

