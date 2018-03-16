#include "common.h"
#include "myMqttClient.h"
#include "ssidConfig.h"

// huzzah ref: https://www.adafruit.com/products/2821

// evernote uuid: 2018-Mar-03-Sat@07:37:13

#define PIN_MQTT_LED      13 /* yellow wire */
#define PIN_MQTT_LED_ON   HIGH
#define PIN_MQTT_LED_OFF  LOW


#define DEV_PREFIX "/garage_steps/"

// note: admin_flags is really a debug thing. Normally never to be used!
#define MQTT_SUB_ADMIN_FLAGS     "admin_flags"        
#define MQTT_SUB_CRAZY_LED       "crazy_led"
#define MQTT_SUB_DISABLE_MOTION  "disable_motion_sensor"
#define MQTT_XUB_TRIGGER_MOTION  "trigger_motion"  // xub: sub and pub
#define MQTT_XUB_SET_LIGHT_MODE  "set_light_mode"  // xub: sub and pub

#define MQTT_PUB_OPER_FLAGS        "oper_flags"  // flags, motion, ...
#define MQTT_PUB_NO_MOTION_MINUTES "no_motion_minutes"
#define MQTT_PUB_LIGHT_MODE        "oper_light_mode"

typedef void (*OnOffToggle)();

// FWDs
bool checkWifiConnected();
bool checkMqttConnected();
static void parseOnOffToggle(const char* subName, const char* message, 
                             OnOffToggle onPtr, OnOffToggle offPtr, OnOffToggle togglePtr);

MqttState mqttState;

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD);

// Create the feeds that will be used for publishing and subscribing to service changes.
// Be aware of MAXSUBSCRIPTIONS in ~/myArduinoLibraries/Adafruit_MQTT_Library/Adafruit_MQTT.h  -- currently set to 5
Adafruit_MQTT_Subscribe service_sub_admin_flags = Adafruit_MQTT_Subscribe(&mqtt, DEV_PREFIX MQTT_SUB_ADMIN_FLAGS);  // debug use only
Adafruit_MQTT_Subscribe service_sub_crazy_led = Adafruit_MQTT_Subscribe(&mqtt, DEV_PREFIX MQTT_SUB_CRAZY_LED);
Adafruit_MQTT_Subscribe service_sub_disable_motion_sensor = Adafruit_MQTT_Subscribe(&mqtt, DEV_PREFIX MQTT_SUB_DISABLE_MOTION);
Adafruit_MQTT_Subscribe service_sub_trigger_motion = Adafruit_MQTT_Subscribe(&mqtt, DEV_PREFIX MQTT_XUB_TRIGGER_MOTION);
Adafruit_MQTT_Subscribe service_sub_set_light_mode = Adafruit_MQTT_Subscribe(&mqtt, DEV_PREFIX MQTT_XUB_SET_LIGHT_MODE);

Adafruit_MQTT_Publish service_pub_oper_flags = Adafruit_MQTT_Publish(&mqtt, DEV_PREFIX MQTT_PUB_OPER_FLAGS);
Adafruit_MQTT_Publish service_pub_no_motion_minutes = Adafruit_MQTT_Publish(&mqtt, DEV_PREFIX MQTT_PUB_NO_MOTION_MINUTES);
Adafruit_MQTT_Publish service_pub_oper_light_mode = Adafruit_MQTT_Publish(&mqtt, DEV_PREFIX MQTT_PUB_LIGHT_MODE);
Adafruit_MQTT_Publish service_pub_trigger_motion = Adafruit_MQTT_Publish(&mqtt, DEV_PREFIX MQTT_XUB_TRIGGER_MOTION);
Adafruit_MQTT_Publish service_pub_set_light_mode = Adafruit_MQTT_Publish(&mqtt, DEV_PREFIX MQTT_XUB_SET_LIGHT_MODE);

void initMyMqtt() {
    memset(&mqttState, 0, sizeof(mqttState));

    pinMode(PIN_MQTT_LED, OUTPUT);

    digitalWrite(PIN_MQTT_LED, PIN_MQTT_LED_OFF);

#ifdef DEBUG
    // Connect to WiFi access point.
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(WLAN_SSID);
#endif

    // Set WiFi to station mode
    // https://www.exploreembedded.com/wiki/Arduino_Support_for_ESP8266_with_simple_test_code
    if (!WiFi.mode(WIFI_STA)) {
#ifdef DEBUG
        Serial.println("Fatal: unable to set wifi mode");
#endif
        delay(1000);
        ESP.restart();
    }
    WiFi.begin(WLAN_SSID, WLAN_PASS);
}

