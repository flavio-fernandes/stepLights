#include "common.h"
#include <map>

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

typedef std::map<String, String> StringMap;

#define STRIP1_PIN 5  /* white wire */
#define STRIP2_PIN 4  /* grey wire */
#define STRIP3_PIN 2  /* purple wire */

#define STRIP_CNT 3
#define LEDS_PER_STRIP 20

static const int pixelCount = STRIP_CNT * LEDS_PER_STRIP;
static const uint8_t maxPixelValue = 255;

// ----------------------------------------------------------------------

// Local types

typedef enum LedStripMode_t {
    ledStripModeOff = 0,
    ledStripModeStair,
    ledStripModeFill,
    ledStripModeRainbow,
    ledStripModeScan,
    ledStripModeBlink,
    ledStripModeBinaryCounter,
    ledStripModeDot,
    ledStripModeUnknown // last one
} LedStripMode;

const char* const ledStripModeOffStr = "off";
const char* const ledStripModeStairStr = "stair";
const char* const ledStripModeFillStr = "fill";
const char* const ledStripModeRainbowStr = "rainbow";
const char* const ledStripModeScanStr = "scan";
const char* const ledStripModeBlinkStr = "blink";
const char* const ledStripModeBinaryCounterStr = "count";
const char* const ledStripModeDotStr = "dot";

const char* const ledStripParamTimeout = "timeout";
const char* const ledStripParamClearAllPixels = "clearPixels";

// the values below can be overriden in global
static const int defaultTimeoutInSeconds = 600;  // seconds (10 mins)
static const uint8_t defaultMaxBrightness = 193;  // 0=dimmest, 255=max

typedef struct {
    int currModeIndex;
    int secsUntilModeExpires;  // -1 = never. If not provided, uses globalParams['defaultTimeout'] or defaultTimeoutInSeconds 
    StringMap globalParams;
    StringMap params;
} Info;

static uint32_t wheel(uint16_t wheelPos);
static uint8_t getMaxBrightness();
static void clearPixelColorsAndShow();
static void checkIfModeTimedOut();
static void changeLedStripMode(LedStripMode wantedLedStripMode, const StringMap& params);
static void changeLedStripMode(LedStripMode wantedLedStripMode);
static uint32_t getPixelColorParam(const StringMap& params);

bool parseBooleanValue(const char* valueStr);
bool isParamSet(const StringMap& params, const char* const paramName, const char* const paramValue);
bool isParamSet(const StringMap& params, const char* const paramName);
bool getParamValue(const StringMap& params, const char* const paramName, String& paramValueFound);
bool getValidParamValue(const StringMap& params, const char* const paramName, String& paramValueFound);
bool getBoolParam(const StringMap& params, const char* const paramName);
int getToIntParam(const StringMap& params, const char* const paramName, int defaultValue);
int getToIntParam(const StringMap& params, const char* const paramName);

typedef void (*modeFunctionPtr)();

typedef struct {
    LedStripMode ledStripMode;
    const char* const ledStripModeStr;
    modeFunctionPtr handlerInit;
    modeFunctionPtr handlerTickFast;  // 100ms
    modeFunctionPtr handlerTick1Sec;
    modeFunctionPtr handlerTick5Sec;
    modeFunctionPtr handlerTick10Sec;
    modeFunctionPtr handlerTick1Min;
    modeFunctionPtr handlerTick5Min;
} Mode;

// ----------------------------------------------------------------------

static Info info;

