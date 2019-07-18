#include <Arduino.h>
// Downloaded from https://github.com/teebr/Influx-Arduino
#include <InfluxArduino.hpp>
#include <WiFi.h>

#include "Config.h"
#include "RootCert.hpp"

#define CONNECTION_RETRY 10
#define INFLUX_UPDATE_MILLISECONDS  60*1000
#define WATER_UPDATE_MILLISECONDS   60*1000
#define SOLENOID_PULSE_LENGTH   100
#define WATER_TO_GPM            0.5
#define WATER_DEBOUNCE          75

#define START_BUTTON_PIN 26
#define DATA_PIN    12
#define CLK_PIN     14
#define LATCH_PIN   13
#define WATER_METER_PIN 34
#define OPEN_PIN    33
#define CLOSE_PIN   32

// Switches
#define SW_VALVE1_PIN   4
#define SW_VALVE2_PIN   5
#define SW_VALVE3_PIN   18
#define SW_VALVE4_PIN   19
#define SW_VALVE5_PIN   21
#define SW_VALVE6_PIN   22
#define SW_VALVE7_PIN   23

// Binary values for the shift register
#define VALVE1_VALUE    254
#define VALVE2_VALUE    253
#define VALVE3_VALUE    251
#define VALVE4_VALUE    247
#define VALVE5_VALUE    239
#define VALVE6_VALUE    223
#define VALVE7_VALUE    191


#define RUNNING_MEASUREMENT     "open_valve"
#define WATER_MEASUREMENT        "water_gallons"


Config config;
InfluxArduino influx;
bool RUNNING = false;
unsigned long INFLUX_TIMER = 0;
unsigned long WATER_TIMER = 0;
unsigned long ZONE_START_TIME = 0;
uint8_t CURRENT_VALVE = 0;
uint8_t VALVE_TO_SEND = 1;

// Water Flow variables
static DRAM_ATTR uint32_t waterCounter = 0;
static DRAM_ATTR uint32_t waterDebounceTimer = 0;
portMUX_TYPE WATER_COUNTER_MUX = portMUX_INITIALIZER_UNLOCKED;

bool SW_VALVE1 = false;
bool SW_VALVE3 = false;
bool SW_VALVE4 = false;
bool SW_VALVE2 = false;
bool SW_VALVE5 = false;
bool SW_VALVE6 = false;
bool SW_VALVE7 = false;

// Everything off (inverted output)
uint8_t SHIFT_VALUE = 255;

void IRAM_ATTR waterMeterInterrupt()
{
    portENTER_CRITICAL_ISR(&WATER_COUNTER_MUX);
    uint32_t now = millis();
    if (now - waterDebounceTimer > WATER_DEBOUNCE) {
        waterDebounceTimer = now;
        waterCounter++;
    }
    portEXIT_CRITICAL_ISR(&WATER_COUNTER_MUX);
}

void sendWaterMetric()
{

    // Runs as close to exactly every 60 seconds as possible.
    unsigned long now = millis();
    if (now - WATER_TIMER >= WATER_UPDATE_MILLISECONDS)
    {
        unsigned long minSinceLastFlush = (now - WATER_TIMER) / (60.00 * 1000);
        uint32_t gpm = 0;

        portENTER_CRITICAL(&WATER_COUNTER_MUX);
        gpm = waterCounter / minSinceLastFlush * WATER_TO_GPM;
        waterCounter = 0;
        portEXIT_CRITICAL(&WATER_COUNTER_MUX);

        Serial.print("Now: ");
        Serial.println(now);
        Serial.print("PREVIOUS: ");
        Serial.println(WATER_TIMER);

        WATER_TIMER = now;

        Serial.print("Gallons last minute: ");
        Serial.println(gpm);
        Serial.println();

        String tags = "location=" + config.Location + ",sensor=" + config.Sensor;
        String fields = "value=" + String(gpm) + ".0";
        sendDatapoint(WATER_MEASUREMENT, tags.c_str(), fields.c_str());
    }
}

void connectToWifi()
{
    WiFi.mode(WIFI_STA);
    while (true)
    {
        Serial.print("Connecting to Wifi network ");
        Serial.print(config.WifiSSID);
        Serial.print(" ");
        WiFi.scanNetworks();
        WiFi.begin(config.WifiSSID.c_str(), config.WifiPassword.c_str());
        for (int x = 0; x < CONNECTION_RETRY; x++)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println("Connected");
                return;
            }
            Serial.print(".");
            delay(500);
        }
    }
}

