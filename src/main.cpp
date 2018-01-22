#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <CREDENTIALS.h>

// PINS FOR UP AND DOWN SHOULD NOT BE ON D3 AND D4 BECAUSE OF PULL-UP RESISTORS. CHANGE THIS!!!!
#define INTERRUPT_SIGNAL_IN D4
#define PIN_UP_SWITCH_OUT D2
#define PIN_DOWN_SWITCH_OUT D1

// Constants
//#define MAX_ULONG 4294967295 // Maximum value an unsigned long can have
#define CYCLE_TIME_US 1000  // 1 ms
#define ARRAY_SIZE 32       // Size of the array that stores received bits
#define MATCH_ARRAY_SIZE 23 // Size of the array that contains the bit pattern to compare to
#define BIT_INDEX_HEIGHT 23 // Index at which the height value starts

// Array that stores received bits
byte gBitArray[ARRAY_SIZE];

// Array that contains the bit pattern to compare to
byte gMatchArray[MATCH_ARRAY_SIZE] = {1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1};

byte gCurBit = 0;
byte gCurHeight = 0;
byte gNumMatchingBits = 0;
byte gTargetHeight = 0;
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

void onEdgeEvent()
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
    if (currentDirection == UP && (gCurHeight + 1) >= gTargetHeight)
    {
        stopTable();
    }
    if (currentDirection == DOWN && (gCurHeight - 1) <= gTargetHeight)
    {
        stopTable();
    }
}

void setHeight()
{
    if (gCurHeight < gTargetHeight && gTargetHeight < 113)
    {
        moveTableUp();
    }

    else if (gCurHeight > gTargetHeight)
    {
        moveTableDown();
    }
}

void callback(char *topic, byte *payload, unsigned int length)
{
    String receivedtopic = topic;

    String value = "";
    for (int i = 0; i < length; i++)
    {
        value += (char)payload[i];
    }

    if (receivedtopic == "deskcontrol/setheight")
    {
        gTargetHeight = value.toInt();
        setHeight();
    }
}

void initWiFi()
{
    IPAddress ip(10, 3, 3, 60);
    IPAddress gateway(10, 3, 3, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress dns(10, 3, 3, 1);

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.config(ip, dns, gateway, subnet);
    WiFi.begin(WIFI_SSID, WIFI_PASS); //Connect to the WiFi network
    Serial.println("Connecting");
    while (WiFi.status() != WL_CONNECTED)
    { //Wait for connection

        delay(1000);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("IP address: ");
    Serial.print(WiFi.localIP()); //Print the local IP
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

void initMQTT()
{
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);

    while (!client.connected())
    {
        Serial.println("Connecting to MQTT...");

        if (client.connect("DeskClient", mqtt_user, mqtt_pass))
        {
            Serial.println("connected");
        }
        else
        {
            Serial.print("failed with state ");
            Serial.print(client.state());
            delay(2000);
        }
    }

    client.subscribe("deskcontrol/setheight");
}

void setup()
{
    Serial.begin(9600);
    pinMode(PIN_UP_SWITCH_OUT, OUTPUT);
    pinMode(PIN_DOWN_SWITCH_OUT, OUTPUT);
    pinMode(INTERRUPT_SIGNAL_IN, INPUT);
    digitalWrite(PIN_UP_SWITCH_OUT, LOW);
    digitalWrite(PIN_DOWN_SWITCH_OUT, LOW);
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_SIGNAL_IN), onEdgeEvent, CHANGE);

    initWiFi();

    initOTA();

    initMQTT();

    gLastTimeUs = micros();
}

//unsigned long previousMillis = 0;

void loop()
{
    ArduinoOTA.handle();
    client.loop();

    // Read the current height signal bit and interpret it
    if (checkTime())
    {
        handleCurrentSignalBit();
    }

    controlTableMovement();
}