Adafruit_NeoPixel strip1 = Adafruit_NeoPixel(LEDS_PER_STRIP, STRIP1_PIN, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel strip2 = Adafruit_NeoPixel(LEDS_PER_STRIP, STRIP2_PIN, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel strip3 = Adafruit_NeoPixel(LEDS_PER_STRIP, STRIP3_PIN, NEO_RGB + NEO_KHZ800);
static Adafruit_NeoPixel* strips[] = { &strip1, &strip2, &strip3 };

static void modeOffInit() {
    // This mode does not expire
    info.secsUntilModeExpires = -1;
}

static void modeOff1Sec() {
    if (!getMotionSensorOperState()) return;

    // motion detected, jump to stairs mode
    StringMap params;
    params["_from_motion"] = "yes";
    changeLedStripMode(ledStripModeStair, params);
}

static const Mode modeOff = { ledStripModeOff, ledStripModeOffStr, modeOffInit /*init*/, 0 /*fast*/, 
                              modeOff1Sec /*1sec*/, 0 /*5sec*/, 0 /*10sec*/, 0 /*1min*/, 0 /*5min*/};

// --

typedef struct {
    uint32_t color;
    uint32_t borderColor;
    bool borderNoAnimation;
    int motionExpirationMinutes;
    bool leavingAnimation;
    uint8_t leavingAnimationBrightness;
    int delayStartCountdown; // ticks till draw, decreased in 1 sec interval
} ModeStairState;

static ModeStairState modeStairState = {0};

static bool _modeStairFinished() {
    const int minutesWithNoMotion = state.motionInfo.lastChangedHour * 60 + state.motionInfo.lastChangedMin;
#ifdef DEBUG
    Serial.print("_modeStairFinished checking secs since motion: "); Serial.print(minutesWithNoMotion, DEC);
    Serial.print(" waiting till: "); Serial.println(modeStairState.motionExpirationMinutes, DEC);
#endif
    if (getMotionSensorOperState()) return false;
    if (minutesWithNoMotion < modeStairState.motionExpirationMinutes) return false;
    return true;
}

static bool _modeStairStripShow() {
    for (int s = 0; s < STRIP_CNT; ++s) {
        Adafruit_NeoPixel& strip(*strips[s]);

        // draw inner (non borders)
        for (uint16_t p=0; p < LEDS_PER_STRIP; ++p) strip.setPixelColor(p, modeStairState.color);

        // draw borders
        for (uint16_t p=0; p < 2; ++p) strip.setPixelColor(p, modeStairState.borderColor);
        for (uint16_t p=LEDS_PER_STRIP - 1; p >= LEDS_PER_STRIP - 2; --p) strip.setPixelColor(p, modeStairState.borderColor);

        strip.show();
    }
}

static void modeStairInit() {
    static const int delayStartTicks = 2; // 1 second ticks
    static const int defaultMotionExpirationMinutes = 3; // minutes
    static const uint32_t defaultColor = Adafruit_NeoPixel::Color(maxPixelValue, maxPixelValue, maxPixelValue);
    static const uint32_t defaultBorderColor = Adafruit_NeoPixel::Color(0, 0, maxPixelValue);  // blue

    modeStairState.color = getPixelColorParam(info.params);
    if (modeStairState.color == 0) {
        modeStairState.color = (uint32_t) getToIntParam(info.globalParams, "stairModeColor", defaultColor);
    }

    modeStairState.borderColor = isParamSet(info.params, "borderColor") ?
        (uint32_t) getToIntParam(info.params, "borderColor") :
        (uint32_t) getToIntParam(info.globalParams, "stairModeBorderColor", defaultBorderColor);

    modeStairState.borderNoAnimation = isParamSet(info.params, "borderNoAnimation") ?
        getBoolParam(info.params, "borderNoAnimation") :
        getBoolParam(info.globalParams, "stairModeBorderNoAnimation");

    modeStairState.motionExpirationMinutes = isParamSet(info.params, "motionExpirationMinutes") ?
        getToIntParam(info.params, "motionExpirationMinutes") :
        getToIntParam(info.globalParams, "stairModemotionExpirationMinutes", defaultMotionExpirationMinutes);

    // This mode does not expire from timeout. It expires from motionExpirationMinutes
    info.secsUntilModeExpires = -1;

    modeStairState.leavingAnimation = false;
    modeStairState.leavingAnimationBrightness = getMaxBrightness();

    // if we got here because a previous mode timed out, consider going straight to off mode
    // if there has been no motion
    if (getBoolParam(info.params, "_from_timeout") && _modeStairFinished()) {
        // go straight to off... there has been no motion already.
        changeLedStripMode(ledStripModeOff);
        return;
    }

    // if we got here because we detected motion, let's postpone any drawing on the strip
    // by a few seconds, so external agents can have first dibs on what they want the strips
    // to perform
    if (getBoolParam(info.params, "_from_motion")) {
        modeStairState.delayStartCountdown = delayStartTicks + 1; // ticks-1 till postponed draw
#ifdef DEBUG
        Serial.println("mode stair init delaying strip draw to give external agent some time to react");
#endif  // #ifdef DEBUG
    } else {
        // not postponed
        modeStairState.delayStartCountdown = 0;  // no ticks!
        _modeStairStripShow();
    }
}

static void modeStairFast() {
    static const uint8_t dimStep = 1;

    if (!modeStairState.leavingAnimation) return;
    if (modeStairState.delayStartCountdown != 0) return;


    if (modeStairState.leavingAnimationBrightness <= dimStep * 2) {
#ifdef DEBUG
        Serial.println("modeStair leavingAnimation is finished");
#endif
        // really end this mode now
        changeLedStripMode(ledStripModeOff);
        return;
    }

    // if motion is detected while leavingAnimation, cancel it
    if (getMotionSensorOperState()) {
#ifdef DEBUG
        Serial.println("modeStair leavingAnimation reverted");
#endif
        modeStairState.leavingAnimation = false;
        modeStairState.leavingAnimationBrightness = getMaxBrightness();
        _modeStairStripShow();
    } else {
        modeStairState.leavingAnimationBrightness -= dimStep;
    }

    for (int s = 0; s < STRIP_CNT; ++s) {
        Adafruit_NeoPixel& strip(*strips[s]);
        strip.setBrightness(modeStairState.leavingAnimationBrightness);
        strip.show();
    }
}

static void modeStair1Sec() {
    static bool flipAnimation;

    if (modeStairState.borderNoAnimation) return;
    if (modeStairState.leavingAnimation) return;

    // decrease ticks for delayed start. Use 1 as the value where
    // draw finaly happens, and set ticks to 0
    if (modeStairState.delayStartCountdown > 1) {
        --modeStairState.delayStartCountdown;
#ifdef DEBUG
        Serial.print("modeStair delaying start tick "); Serial.println(modeStairState.delayStartCountdown, DEC);
#endif
        return;
    } else if (modeStairState.delayStartCountdown == 1) {
        modeStairState.delayStartCountdown = 0;  // no ticks!
        _modeStairStripShow();
        // fall through...
    }

    // border animation
    for (int s = 0; s < STRIP_CNT; ++s) {
        Adafruit_NeoPixel& strip(*strips[s]);
        for (uint16_t p=0; p < 2; ++p) {
            strip.setPixelColor(p,
                                flipAnimation ? modeStairState.borderColor : modeStairState.color);
            flipAnimation = !flipAnimation;
        }
        for (uint16_t p=LEDS_PER_STRIP - 1; p >= LEDS_PER_STRIP - 2; --p) {
            strip.setPixelColor(p,
                                flipAnimation ? modeStairState.borderColor : modeStairState.color);
            flipAnimation = !flipAnimation;
        }
        strip.show();
    }

    flipAnimation = !flipAnimation;
}

static void modeStair5Sec() {
    if (modeStairState.leavingAnimation) return;
    if (modeStairState.delayStartCountdown != 0) return;

    if (_modeStairFinished()) {
#ifdef DEBUG
        Serial.println("modeStair leavingAnimation starting");
#endif
        // no motion for the number of specified minutes... time to end this mode
        modeStairState.leavingAnimation = true;
        _modeStairStripShow();
    }
}

static const Mode modeStair = { ledStripModeStair, ledStripModeStairStr, modeStairInit /*init*/, 
                                modeStairFast /*fast*/, modeStair1Sec /*1sec*/, modeStair5Sec /*5sec*/, 
                                0 /*10sec*/, 0 /*1min*/, 0 /*5min*/};

// --

typedef struct {
    bool randomColor;
    bool perStrip;
    bool perRefresh;
    bool fastRefresh;
    bool refresh1Sec;
    bool refresh5Sec;
    bool refresh10Sec;
    uint32_t color;
    int stripWanted;
} ModeFillState;

static ModeFillState modeFillState = {0};

static void _modeFillStripShow(int s) {
    Adafruit_NeoPixel& strip(*strips[s]);
    for (uint16_t p=0; p < LEDS_PER_STRIP; ++p) {
        strip.setPixelColor(p, modeFillState.color);
        if (modeFillState.randomColor && !modeFillState.perStrip && !modeFillState.perRefresh) {
            modeFillState.color = random(0x00fffffe) + 1;
        }
    }
    strip.show();
}

static void _modeFillShow() {
    if (modeFillState.stripWanted != ~0) {
        _modeFillStripShow(modeFillState.stripWanted);
    } else {
        for (int i = 0; i < STRIP_CNT; ++i) {
            _modeFillStripShow(i);
            if (modeFillState.randomColor && !modeFillState.perRefresh) {
                modeFillState.color = random(0x00fffffe) + 1;  // perStrip
            }
        }
    }
    // for next time...
    if (modeFillState.randomColor) {
        modeFillState.color = random(0x00fffffe) + 1;  // perRefresh
    }
}

static void modeFillInit() {
    // init modeFillState
    modeFillState.randomColor = getBoolParam(info.params, "randomColor");
    modeFillState.perStrip = getBoolParam(info.params, "perStrip");
    modeFillState.perRefresh = getBoolParam(info.params, "perRefresh");
    modeFillState.fastRefresh = getBoolParam(info.params, "fast");
    modeFillState.refresh1Sec = getBoolParam(info.params, "1sec");
    modeFillState.refresh5Sec = getBoolParam(info.params, "5sec");
    modeFillState.refresh10Sec = getBoolParam(info.params, "10sec");
    modeFillState.color = getPixelColorParam(info.params);
    if (modeFillState.color == 0) modeFillState.color = random(0x00fffffe) + 1;

    const int stripWanted = getToIntParam(info.params, "stripWanted", ~0);
    modeFillState.stripWanted = (stripWanted >= 0 && stripWanted < STRIP_CNT) ? stripWanted : ~0;

    _modeFillShow();
}

static void modeFillFast() { if (modeFillState.fastRefresh) _modeFillShow(); }
static void modeFill1Sec() { if (modeFillState.refresh1Sec) _modeFillShow(); }
static void modeFill5Sec() { if (modeFillState.refresh5Sec) _modeFillShow(); }
static void modeFill10Sec() { if (modeFillState.refresh10Sec) _modeFillShow(); }

static const Mode modeFill = { ledStripModeFill, ledStripModeFillStr, modeFillInit /*init*/, 
                               modeFillFast /*fast*/, modeFill1Sec /*1sec*/, modeFill5Sec /*5sec*/, 
                               modeFill10Sec /*10sec*/, 0 /*1min*/, 0 /*5min*/};

// --

typedef struct {
    bool fastRefresh;
    bool refresh1Sec;
    bool refresh10Sec;
} ModeRainbowState;

static ModeRainbowState modeRainbowState = {0};

static void modeRainbowStripShow() {
    static uint16_t j = 0;

    for (int s=0; s < STRIP_CNT; ++s) {
        Adafruit_NeoPixel& strip(*strips[s]);

        for (uint16_t i=0; i < LEDS_PER_STRIP; ++i) {
            // tricky math! we use each pixel as a fraction of the full 384-color
            // wheel (thats the i / lpd8806.numPixels() part)
            // Then add in j which makes the colors go around per pixel
            // the % 384 is to make the wheel cycle around
            strip.setPixelColor(i, wheel(((i * 384 / LEDS_PER_STRIP) + j) % 384));
        }
        strip.show();   // write all the pixels out
        if (++j >= 384) j = 0;
    }
}

static void modeRainbowInit() {
    modeRainbowState.fastRefresh = getBoolParam(info.params, "fast");
    modeRainbowState.refresh1Sec = getBoolParam(info.params, "1sec");
    modeRainbowState.refresh10Sec = getBoolParam(info.params, "10sec");

    modeRainbowStripShow();
}

static void modeRainbowFast() { if (modeRainbowState.fastRefresh) modeRainbowStripShow(); }
static void modeRainbow1sec() { if (modeRainbowState.refresh1Sec) modeRainbowStripShow(); }
static void modeRainbow10sec() { if (modeRainbowState.refresh10Sec) modeRainbowStripShow(); }

static const Mode modeRainbow = {ledStripModeRainbow, ledStripModeRainbowStr, modeRainbowInit /*init*/,
                                 modeRainbowFast, modeRainbow1sec, 0 /*5sec*/, modeRainbow10sec,
                                 modeRainbowStripShow /*1min*/, 0 /*5min*/};

typedef struct {
    bool randomColor;
    uint32_t color;
    int stripWanted;
    bool noWrap;
    int pixelIncrement;
    int currPixel;
} ModeScanState;

static ModeScanState modeScanState = {0};

static int _modeScanPreviousPixel(int pixel) {
    for (int i=0; i < abs(modeScanState.pixelIncrement); ++i) if (--pixel < 0) pixel = pixelCount - 1;
    return pixel;
}
static int _modeScanNextPixel(int pixel) {
    for (int i=0; i < abs(modeScanState.pixelIncrement); ++i) if (++pixel >= pixelCount) pixel = 0;
    return pixel;
}
static int _modeScanAddPixelIncrement(int pixel) {
    return modeScanState.pixelIncrement > 0 ? _modeScanNextPixel(pixel) : _modeScanPreviousPixel(pixel);
}

// 0-19 20-39 40-59
static int _modeScanGetStrip(int pixel) { return (pixel / LEDS_PER_STRIP) % STRIP_CNT; }
static int _modeScanGetPixelinStrip(int pixel) { return pixel % LEDS_PER_STRIP; }

static void modeScanInit() {
    modeScanState.randomColor = getBoolParam(info.params, "randomColor");
    modeScanState.color = getPixelColorParam(info.params);
    if (modeScanState.color == 0) modeScanState.color = random(0x00fffffe) + 1;

    const int stripWanted = getToIntParam(info.params, "stripWanted", ~0);
    modeScanState.stripWanted = (stripWanted >= 0 && stripWanted < STRIP_CNT) ? stripWanted : ~0;

    modeScanState.noWrap = getBoolParam(info.params, "noWrap");
    modeScanState.pixelIncrement = getToIntParam(info.params, "pixelIncrement", 1);
    if (modeScanState.pixelIncrement == 0) modeScanState.pixelIncrement = 1;  // increment must not be zero
    if (getBoolParam(info.params, ledStripParamClearAllPixels)) modeScanState.currPixel = 0;
}

static void modeScanFast() {
    const int prevPixel = modeScanState.currPixel;

    // clear existing pixel
    Adafruit_NeoPixel& prevStrip(*strips[ _modeScanGetStrip(prevPixel) ]);
    prevStrip.setPixelColor(_modeScanGetPixelinStrip(prevPixel), 0);

    int nextPixel = _modeScanAddPixelIncrement(prevPixel);
    if (modeScanState.stripWanted != ~0) {
        const int initialNextPixelValue = nextPixel;
        int reries = 0;
        while (_modeScanGetStrip(nextPixel) != modeScanState.stripWanted) {
            if (++reries > pixelCount) break;  // insanity
            nextPixel = _modeScanAddPixelIncrement(nextPixel);
            if (modeScanState.noWrap) {
                if (modeScanState.pixelIncrement > 0 && nextPixel < prevPixel) {
                    modeScanState.pixelIncrement *= -1;
                    nextPixel = _modeScanAddPixelIncrement(initialNextPixelValue);
                } else if (modeScanState.pixelIncrement < 0 && nextPixel > prevPixel) {
                    modeScanState.pixelIncrement *= -1; 
                    nextPixel = _modeScanAddPixelIncrement(initialNextPixelValue);
                }
            }
        }
    } else {
        if (modeScanState.noWrap) {
            if (modeScanState.pixelIncrement > 0 && nextPixel < prevPixel) {
                modeScanState.pixelIncrement *= -1;
                nextPixel = _modeScanAddPixelIncrement(prevPixel);
            } else if (modeScanState.pixelIncrement < 0 && nextPixel > prevPixel) {
                modeScanState.pixelIncrement *= -1; 
                nextPixel = _modeScanAddPixelIncrement(prevPixel);
            }
        }
    }

    // set next pixel
    Adafruit_NeoPixel& nextStrip(*strips[ _modeScanGetStrip(nextPixel) ]);
    nextStrip.setPixelColor(_modeScanGetPixelinStrip(nextPixel), modeScanState.color);

    if (&prevStrip != &nextStrip) prevStrip.show();    
    nextStrip.show();

    modeScanState.currPixel = nextPixel;
    if (modeScanState.randomColor) modeScanState.color = random(0x00fffffe) + 1;
}

static const Mode modeScan = {ledStripModeScan, ledStripModeScanStr, modeScanInit /*init*/,
                              modeScanFast /*fast*/, 0 /*1sec*/, 0 /*5sec*/, 0 /*10sec*/,
                              0 /*1min*/, 0 /*5min*/};

typedef struct {
    bool randomColor;
    bool perStrip;
    uint32_t color;
    int slowScale; // the higher, the slower the blink effect
    int stripIncrement;  // 0 same strip, 1 up animation, 2 down animation
    int currStrip;
    bool sameStripClearIteration;
} ModeBlinkState;

static ModeBlinkState modeBlinkState = {0};

static void _modeBlinkStripShow() {
    for (int s=0; s < STRIP_CNT; ++s) {
        Adafruit_NeoPixel& strip(*strips[s]);

        // check if this is the strip that needs to be drawn and it is not the one drawn in previous call
        if (s == modeBlinkState.currStrip && !modeBlinkState.sameStripClearIteration) {
            for (uint16_t p=0; p < LEDS_PER_STRIP; ++p) {
                strip.setPixelColor(p, modeBlinkState.color);
                if (modeBlinkState.randomColor && !modeBlinkState.perStrip) modeBlinkState.color = random(0x00fffffe) + 1;
            }
            // randomize color for next time this function gets called
            if (modeBlinkState.randomColor) modeBlinkState.color = random(0x00fffffe) + 1;
        } else {
            // if we made it here, the strip is meant to be all off
            strip.clear();
        }
        strip.show();   // write all the pixels out
    }

    // prepare for next iteration
    const int prevStrip = modeBlinkState.currStrip;
    modeBlinkState.currStrip = (prevStrip + modeBlinkState.stripIncrement) % STRIP_CNT;

    // toggle sameStripClearIteration if increment lands us on same strip
    if (prevStrip == modeBlinkState.currStrip) modeBlinkState.sameStripClearIteration = !modeBlinkState.sameStripClearIteration;
}

static void modeBlinkInit() {
    modeBlinkState.randomColor = getBoolParam(info.params, "randomColor");
    modeBlinkState.perStrip = getBoolParam(info.params, "perStrip");
    modeBlinkState.color = getPixelColorParam(info.params);
    if (modeBlinkState.color == 0) modeBlinkState.color = random(0x00fffffe) + 1;

    modeBlinkState.slowScale = abs(getToIntParam(info.params, "slowScale", 1));
    modeBlinkState.stripIncrement = abs(getToIntParam(info.params, "stripIncrement", 1));
    modeBlinkState.currStrip = abs(getToIntParam(info.params, "startStrip")) % STRIP_CNT;

    modeBlinkState.sameStripClearIteration = false;
}

static void modeBlinkFast() {
    static int slowScaleTicker = 0;

    // bump the ticker. If it is smaller than slowScale, simply return
    if (++slowScaleTicker <= modeBlinkState.slowScale) return;

    _modeBlinkStripShow();
    slowScaleTicker = 0;
}

static const Mode modeBlink = {ledStripModeBlink, ledStripModeBlinkStr, modeBlinkInit /*init*/,
                              modeBlinkFast /*fast*/, 0 /*1sec*/, 0 /*5sec*/, 0 /*10sec*/,
                              0 /*1min*/, 0 /*5min*/};

typedef struct {
    bool fast;
    bool randomColor;
    uint32_t color;
    uint32_t counter;
} ModeBinaryCounterState;

static ModeBinaryCounterState modeBinaryCounterState = {0};

static void _modeBinaryCounterStripShow() {
    for (int s = 0; s < STRIP_CNT; ++s) {
        Adafruit_NeoPixel& strip(*strips[s]);
        strip.clear();
        for (int i=0; i < 32 && i < LEDS_PER_STRIP; ++i) {
            if (bitRead(modeBinaryCounterState.counter, i)) {
                strip.setPixelColor(i, modeBinaryCounterState.color);
            }
        }
        strip.show();
    }
    if (modeBinaryCounterState.randomColor) modeBinaryCounterState.color = random(0x00fffffe) + 1;

    ++modeBinaryCounterState.counter;
}

static void modeBinaryCounterInit() {
    modeBinaryCounterState.fast = getBoolParam(info.params, "fast");
    modeBinaryCounterState.randomColor = getBoolParam(info.params, "randomColor");
    modeBinaryCounterState.color = getPixelColorParam(info.params);
    if (modeBinaryCounterState.color == 0) modeBinaryCounterState.color = random(0x00fffffe) + 1;

    if (isParamSet(info.params, "startValue")) {
        modeBinaryCounterState.counter = (uint32_t) getToIntParam(info.params, "startValue");
    } else if (getBoolParam(info.params, ledStripParamClearAllPixels)) {
        modeBinaryCounterState.counter = 0;
    }
}

static void modeBinaryCounterFast() {
    if (modeBinaryCounterState.fast) _modeBinaryCounterStripShow();
}

static void modeBinaryCounter1sec() {
    _modeBinaryCounterStripShow();
}

static const Mode modeBinaryCounter = {ledStripModeBinaryCounter, ledStripModeBinaryCounterStr, modeBinaryCounterInit /*init*/,
                                       modeBinaryCounterFast /*fast*/, modeBinaryCounter1sec /*1sec*/, 0 /*5sec*/, 0 /*10sec*/,
                                       0 /*1min*/, 0 /*5min*/};

typedef struct {
    uint8_t cachedMaxBrightness;
    int brightIncrement;
    bool waveOn[STRIP_CNT];
} ModeDotState;

typedef enum {
    modeDotOperOr,
    modeDotOperXor,
    modeDotOperAnd,
    modeDotOperNot,
    modeDotOperNot2,
} ModeDotOper;

static ModeDotState modeDotState = {0};

static int /*ModeDotOper*/ _modeDotParseOper(const char* modeDotOperStr) {
    if (strcasecmp(modeDotOperStr, "xor") == 0) return modeDotOperXor;
    if (strcasecmp(modeDotOperStr, "and") == 0) return modeDotOperAnd;
    if (strcasecmp(modeDotOperStr, "not") == 0) return modeDotOperNot;
    if (strcasecmp(modeDotOperStr, "not2") == 0) return modeDotOperNot2;
    return modeDotOperOr; // catch all
}

static void modeDotInit() {
    static const int defaultBrightIncrement = 25;

    // flush all state if clear all pixels was set. And then rebuild it all below
    if (getBoolParam(info.params, ledStripParamClearAllPixels)) modeDotState = {0};

    modeDotState.cachedMaxBrightness = getMaxBrightness();
    if (isParamSet(info.params, "brightIncrement") || modeDotState.brightIncrement == 0) {
        modeDotState.brightIncrement = abs(getToIntParam(info.params, "brightIncrement", defaultBrightIncrement));
    }
    if (modeDotState.brightIncrement > modeDotState.cachedMaxBrightness) modeDotState.brightIncrement = 0;

    uint32_t color = getPixelColorParam(info.params);
    if (color == 0) color = random(0x00fffffe) + 1;
    const bool forceColor = getBoolParam(info.params, "forceColor");
    const bool deltaColor = getBoolParam(info.params, "deltaColor");
    int targetStrip = getToIntParam(info.params, "strip", 0);
    if (targetStrip < 0 || targetStrip >= STRIP_CNT) targetStrip = 0;

    Adafruit_NeoPixel& strip(*strips[targetStrip]);
    if (isParamSet(info.params, "waveOn")) {
        modeDotState.waveOn[targetStrip] = getBoolParam(info.params, "waveOn");
        if (!modeDotState.waveOn[targetStrip]) strip.setBrightness(modeDotState.cachedMaxBrightness); // restore max brightness on strip
    }

    String modeDotOperStr = "or";
    (void) getValidParamValue(info.params, "oper", modeDotOperStr);
    const ModeDotOper modeDotOper = (ModeDotOper) _modeDotParseOper(modeDotOperStr.c_str());

    uint64_t valueParam = 0;
    {
        String valueParamStr;
        if (getValidParamValue(info.params, "value", valueParamStr)) {
            valueParamStr.toLowerCase();
            valueParam = strtoull(valueParamStr.c_str(), nullptr /*endptr*/, 0 /*base auto detect*/);
        }
    }
    valueParam = valueParam >> abs(getToIntParam(info.params, "shiftRight", 0));
    valueParam = valueParam << abs(getToIntParam(info.params, "shiftLeft", 0));

    // build a mask of the current pixels that have some color
    uint64_t currentValue = 0;
    for (uint16_t p=0; p < LEDS_PER_STRIP; ++p) {
        const uint32_t currPixelColor = strip.getPixelColor(p);
        if (currPixelColor != 0) {
#ifdef DEBUG
            Serial.print("strip "); Serial.print(targetStrip, DEC),
            Serial.print(" pixel "); Serial.print(p, DEC); Serial.print(" has color 0x");
            Serial.println(currPixelColor, HEX);
#endif
            // NOTE: using bitset or currentValue |= 1 << p; did not work above 31
            currentValue += (uint64_t)1 << p;   // bitSet(currentValue, p);
        }
    }

    uint64_t newValue = 0;
    switch (modeDotOper) {
        case modeDotOperXor:  newValue = valueParam ^ currentValue; break;
        case modeDotOperAnd:  newValue = valueParam & currentValue; break;
        case modeDotOperNot:  newValue = ~valueParam & currentValue; break;
        case modeDotOperNot2: newValue = valueParam | ~currentValue; break;
        case modeDotOperOr:   // fall through...
        default:
            newValue = valueParam | currentValue; break;
    }

#ifdef DEBUG
    Serial.print("dot mode oper: "); Serial.print(modeDotOperStr.c_str());
    Serial.print(" ("); Serial.print(modeDotOper, DEC); Serial.println(")");

    Serial.print("  old: H0x"); Serial.print((uint32_t) (currentValue >> 32), HEX);
    Serial.print(" L0x"); Serial.println((uint32_t) currentValue, HEX);

    Serial.print("param: H0x"); Serial.print((uint32_t) (valueParam >> 32), HEX);
    Serial.print(" L0x"); Serial.println((uint32_t) valueParam, HEX);

    Serial.print("  new: H0x"); Serial.print((uint32_t) (newValue >> 32), HEX);
    Serial.print(" L0x"); Serial.println((uint32_t) newValue, HEX);
#endif // #ifdef DEBUG

    for (uint16_t p=0; p < LEDS_PER_STRIP; ++p) {
        if (bitRead(newValue, p)) {
            if (forceColor ||
                bitRead(currentValue, p) == 0 ||
                (deltaColor && bitRead(valueParam, p))) strip.setPixelColor(p, color);
        } else {
            strip.setPixelColor(p, 0);
        }
    }
    strip.show();
}

static void modeDotFast() {
    static int direction = 1;

    for (int i=0; i < STRIP_CNT; ++i) {
        if (!modeDotState.waveOn[i]) continue;

        Adafruit_NeoPixel& strip(*strips[i]);
        const int currBrightness = strip.getBrightness();

        int newBrightness = currBrightness + modeDotState.brightIncrement * direction;
        if (direction > 0) {
            if (newBrightness > modeDotState.cachedMaxBrightness ) {
                direction *= -1;
                newBrightness = currBrightness + modeDotState.brightIncrement * direction;
            }
        } else {
            if (newBrightness < 1) { 
                direction *= -1;
                newBrightness = currBrightness + modeDotState.brightIncrement * direction;
            }
        }
        strip.setBrightness( (uint8_t) newBrightness );
        strip.show();
    }
}

static const Mode modeDot = { ledStripModeDot, ledStripModeDotStr, modeDotInit /*init*/, modeDotFast /*fast*/,
                              0 /*1sec*/, 0 /*5sec*/, 0 /*10sec*/, 0 /*1min*/, 0 /*5min*/};

// ----------------------------------------------------------------------

static const Mode allModes[] = { modeOff, modeStair, modeFill, modeRainbow, modeScan, modeBlink, modeBinaryCounter, modeDot };
static const int allModesCount = sizeof(allModes) / sizeof(allModes[0]);

void initLights() {
    for (int i=0; i < STRIP_CNT; ++i) strips[i]->begin();

    StringMap params;
    params[ledStripParamClearAllPixels] = "yes";
    params["randomColor"] = "yes";
    params["fast"] = "yes";
    params["timeout"] = "5";
    changeLedStripMode(ledStripModeFill, params);
}

void lightsFastTick() { if (allModes[info.currModeIndex].handlerTickFast != nullptr) (*(allModes[info.currModeIndex].handlerTickFast))(); }
void lights1SecTick() { 
    if (allModes[info.currModeIndex].handlerTick1Sec != nullptr) (*(allModes[info.currModeIndex].handlerTick1Sec))();
    checkIfModeTimedOut();
}
void lights5SecTick() { if (allModes[info.currModeIndex].handlerTick5Sec != nullptr) (*(allModes[info.currModeIndex].handlerTick5Sec))(); }
void lights10SecTick() { if (allModes[info.currModeIndex].handlerTick10Sec != nullptr) (*(allModes[info.currModeIndex].handlerTick10Sec))(); }
void lights1MinTick() { if (allModes[info.currModeIndex].handlerTick1Min != nullptr) (*(allModes[info.currModeIndex].handlerTick1Min))(); }
void lights5MinTick() {if (allModes[info.currModeIndex].handlerTick5Min != nullptr) (*(allModes[info.currModeIndex].handlerTick5Min))(); }

const char* getLightOperInfo() {
    return allModes[info.currModeIndex].ledStripModeStr;
}

static void setKeyAndData(String& key, String& data, const String& tokenMsg) {
    int equalSignIndex = tokenMsg.indexOf('=');
    if (equalSignIndex == -1) {
        key = tokenMsg;
        data = "true";   // default value when data is not provided
    } else {
        key = tokenMsg.substring(0, equalSignIndex);
        data = tokenMsg.substring(equalSignIndex + 1);
    }
}

void parseSetLightModeMessage(const char* message) {
    StringMap params;

    String msg(message);

    msg.replace(" ", "");
    // ref: ~/Library/Arduino15/packages/esp8266/hardware/esp8266/WString.h
    // at one point, I was going to do this:
    // msg.toLowerCase();
    // but I gave up on that for now.

    if (msg.length() > 0) {
        int initialPos = 0;
        int commaIndex = msg.indexOf(',');
        String key;
        String data;
        while (commaIndex != -1) {
            setKeyAndData(key, data, msg.substring(initialPos, commaIndex));
            params[key] = data;
            initialPos = commaIndex + 1;
            commaIndex = msg.indexOf(',', initialPos);
        }
        // parse token after last comma
        setKeyAndData(key, data, msg.substring(initialPos));
        params[key] = data;
    }

#ifdef DEBUG
    Serial.print("parseSetLightModeMessage: "); Serial.println(message);
    Serial.print("  Entries: "); Serial.println(params.size(), DEC);
    for (auto const &iter : params) {
        auto const &key = iter.first; auto const &data = iter.second;
        Serial.print("          ");
        Serial.print(key.c_str()); Serial.print(" => "); Serial.println(data.c_str());
    }
#endif // #ifdef DEBUG

    // See what message is about
    if (isParamSet(params, "mode")) {
        String modeStr;
        getParamValue(params, "mode", modeStr);
        for (int i=0; i < allModesCount; ++i) {
            if (modeStr.equalsIgnoreCase(allModes[i].ledStripModeStr)) {
                changeLedStripMode(allModes[i].ledStripMode, params);
                break;
            }
        }
    } else if (isParamSet(params, "clearGlobals")) {
        info.globalParams.clear();
    } else if (isParamSet(params, "clearGlobal")) {
        for (auto const &iter : params) info.globalParams.erase(iter.first);
    } else if (isParamSet(params, "setGlobal")) {
        for (auto const &iter : params) {
            auto const &key = iter.first; auto const &data = iter.second;
            info.globalParams[key] = data;
        }
    } else if (isParamSet(params, "reboot")) {
        // boom!
#ifdef DEBUG
        Serial.println("BOOM");
#endif
        delay(2200);
        // https://www.pieterverhees.nl/sparklesagarbage/esp8266/130-difference-between-esp-reset-and-esp-restart
        // evernote uuid: 2018-Mar-13-Tue@12:18:13
        ESP.reset();
        // ESP.restart();
    } else {
#ifdef DEBUG
        Serial.println("Message received made no sense");
#endif
    }
}

static uint32_t wheel(uint16_t wheelPos) {
    uint8_t r, g, b;

    switch (wheelPos / 128)
    {
        case 0:
            r = 127 - wheelPos % 128; // red down
            g = wheelPos % 128;       // green up
            b = 0;                    // blue off
            break;
        case 1:
            g = 127 - wheelPos % 128; // green down
            b = wheelPos % 128;       // blue up
            r = 0;                    // red off
            break;
        case 2:
            b = 127 - wheelPos % 128; // blue down
            r = wheelPos % 128;       // red up
            g = 0;                    // green off
            break;
    }
    return Adafruit_NeoPixel::Color(r,g,b);
}

static uint8_t getMaxBrightness() {
    uint8_t brightness = defaultMaxBrightness;
    StringMap::const_iterator iter = info.globalParams.find("maxBrightness");
    if (iter != info.globalParams.end()) brightness = (iter->second).toInt();

#ifdef DEBUG
    if (brightness != defaultMaxBrightness) {
        Serial.print("getMaxBrightness returning "); Serial.println(brightness, DEC);
    }
#endif
    return brightness;
}

static void clearPixelColorsAndShow() {
    const uint8_t brightness = getMaxBrightness();
    for (int i=0; i < STRIP_CNT; ++i) {
        Adafruit_NeoPixel& strip(*strips[i]);
        strip.clear();
        strip.setBrightness(brightness);
        strip.show();
    }
}

static void checkIfModeTimedOut() {
    if (info.secsUntilModeExpires < 0) return;

    if (--info.secsUntilModeExpires < 0) {
        const LedStripMode currentLedStripMode = allModes[ info.currModeIndex ].ledStripMode;

        // mode hit timeout: go to stairs and it will determine if it should go off based on motion
        StringMap params;
        params["_from_timeout"] = "yes";
        changeLedStripMode(currentLedStripMode == ledStripModeStair ? ledStripModeOff : ledStripModeStair,
                           params);
    }
}

static void changeLedStripMode(LedStripMode wantedLedStripMode, const StringMap& params) {
    const LedStripMode currentLedStripMode = allModes[ info.currModeIndex ].ledStripMode;
    for (int i=0; i < allModesCount; ++i) {
        if (allModes[i].ledStripMode == wantedLedStripMode) {
            const bool clearAllPixelsParamProvided = isParamSet(params, ledStripParamClearAllPixels);
            const bool shouldClearAllPixels = clearAllPixelsParamProvided && getBoolParam(params, ledStripParamClearAllPixels);

            // Clear strip only if mode is changing, or we were explicitly asked to do so
            if (clearAllPixelsParamProvided) {
                if (shouldClearAllPixels) clearPixelColorsAndShow();
            } else {
                if (currentLedStripMode != wantedLedStripMode || shouldClearAllPixels) clearPixelColorsAndShow();
            }

            info.secsUntilModeExpires = isParamSet(params, ledStripParamTimeout) ?
                getToIntParam(params, ledStripParamTimeout) :
                getToIntParam(info.globalParams, "defaultTimeout", defaultTimeoutInSeconds);

#ifdef DEBUG
            Serial.print("setting secsUntilModeExpires (if not overriden) to: ");
            Serial.println(info.secsUntilModeExpires, DEC);
#endif  // #ifdef DEBUG

            info.currModeIndex = i;
            info.params = params;

            if (allModes[i].handlerInit != nullptr) (*(allModes[i].handlerInit))();
            ++state.lightModeChanges;
            break;
        }
    }
}

static void changeLedStripMode(LedStripMode wantedLedStripMode) {
    StringMap noopParams;
    changeLedStripMode(wantedLedStripMode, noopParams);
}

static uint32_t getPixelColorParam(const StringMap& params) {
    // should be red, green and blue ... but not what I see! :)
    return Adafruit_NeoPixel::Color( (uint8_t) getToIntParam(params, "green"),
                                     (uint8_t) getToIntParam(params, "red"),
                                     (uint8_t) getToIntParam(params, "blue") );
}

// ----------------------------------------------------------------------

bool parseBooleanValue(const char* valueStr) {
    if (valueStr == nullptr) return false;
    if (strncasecmp(valueStr, "n", 1) == 0) return false;
    if (strncasecmp(valueStr, "y", 1) == 0) return true;
    if (strcasecmp(valueStr, "false") == 0) return false;
    if (strcasecmp(valueStr, "true") == 0) return true;
    return strtoul(valueStr, nullptr /*endptr*/, 0 /*base*/) != 0;
}

bool isParamSet(const StringMap& params, const char* const paramName, const char* const paramValue) {
    StringMap::const_iterator iter = params.find(paramName); // case sensitive!
    if (iter == params.end()) return false;
    // https://www.arduino.cc/en/Tutorial/StringComparisonOperators (ignore case)
    if (paramValue != nullptr && !(iter->second).equalsIgnoreCase(paramValue)) return false;
    // if (paramValue != nullptr && iter->second != paramValue) return false;
    return true;
}

bool isParamSet(const StringMap& params, const char* const paramName) {
    return isParamSet(params, paramName, nullptr);
}

bool getParamValue(const StringMap& params, const char* const paramName, String& paramValueFound) {
    StringMap::const_iterator iter = params.find(paramName);  // case sensitive!
    if (iter != params.end()) {
        paramValueFound = iter->second;
        return true;
    }
    return false;
}

bool getValidParamValue(const StringMap& params, const char* const paramName, String& paramValueFound) {
    return getParamValue(params, paramName, paramValueFound) && paramValueFound.length() > 0;
}

bool getBoolParam(const StringMap& params, const char* const paramName) {
    String paramValue;
    if (!getValidParamValue(params, paramName, paramValue)) return false;
    return parseBooleanValue(paramValue.c_str());
}

int getToIntParam(const StringMap& params, const char* const paramName, int defaultValue) {
    String paramValue;
    if (!getValidParamValue(params, paramName, paramValue)) return defaultValue;
    return (int) paramValue.toInt();
}

int getToIntParam(const StringMap& params, const char* const paramName) {
    return getToIntParam(params, paramName, 0);
}


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