bool sendDatapoint(const char *measurement, const char *tags, const char *fields)
{
    // Make sure there is a connection
    if (WiFi.status() != WL_CONNECTED)
    {
        connectToWifi();
    }

    // Send the data point
    if (!influx.write(measurement, tags, fields))
    {
        Serial.print("ERROR sending ");
        Serial.print(measurement);
        Serial.print(": ");
        Serial.println(influx.getResponse());
        return false;
    }

    Serial.print("MEASUREMENT: ");
    Serial.print(measurement);
    Serial.print(", tags: ");
    Serial.print(tags);
    Serial.print(", fields: ");
    Serial.println(fields);

    return true;
}

void reconfigureCheck()
{
    if (Serial.available())
    {
        char code = Serial.read();
        if (code == 'i' || code == 'I')
        {
            printConfig(config);
            return;
        }
        else if (code == 'c' || code == 'C')
        {
            // Reconfigure the sensor
            askForSettings(config);
        }
        else if (code == 'p' || code == 'P') {
            askForPreferences(config);
            saveConfig(config);
            loadConfig(config);
        }
    }
}

// ----------------------------------------------------------------------------
// Shift Register operations
// ----------------------------------------------------------------------------

void switchOutput()
{
    digitalWrite(LATCH_PIN, HIGH);
    for (int x = 0; x < 8; x++)
    {
        uint8_t output = SHIFT_VALUE & (1 << x);
        // Serial.print("BIT: ");
        if (output)
        {
            digitalWrite(DATA_PIN, HIGH);
            // Serial.println("1");
        }
        else
        {
            digitalWrite(DATA_PIN, LOW);
            // Serial.println("0");
        }

        digitalWrite(CLK_PIN, LOW);
        delay(25);
        digitalWrite(CLK_PIN, HIGH);
        delay(25);
    }
    digitalWrite(CLK_PIN, LOW);
    digitalWrite(LATCH_PIN, LOW);
    // Serial.println();
}

void outputOn(uint8_t valve)
{
    bitClear(SHIFT_VALUE, 8 - valve);
    switchOutput();
}

void outputOff(uint8_t valve)
{
    bitSet(SHIFT_VALUE, 8 - valve);
    switchOutput();
}

void allOutputsOff()
{
    SHIFT_VALUE = 255;
    switchOutput();
}

void firstOutput()
{
    // disable all outputs
    allOutputsOff();

    // then tunr on the first output
    digitalWrite(CLK_PIN, LOW);

    digitalWrite(LATCH_PIN, HIGH);
    digitalWrite(DATA_PIN, LOW);

    delay(20);
    digitalWrite(CLK_PIN, HIGH);
    delay(20);
}

void nextOutput()
{
    // The first output already shifted one into the register (DATA_PIN, LOW)
    // This just bumps it forward by one
    digitalWrite(CLK_PIN, LOW);

    digitalWrite(LATCH_PIN, HIGH);
    digitalWrite(DATA_PIN, HIGH);

    delay(20);
    digitalWrite(CLK_PIN, HIGH);
    delay(20);
}

// ----------------------------------------------------------------------------
// Valve operations
// ----------------------------------------------------------------------------

void pulseSolenoidOpen()
{
    delay(250);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(OPEN_PIN, HIGH);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(OPEN_PIN, LOW);
    delay(SOLENOID_PULSE_LENGTH);
}

void pulseSolenoidClosed()
{
    delay(250);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(CLOSE_PIN, HIGH);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(CLOSE_PIN, LOW);
    delay(SOLENOID_PULSE_LENGTH);
}

void valveOn(uint8_t valve) {
    Serial.print(millis());
    Serial.print(" - Valve ");
    Serial.print(valve);
    Serial.println(": ON");

    outputOn(valve);
    pulseSolenoidOpen();
}

void valveOff(uint8_t valve) {
    Serial.print(millis());
    Serial.print(" - Valve ");
    Serial.print(valve);
    Serial.println(": OFF");

    // pulse the soledonid BEFORE changing the shift register
    pulseSolenoidClosed();
    outputOff(valve);
}

void firstValve() {
    Serial.print(millis());
    Serial.println(" - First Valve (1): ON");
    CURRENT_VALVE = 1;

    firstOutput();
    pulseSolenoidOpen();
}

void nextValve() {
    Serial.print(millis());
    Serial.print(" - Valve ");
    Serial.print(CURRENT_VALVE);
    Serial.println(": OFF");

    pulseSolenoidClosed();
    CURRENT_VALVE++;

    Serial.print(millis());
    Serial.print(" - Next Valve ");
    Serial.print(CURRENT_VALVE);
    Serial.println(": ON");

    nextOutput();
    pulseSolenoidOpen();
}


void allValvesOff() {
    Serial.print(millis());
    Serial.println(" - All Valves OFF ");
    CURRENT_VALVE = 0;

    // Pulse all currently running solenoids to turn them off,
    // then disable the shift register outputs
    pulseSolenoidClosed();
    allOutputsOff();
}



