#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <CREDENTIALS.h>

// PINS FOR UP AND DOWN SHOULD NOT BE ON D3 AND D4 BECAUSE OF PULL-UP RESISTORS. CHANGE THIS!!!!
#define INTERRUPT_SIGNAL_IN D6
#define PIN_UP_SWITCH_OUT D2
#define PIN_DOWN_SWITCH_OUT D1

// From hand switch to Arduino
#define PIN_UP_SWITCH_IN D4
#define PIN_DOWN_SWITCH_IN D3

// Constants
//#define MAX_ULONG 4294967295 // Maximum value an unsigned long can have
#define CYCLE_TIME_US 1000  // 1 ms
#define ARRAY_SIZE 32       // Size of the array that stores received bits
#define MATCH_ARRAY_SIZE 23 // Size of the array that contains the bit pattern to compare to
#define BIT_INDEX_HEIGHT 23 // Index at which the height value starts

const int maxHeight = 113;
const int upOffset = 1;
const int downOffset = 2;

// Array that stores received bits
byte gBitArray[ARRAY_SIZE];

// Array that contains the bit pattern to compare to
byte gMatchArray[MATCH_ARRAY_SIZE] = {1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1};

byte gCurBit = 0;
byte gCurHeight = 0;
byte gNumMatchingBits = 0;
byte gTargetHeight = 0;
bool gIsSwitchOverride = false;
bool gIsAutoMode = true;
volatile unsigned long gCurTimeUs = 0;
volatile unsigned long gLastTimeUs = 0;

enum direction
{
    UP,
    DOWN,
    NONE
};
direction currentDirection = NONE;

WiFiClient espClient;
PubSubClient client(espClient);

bool checkTime()
{
    gCurTimeUs = micros();
    if (gCurTimeUs - gLastTimeUs >= CYCLE_TIME_US)
    {
        gLastTimeUs = gCurTimeUs;
        return true;
    }
    return false;
}

void ICACHE_RAM_ATTR onEdgeEvent()
{
    // Synchronize wait time with edge
    gLastTimeUs = micros() - (CYCLE_TIME_US / 2);
}

void extractHeightFromBitArray()
{
    // Extract the height value from the bit array
    byte newHeight = 0;
    for (byte i = 0; i < 8; ++i)
        newHeight |= (gBitArray[(BIT_INDEX_HEIGHT + i) % ARRAY_SIZE] << i);
    newHeight = ~newHeight;

    // Plausibility check 1: height must be in a valid range
    if (newHeight < 60 || newHeight > 120)
        return;

    // Plausibility check 2: height must not differ more than 5cm from last value
    if (gCurHeight == 0 || abs(gCurHeight - newHeight) < 5)
        gCurHeight = newHeight;
}

void handleReceivedBit()
{
    // Shift the array
    for (byte i = 0; i < ARRAY_SIZE - 1; ++i)
        gBitArray[i] = gBitArray[i + 1];

    // Insert the new value
    gBitArray[ARRAY_SIZE - 1] = gCurBit;

    // Count the number of matching bits
    for (gNumMatchingBits = 0; gNumMatchingBits < MATCH_ARRAY_SIZE; gNumMatchingBits++)
        if (gBitArray[gNumMatchingBits] != gMatchArray[gNumMatchingBits])
            break;

    // Update the height if we have a match
    if (gNumMatchingBits >= MATCH_ARRAY_SIZE)
        extractHeightFromBitArray();
}

void handleCurrentSignalBit()
{
    // Grab the current sample and immediately inform the real switch
    gCurBit = digitalRead(INTERRUPT_SIGNAL_IN);
    handleReceivedBit();
}

void moveTableUp()
{
    currentDirection = UP;

    digitalWrite(PIN_DOWN_SWITCH_OUT, LOW);
    digitalWrite(PIN_UP_SWITCH_OUT, HIGH);
}

void moveTableDown()
{
    currentDirection = DOWN;

    digitalWrite(PIN_UP_SWITCH_OUT, LOW);
    digitalWrite(PIN_DOWN_SWITCH_OUT, HIGH);
}

void stopTable()
{
    currentDirection = NONE;

    digitalWrite(PIN_DOWN_SWITCH_OUT, LOW);
    digitalWrite(PIN_UP_SWITCH_OUT, LOW);

    gTargetHeight = gCurHeight;
}

