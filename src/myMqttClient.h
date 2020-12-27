#ifndef _MY_MQTT_CLIENT_H

#define _MY_MQTT_CLIENT_H

#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

static const unsigned int defaultMqttReconnect = 30;

typedef struct {
    unsigned int reconnectTicks;  // do not mqtt connect while this is > 0
} MqttState;

extern MqttState mqttState;



#endif  // define _MY_MQTT_CLIENT_H