void myMqttLoop() {
    yield();  // make esp8266 happy

    if (!checkWifiConnected()) return;
    if (!checkMqttConnected()) return;

    // Listen for updates on any subscribed MQTT feeds and process them all.
    Adafruit_MQTT_Subscribe* subscription;
    while ((subscription = mqtt.readSubscription())) {
        const char* const message = (const char*) subscription->lastread;

        if (subscription == &service_sub_admin_flags) {
            const uint8_t newFlags = (uint8_t) atoi(message);
#ifdef DEBUG
            // note: this is really a debug thing. Normally never to be used!
            Serial.print("HACK: setting admin flags: "); Serial.println((int) newFlags);
#endif
            setFlags(newFlags);
            sendOperState();
            sendOperLightMode();
        } else if (subscription == &service_sub_crazy_led) {
            parseOnOffToggle(MQTT_SUB_CRAZY_LED, message, setCrazyLedOn, clearCrazyLedOn, toggleCrazyLed);
        } else if (subscription == &service_sub_disable_motion_sensor) {
            parseOnOffToggle(MQTT_SUB_DISABLE_MOTION, message, setDisableMotionSensor, clearDisableMotionSensor, toggleDisableMotionSensor);
            state.overrideMotionPinCountdownSeconds = 0;  // messing with this topic causes reset of override countown
        } else if (subscription == &service_sub_trigger_motion) {
            // if strlen of message is 0, that means we caused it due to publish below... silently ignore it
            if (strlen(message) == 0) {
#ifdef DEBUG
                Serial.println("service_sub_trigger_motion ignoring empty message");
#endif
                continue;
            }

            // messing with this topic while disable motion sensor is set will be a noop
            // add 1 to absolute value to give it at least 1 second in cases where message is not
            // a properly formatted integer
            state.overrideMotionPinCountdownSeconds = getDisableMotionSensor() ? 0 : abs(atoi(message) + 1);
#ifdef DEBUG
            Serial.print("setting overrideMotionPinCountdownSeconds: ");
            Serial.println(state.overrideMotionPinCountdownSeconds);
#endif
            updateMotionTick1Sec(); // explicitly call motion, so it does not miss this change

            // explicitly clear mqtt topic
            /*const*/ uint8_t foo_payload = ~0;
            if (!service_pub_trigger_motion.publish(&foo_payload, 0 /*bLen*/)) {
#ifdef DEBUG
                Serial.println("Unable to publish reset of trigger_motion");
#endif
                mqtt.disconnect();
            }
        } else if (subscription == &service_sub_set_light_mode) {
            // if strlen of message is 0, that means we caused it due to publish below... silently ignore it
            if (strlen(message) == 0) {
#ifdef DEBUG
                Serial.println("service_sub_set_light_mode ignoring empty message");
#endif
                continue;
            }
            parseSetLightModeMessage(message);

            // explicitly clear mqtt topic
            /*const*/ uint8_t foo_payload = ~0;
            if (!service_pub_set_light_mode.publish(&foo_payload, 0 /*bLen*/)) {
#ifdef DEBUG
                Serial.println("Unable to publish reset of set_light_mode");
#endif
                mqtt.disconnect();
            }
        } else {
#ifdef DEBUG
            Serial.print("got unexpected msg on subscription: "); 
            Serial.println(subscription->topic);
#endif
        }
    }
}

