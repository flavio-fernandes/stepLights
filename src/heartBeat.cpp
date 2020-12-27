#include "common.h"
#include "tickerScheduler.h"

// Use Huzzah 8266 onboard LEDs
// Blue light next to the antena of the ESP8266 is GPIO 2
// Red light is connected to GPIO 0
//
static const byte heartBeatPin = 0;

static void heartBeatTick() {
    static int hbValue = 0;
#if 0
    // TODO: analogWrite is causing noise with the neopixels... let's keep it
    //       simple instead of dealing with it!
    //
    static const int hbIncrementScale = 15;
    static int hbIncrement = hbIncrementScale;

    hbValue += hbIncrement;

    if (hbValue >= 256) {
        hbValue = 255; hbIncrement = hbIncrementScale * -1;
    } else if (hbValue <= -1) {
        hbValue = 0; hbIncrement = hbIncrementScale;
    }
    analogWrite(heartBeatPin, hbValue);
#else // #if 0
    ++hbValue;
    digitalWrite(heartBeatPin, hbValue < 2 ? LOW : HIGH);
    hbValue &= B00011111;  // https://www.arduino.cc/reference/en/language/structure/bitwise-operators/
#endif // #if 0
}

void initHeartBeat(TickerScheduler &ts) {
    pinMode(heartBeatPin, OUTPUT);
    digitalWrite(heartBeatPin, LOW);
    ts.sched(heartBeatTick, 100);
}