void checkZoneSwitches() {
    if (digitalRead(SW_VALVE1_PIN) == LOW && SW_VALVE1 == false)
    {
        Serial.println("VALVE 1 FLIP SWITCHED ON");
        valveOn(1);
        SW_VALVE1 = true;
    }
    else if (digitalRead(SW_VALVE1_PIN) == HIGH && SW_VALVE1 == true)
    {
        Serial.println("VALVE 1 FLIP SWITCHED OFF");
        valveOff(1);
        SW_VALVE1 = false;
    }

    if (digitalRead(SW_VALVE2_PIN) == LOW && SW_VALVE2 == false)
    {
        Serial.println("VALVE 2 FLIP SWITCHED ON");
        valveOn(2);
        SW_VALVE2 = true;
    }
    else if (digitalRead(SW_VALVE2_PIN) == HIGH && SW_VALVE2 == true)
    {
        Serial.println("VALVE 2 FLIP SWITCHED OFF");
        valveOff(2);
        SW_VALVE2 = false;
    }

    if (digitalRead(SW_VALVE3_PIN) == LOW && SW_VALVE3 == false)
    {
        Serial.println("VALVE 3 FLIP SWITCHED ON");
        valveOn(3);
        SW_VALVE3 = true;
    }
    else if (digitalRead(SW_VALVE3_PIN) == HIGH && SW_VALVE3 == true)
    {
        Serial.println("VALVE 3 FLIP SWITCHED OFF");
        valveOff(3);
        SW_VALVE3 = false;
    }

    if (digitalRead(SW_VALVE4_PIN) == LOW && SW_VALVE4 == false)
    {
        Serial.println("VALVE 4 FLIP SWITCHED ON");
        valveOn(4);
        SW_VALVE4 = true;
    }
    else if (digitalRead(SW_VALVE4_PIN) == HIGH && SW_VALVE4 == true)
    {
        Serial.println("VALVE 4 FLIP SWITCHED OFF");
        valveOff(4);
        SW_VALVE4 = false;
    }

    if (digitalRead(SW_VALVE5_PIN) == LOW && SW_VALVE5 == false)
    {
        Serial.println("VALVE 5 FLIP SWITCHED ON");
        valveOn(5);
        SW_VALVE5 = true;
    }
    else if (digitalRead(SW_VALVE5_PIN) == HIGH && SW_VALVE5 == true)
    {
        Serial.println("VALVE 5 FLIP SWITCHED OFF");
        valveOff(5);
        SW_VALVE5 = false;
    }

    if (digitalRead(SW_VALVE6_PIN) == LOW && SW_VALVE6 == false)
    {
        Serial.println("VALVE 6 FLIP SWITCHED ON");
        valveOn(6);
        SW_VALVE6 = true;
    }
    else if (digitalRead(SW_VALVE6_PIN) == HIGH && SW_VALVE6 == true)
    {
        Serial.println("VALVE 6 FLIP SWITCHED OFF");
        valveOff(6);
        SW_VALVE6 = false;
    }

    if (digitalRead(SW_VALVE7_PIN) == LOW && SW_VALVE7 == false)
    {
        Serial.println("VALVE 7 FLIP SWITCHED ON");
        valveOn(7);
        SW_VALVE7 = true;
    }
    else if (digitalRead(SW_VALVE7_PIN) == HIGH && SW_VALVE7 == true)
    {
        Serial.println("VALVE 7 FLIP SWITCHED OFF");
        valveOff(7);
        SW_VALVE7 = false;
    }
}


void checkStartCancel() {
    if (!RUNNING)
    {
        //  All 3 consecutive reads must be HIGH for
        //  the button to register pressed.
        bool pressed = true;
        for (uint8_t x = 0; x < 3; x++)
        {
            if (digitalRead(START_BUTTON_PIN) == LOW)
            {
                pressed = false;
                break;
            }
            delay(5);
        }
        if (pressed)
        {
            // Start the Zones!
            ZONE_START_TIME = millis();

            Serial.print(ZONE_START_TIME);
            Serial.println(" - Start Button Pressed");
            RUNNING = true;
            firstValve();
            delay(5 * 1000);
            return;
        }
        delay(10);
    }
    else
    {
        // Look for cancelation press
        bool pressed = true;
        for (uint8_t x = 0; x < 3; x++)
        {
            if (digitalRead(START_BUTTON_PIN) == LOW)
            {
                pressed = false;
                break;
            }
            delay(5);
        }
        if (pressed)
        {
            // Emergency Stop
            Serial.print(millis());
            Serial.println(" - Cancel Button Pressed");
            RUNNING = false;
            allValvesOff();
            delay(5 * 1000);
            return;
        }
        delay(10);
    }
}