bool checkWifiConnected() {
    static bool lastConnected = false;

    const bool currConnected = WiFi.status() == WL_CONNECTED;

    if (lastConnected != currConnected) {

        if (currConnected) {
#ifdef DEBUG
            Serial.println("WiFi connected");
            Serial.print("IP address: "); Serial.println(WiFi.localIP());
#endif

            // idem potent. If it fails, this is a game stopper...
            if (!mqtt.subscribe(&service_sub_admin_flags) ||
                !mqtt.subscribe(&service_sub_crazy_led) ||
                !mqtt.subscribe(&service_sub_disable_motion_sensor) ||
                !mqtt.subscribe(&service_sub_trigger_motion) ||
                !mqtt.subscribe(&service_sub_set_light_mode)) {
#ifdef DEBUG
                Serial.println("Fatal: unable to subscribe to mqtt");
                delay(9000);
#endif
                delay(1000);
                ESP.restart();
            }

        } else {
#ifdef DEBUG
            Serial.println("WiFi disconnected");
#endif
            digitalWrite(PIN_MQTT_LED, PIN_MQTT_LED_OFF);
        }

        lastConnected = currConnected;
    }

    return currConnected;
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
bool checkMqttConnected() {
    static bool lastMqttConnected = false;

    // not attempt to reconnect if there are reconnect ticks outstanding
    if (mqttState.reconnectTicks > 0) return false;

    const bool currMqttConnected = mqtt.connected();

    // noop?
    if (lastMqttConnected && currMqttConnected) return true;

    digitalWrite(PIN_MQTT_LED, currMqttConnected ? PIN_MQTT_LED_ON : PIN_MQTT_LED_OFF);

    if (currMqttConnected) {
#ifdef DEBUG
        Serial.println("MQTT Connected!");
#endif

        // Don't send oper_state. Let mqtt admin dictate what the initial values should
        // be from broker
        // sendOperState();
        sendOperLightMode();
    } else {
#ifdef DEBUG
        Serial.print("MQTT is connecting... ");
#endif

        // Note: the connect call can block for up to 6 seconds.
        //       when mqtt is out... be aware.
        const int8_t ret = mqtt.connect();
        if (ret != 0) {
#ifdef DEBUG
            Serial.println(mqtt.connectErrorString(ret));
#endif
            mqtt.disconnect();

            // do not attempt to connect before a few ticks
            mqttState.reconnectTicks = defaultMqttReconnect;
        } else {
#ifdef DEBUG
        Serial.println("done.");
#endif
        }
    }

    lastMqttConnected = currMqttConnected;
    return currMqttConnected;
}

void mqtt1SecTick() {
    static uint8_t lastFlags = 0;
    static uint32_t lastLightModeChanges = 0;

    if (mqttState.reconnectTicks > 0) --mqttState.reconnectTicks;

#ifdef DEBUG
    if (! mqtt.connected()) {
        Serial.print("mqtt1SecTick"); 
        Serial.print(" reconnectTics: "); Serial.print(mqttState.reconnectTicks, DEC);
        Serial.print(" mqtt_led: "); Serial.print(digitalRead(PIN_MQTT_LED) == PIN_MQTT_LED_ON ? "on" : "off");
        Serial.println("");
    }
#endif

    if (lastFlags != state.flags) {
        lastFlags = state.flags;
#ifdef DEBUG
        Serial.print("detected change in state flags. Sending update ");
        Serial.println(lastFlags, DEC);
#endif
        sendOperState();
    }

    if (lastLightModeChanges != state.lightModeChanges) {
        lastLightModeChanges = state.lightModeChanges;
#ifdef DEBUG
        Serial.print("detected change in light mode. Sending update ");
        Serial.println(lastLightModeChanges, DEC);
#endif
        sendOperLightMode();
    }
}

void mqtt10MinTick() {
#ifdef DEBUG
    Serial.println("mqtt10MinTick -- sending gratuitous state");
#endif

    // gratuitous
    sendOperState();
    sendOperLightMode();
}

void sendOperState() {
    if (! mqtt.connected()) return;

    uint32_t noMotionMins = 0;
    if (!getMotionSensorOperState()) {
        noMotionMins = (uint32_t) state.motionInfo.lastChangedMin +
            (uint32_t) state.motionInfo.lastChangedHour * 60;
    }

    if (!service_pub_oper_flags.publish( (uint32_t) state.flags ) ||
        !service_pub_no_motion_minutes.publish(noMotionMins)) {
#ifdef DEBUG
        Serial.println("Unable to publish operational state");
#endif
        mqtt.disconnect();
    }
}

void sendOperLightMode() {
    if (! mqtt.connected()) return;

    if (!service_pub_oper_light_mode.publish(getLightOperInfo())) {
#ifdef DEBUG
        Serial.println("Unable to publish light mode value");
#endif
        mqtt.disconnect();
    }
}

static void parseOnOffToggle(const char* subName, const char* message, 
                             OnOffToggle onPtr, OnOffToggle offPtr, OnOffToggle togglePtr) {
    if (onPtr != 0 && strncmp(message, "on", 2) == 0) { (*onPtr)(); } 
    else if (offPtr != 0 && strncmp(message, "off", 3) == 0) { (*offPtr)(); }
    else if (togglePtr != 0 && strncmp(message, "toggle", 6) == 0) { (*togglePtr)(); }

#ifdef DEBUG
    Serial.print("got msg on "); 
    Serial.print(subName); 
    Serial.print(": ");
    Serial.println(message);
#endif
}