void controlTableMovement()
{
    if (gIsSwitchOverride)
    {
        return;
    }

    if (gIsAutoMode)
    {
        if (currentDirection == UP && (gCurHeight + upOffset) >= gTargetHeight)
        {
            stopTable();
        }
        if (currentDirection == DOWN && (gCurHeight - downOffset) <= gTargetHeight)
        {
            stopTable();
        }
    }
}

void setHeight()
{
    if (!gIsSwitchOverride)
    {
        gIsAutoMode = true;
        if (gCurHeight < gTargetHeight && gTargetHeight < maxHeight)
        {
            moveTableUp();
        }

        else if (gCurHeight > gTargetHeight)
        {
            moveTableDown();
        }
    }
}

void callback(char *topic, byte *payload, unsigned int length)
{
    String receivedtopic = topic;

    String value = "";
    for (unsigned int i = 0; i < length; i++)
    {
        value += (char)payload[i];
    }

    if (receivedtopic == "deskcontrol/setheight")
    {
        gTargetHeight = value.toInt();
        setHeight();
    }
}

// Read input from the real switches (can override PC)
void handleSwitchInputs()
{
    bool upSwitchPressed = digitalRead(PIN_UP_SWITCH_IN);
    bool downSwitchPressed = digitalRead(PIN_DOWN_SWITCH_IN);

    if (gIsSwitchOverride)
    {
        if (upSwitchPressed == HIGH && downSwitchPressed == HIGH)
        {
            stopTable();
            gIsSwitchOverride = false;
        }
    }
    else
    {
        if (upSwitchPressed == LOW)
        {
            moveTableUp();
            gIsSwitchOverride = true;
            gIsAutoMode = false;
        }
        else if (downSwitchPressed == LOW)
        {
            moveTableDown();
            gIsSwitchOverride = true;
            gIsAutoMode = false;
        }
    }
}

void initWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.config(ip, dns, gateway, subnet);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS); //Connect to the WiFi network
}

void initOTA()
{
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_SPIFFS
            type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
            Serial.println("End Failed");
    });
    ArduinoOTA.begin();
}

long lastReconnectAttempt = 0;
boolean mqttReconnect()
{
    if (client.connect("DeskClient"))
    {
        client.subscribe("deskcontrol/setheight");
    }
    return client.connected();
}

void initMQTT()
{
    if (!client.connected())
    {
        long now = millis();
        if (now - lastReconnectAttempt > 5000)
        {
            lastReconnectAttempt = now;
            // Attempt to reconnect
            if (mqttReconnect())
            {
                lastReconnectAttempt = 0;
            }
        }
    }
    else
    {
        // Client connected

        client.loop();
    }
}

long lastPublish = 0;
byte lastHeight = 0;
void publishHeight()
{
    long now = millis();
    if (lastHeight != gCurHeight && now - lastPublish > 5000)
    {
        lastPublish = now;
        lastHeight = gCurHeight;
        char heightBuffer[3];
        sprintf(heightBuffer, "%d", gCurHeight);
        client.publish("deskcontrol/currentheight", heightBuffer, true);
    }
}

void setup()
{
    Serial.begin(9600);
    pinMode(PIN_UP_SWITCH_OUT, OUTPUT);
    pinMode(PIN_DOWN_SWITCH_OUT, OUTPUT);
    pinMode(INTERRUPT_SIGNAL_IN, INPUT);
    pinMode(PIN_UP_SWITCH_IN, INPUT_PULLUP);
    pinMode(PIN_DOWN_SWITCH_IN, INPUT_PULLUP);
    digitalWrite(PIN_UP_SWITCH_OUT, LOW);
    digitalWrite(PIN_DOWN_SWITCH_OUT, LOW);
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_SIGNAL_IN), onEdgeEvent, CHANGE);

    initWiFi();

    initOTA();

    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    lastReconnectAttempt = 0;

    gLastTimeUs = micros();
}

void loop()
{
    ArduinoOTA.handle();
    initMQTT();

    // Read the current height signal bit and interpret it
    if (checkTime())
    {
        handleCurrentSignalBit();
    }

    controlTableMovement();

    handleSwitchInputs();

    publishHeight();
}