uint8_t getValveState(uint8_t valve) {
    if (RUNNING)
    {
        if (CURRENT_VALVE == valve)
            return 1;
    } else {
        if (SW_VALVE1 && valve == 1)
            return 1;
        if (SW_VALVE2 && valve == 2)
            return 1;
        if (SW_VALVE3 && valve == 3)
            return 1;
        if (SW_VALVE4 && valve == 4)
            return 1;
        if (SW_VALVE5 && valve == 5)
            return 1;
        if (SW_VALVE6 && valve == 6)
            return 1;
        if (SW_VALVE7 && valve == 7)
            return 1;
    }
    return 0;
}


// ----------------------------------------------------------------------------
// Setup and Main loops
// ----------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    Serial.println("####### ESP32 Sensor INIT #######");

    // Setup shift register for the relay board
    pinMode(DATA_PIN, OUTPUT);
    pinMode(LATCH_PIN, OUTPUT);
    pinMode(CLK_PIN, OUTPUT);
    pinMode(OPEN_PIN, OUTPUT);
    pinMode(CLOSE_PIN, OUTPUT);
    digitalWrite(DATA_PIN, LOW);
    digitalWrite(LATCH_PIN, LOW);
    digitalWrite(CLK_PIN, LOW);
    digitalWrite(OPEN_PIN, LOW);
    digitalWrite(CLOSE_PIN, LOW);
    allOutputsOff();

    pinMode(SW_VALVE1_PIN, INPUT);
    pinMode(SW_VALVE2_PIN, INPUT);
    pinMode(SW_VALVE3_PIN, INPUT);
    pinMode(SW_VALVE4_PIN, INPUT);
    pinMode(SW_VALVE5_PIN, INPUT);
    pinMode(SW_VALVE6_PIN, INPUT);
    pinMode(SW_VALVE7_PIN, INPUT);

    loadConfig(config);
    if (config.Magic != CONFIG_MAGIC)
    {
        askForSettings(config);
    }

    // wait to see if user wants to update settings
    delay(1000);
    reconfigureCheck();

    pinMode(WATER_METER_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(WATER_METER_PIN), waterMeterInterrupt, FALLING);

    // Setup the start/cancel button
    pinMode(START_BUTTON_PIN, INPUT);

    // Setup the water flow meter as an interrupt
    waterCounter = 0;
    waterDebounceTimer = millis();

    INFLUX_TIMER = millis();

    Serial.println("Internet Task Handler initiated");
    connectToWifi();
    Serial.print("Setting up influxdb connection...");

    influx.configure(config.InfluxDatabase.c_str(), config.InfluxHostname.c_str());
    influx.authorize(config.InfluxUser.c_str(), config.InfluxPassword.c_str());
    influx.addCertificate(ROOT_CERT);
    Serial.println("Done");

    Serial.println("Setup Complete");
}

void loop()
{
    //
    // Handle Button Presses
    //
    checkStartCancel();

    //
    // Check Zone Switches
    //
    if (!RUNNING)
    {
        checkZoneSwitches();
    }

    //
    // Handle Cycling through the zones when running
    //
    unsigned long now = millis();
    if (RUNNING && now % 60 == 0)
    {
        unsigned long elapsed_time = (now - ZONE_START_TIME) / 1000;
        switch (CURRENT_VALVE)
        {
        case 1:
            if (elapsed_time >= (config.Zone1Duration * 60))
                nextValve();
            break;

        case 2:
            if (elapsed_time >= (config.Zone2Duration * 60))
                nextValve();
            break;

        case 3:
            if (elapsed_time >= (config.Zone3Duration * 60))
                nextValve();
            break;

        case 4:
            if (elapsed_time >= (config.Zone4Duration * 60))
                nextValve();
            break;

        case 5:
            if (elapsed_time >= (config.Zone5Duration * 60))
                nextValve();
            break;

        case 6:
            if (elapsed_time >= (config.Zone6Duration * 60))
                nextValve();
            break;

        case 7:
            if (elapsed_time >= (config.DrainDuration))
            {
                allValvesOff();
                RUNNING = false;
            }
            break;

        default:
            Serial.println("RUNTIME ERROR: default case reached");
            allValvesOff();
            break;
        }
    }

    //
    // Report Data
    //
    if (millis() - INFLUX_TIMER > INFLUX_UPDATE_MILLISECONDS)
    {
        String tags = "";
        String fields = "";
        int v = getValveState(VALVE_TO_SEND);

        tags = "location=" + config.Location + ",sensor=" + config.Sensor + ",valve=" + String(VALVE_TO_SEND);
        fields = "value=" + String(v);
        sendDatapoint(RUNNING_MEASUREMENT, tags.c_str(), fields.c_str());

        if (VALVE_TO_SEND == 7) {
            INFLUX_TIMER = millis();
            VALVE_TO_SEND = 1;
        }
        VALVE_TO_SEND++;
    }

    // Run this every loop. It will send as necessary
    sendWaterMetric();
}
