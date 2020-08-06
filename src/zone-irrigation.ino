#include <Arduino.h>
#include "analogComp.h"

#define START_BTN_PIN       2     // INT_0 is D2

#define WATER_METER_PIN     AIN0  //D6
#define WATER_METER_REF     AIN1  //D7

#define WATER_ANALOG_PIN    A3    // A3
#define WATER_ANALOG_REF    A4    // A4

#define SWITCH_1_PIN        3     // D3
#define SWITCH_2_PIN        8     // D4
#define SWITCH_3_PIN        9     // D5
#define SWITCH_4_PIN        10     // D8
#define SWITCH_5_PIN        11    // D9
#define SWITCH_6_PIN        4     // D10
#define SWITCH_7_PIN        5     // D11

#define SHIFT_DATA_PIN      A0     // A0
#define SHIFT_CLK_PIN       A1     // A1
#define SHIFT_LATCH_PIN     A2     // A2

#define VALVE_OPEN_PIN       12    // D12
#define VALVE_CLOSE_PIN      13    // D13

#define SOLENOID_PULSE_LENGTH   100     // milliseconds
#define WATER_DEBOUNCE          75      // milliseconds

#define START_BTN_DEBOUNCE      SOLENOID_PULSE_LENGTH + 10  // milliseconds


unsigned long WATER_COUNTER_TIMER  = 0;
unsigned long WATER_DEBOUNCE_TIMER = 0;
unsigned long START_DEBOUNCE_TIMER = 0;
volatile uint32_t WATER_COUNTER = 0;

// Everything off (inverted output)
uint8_t SHIFT_REGISTER = 255;
char START_STOP = 's';

bool PROGRAM_RUNNING = false;

bool VALVE_1_ON = false;
bool VALVE_2_ON = false;
bool VALVE_3_ON = false;
bool VALVE_4_ON = false;
bool VALVE_5_ON = false;
bool VALVE_6_ON = false;
bool VALVE_7_ON = false;


void debug(String msg) {
    Serial.print("D - ");
    Serial.println(msg);
}

void startCancelInterrupt() {
    // Interrupt for start/cancel btn
    unsigned long now = millis();
    if (now - START_DEBOUNCE_TIMER > START_BTN_DEBOUNCE) {
        START_DEBOUNCE_TIMER = now;
        START_STOP = 'S';
        if (PROGRAM_RUNNING) {
            PROGRAM_RUNNING = false;
        }
    }
}

void waterInterrupt() {
    unsigned long now = millis();
    if (now - WATER_DEBOUNCE_TIMER >= WATER_DEBOUNCE) {
        WATER_DEBOUNCE_TIMER = now;
        WATER_COUNTER++;
    }
}

void printWaterMeterValues() {
    int total = 0;
    int avg_value = 0;
    int avg_ref = 0;

    // Read the analog value from the hall effect sensor
    total = 0;
    for (int x =0; x< 16; x++) {
        total += analogRead(WATER_ANALOG_PIN);
    }
    avg_value = total/16;


    // Read the reference voltage that is being compared against
    total = 0;
    for (int x =0; x< 16; x++) {
        total += analogRead(WATER_ANALOG_REF);
    }
    avg_ref = total/16;
    Serial.print("D - CALIBRATION: ref: ");
    Serial.print(avg_ref);
    Serial.print(", meas: ");
    Serial.print(avg_value);

    Serial.print(", DIFF: ");
    Serial.print(avg_value - avg_ref);
    Serial.print(", cnt: ");
    Serial.println(WATER_COUNTER);    
}

// ----------------------------------------------------------------------------
// Shift Register operations
// ----------------------------------------------------------------------------

void switchOutput()
{
    digitalWrite(SHIFT_LATCH_PIN, HIGH);
    for (int x = 0; x < 8; x++)
    {
        uint8_t output = SHIFT_REGISTER & (1 << x);
        // debug("BIT: ");
        if (output)
        {
            digitalWrite(SHIFT_DATA_PIN, HIGH);
            // debug("1");
        }
        else
        {
            digitalWrite(SHIFT_DATA_PIN, LOW);
            // debug("0");
        }

        digitalWrite(SHIFT_CLK_PIN, LOW);
        delay(25);
        digitalWrite(SHIFT_CLK_PIN, HIGH);
        delay(25);
    }
    digitalWrite(SHIFT_CLK_PIN, LOW);
    digitalWrite(SHIFT_LATCH_PIN, LOW);
    // debug("");
}

void outputOn(uint8_t valve)
{
    bitClear(SHIFT_REGISTER, 8 - valve);
    switchOutput();
}

void outputOff(uint8_t valve)
{
    bitSet(SHIFT_REGISTER, 8 - valve);
    switchOutput();
}

void allOutputsOff()
{
    SHIFT_REGISTER = 255;
    switchOutput();
}

// ----------------------------------------------------------------------------
// Valve operations
// ----------------------------------------------------------------------------

void pulseSolenoidOpen()
{
    delay(100);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(VALVE_OPEN_PIN, HIGH);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(VALVE_OPEN_PIN, LOW);
    delay(SOLENOID_PULSE_LENGTH);
}

void pulseSolenoidClosed()
{
    delay(100);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(VALVE_CLOSE_PIN, HIGH);
    delay(SOLENOID_PULSE_LENGTH);
    digitalWrite(VALVE_CLOSE_PIN, LOW);
    delay(SOLENOID_PULSE_LENGTH);
}

void valveOn(uint8_t valve)
{
    debug("Valve " + String(valve) + ": ON");

    outputOn(valve);
    pulseSolenoidOpen();
}

void valveOff(uint8_t valve)
{
    debug("Valve " + String(valve) + ": OFF");

    // pulse the soledonid BEFORE changing the shift register
    pulseSolenoidClosed();
    outputOff(valve);
}

void allValvesOff()
{
    debug("ALL VALVES OFF");
    // Pulse all currently running solenoids to turn them off,
    // then disable the shift register outputs
    pulseSolenoidClosed();
    allOutputsOff();

    VALVE_1_ON = false;
    VALVE_2_ON = false;
    VALVE_3_ON = false;
    VALVE_4_ON = false;
    VALVE_5_ON = false;
    VALVE_6_ON = false;
    VALVE_7_ON = false;
}

bool handleValve(uint8_t valve, bool state)
{
    if (state == false) {
        debug("Controller - Valve " + String(valve) + " On");
        valveOn(valve);
        return true;
    }

    debug("Controller - Valve " + String(valve) + " Off");
    valveOff(valve);
    return false;
}

void printOpenValves() {
    // Print each valve that is on
    if (VALVE_1_ON)
        Serial.print('1');
    if (VALVE_2_ON)
        Serial.print('2');
    if (VALVE_3_ON)
        Serial.print('3');
    if (VALVE_4_ON)
        Serial.print('4');
    if (VALVE_5_ON)
        Serial.print('5');
    if (VALVE_6_ON)
        Serial.print('6');
    if (VALVE_7_ON)
        Serial.print('7');

    Serial.println();
}


void handleValveSwitches() {
    if (PROGRAM_RUNNING)
        return;

    if (digitalRead(SWITCH_1_PIN) == LOW && VALVE_1_ON == false) {
        debug("VALVE 1 FLIP SWITCHED ON");
        valveOn(1);
        VALVE_1_ON = true;
    }
    else if (digitalRead(SWITCH_1_PIN) == HIGH && VALVE_1_ON == true) {
        debug("VALVE 1 FLIP SWITCHED OFF");
        valveOff(1);
        VALVE_1_ON = false;
    }

    if (digitalRead(SWITCH_2_PIN) == LOW && VALVE_2_ON == false)
    {
        debug("VALVE 2 FLIP SWITCHED ON");
        valveOn(2);
        VALVE_2_ON = true;
    }
    else if (digitalRead(SWITCH_2_PIN) == HIGH && VALVE_2_ON == true)
    {
        debug("VALVE 2 FLIP SWITCHED OFF");
        valveOff(2);
        VALVE_2_ON = false;
    }

    if (digitalRead(SWITCH_3_PIN) == LOW && VALVE_3_ON == false)
    {
        debug("VALVE 3 FLIP SWITCHED ON");
        valveOn(3);
        VALVE_3_ON = true;
    }
    else if (digitalRead(SWITCH_3_PIN) == HIGH && VALVE_3_ON == true)
    {
        debug("VALVE 3 FLIP SWITCHED OFF");
        valveOff(3);
        VALVE_3_ON = false;
    }

    if (digitalRead(SWITCH_4_PIN) == LOW && VALVE_4_ON == false)
    {
        debug("VALVE 4 FLIP SWITCHED ON");
        valveOn(4);
        VALVE_4_ON = true;
    }
    else if (digitalRead(SWITCH_4_PIN) == HIGH && VALVE_4_ON == true)
    {
        debug("VALVE 4 FLIP SWITCHED OFF");
        valveOff(4);
        VALVE_4_ON = false;
    }

    if (digitalRead(SWITCH_5_PIN) == LOW && VALVE_5_ON == false)
    {
        debug("VALVE 5 FLIP SWITCHED ON");
        valveOn(5);
        VALVE_5_ON = true;
    }
    else if (digitalRead(SWITCH_5_PIN) == HIGH && VALVE_5_ON == true)
    {
        debug("VALVE 5 FLIP SWITCHED OFF");
        valveOff(5);
        VALVE_5_ON = false;
    }

    if (digitalRead(SWITCH_6_PIN) == LOW && VALVE_6_ON == false)
    {
        debug("VALVE 6 FLIP SWITCHED ON");
        valveOn(6);
        VALVE_6_ON = true;
    }
    else if (digitalRead(SWITCH_6_PIN) == HIGH && VALVE_6_ON == true)
    {
        debug("VALVE 6 FLIP SWITCHED OFF");
        valveOff(6);
        VALVE_6_ON = false;
    }

    if (digitalRead(SWITCH_7_PIN) == LOW && VALVE_7_ON == false)
    {
        debug("VALVE 7 FLIP SWITCHED ON");
        valveOn(7);
        VALVE_7_ON = true;
    }
    else if (digitalRead(SWITCH_7_PIN) == HIGH && VALVE_7_ON == true)
    {
        debug("D - VALVE 7 FLIP SWITCHED OFF");
        valveOff(7);
        VALVE_7_ON = false;
    }
}

void setup() {
    Serial.begin(57600);

    // Enable the switches
    pinMode(SWITCH_1_PIN, INPUT);
    pinMode(SWITCH_2_PIN, INPUT);
    pinMode(SWITCH_3_PIN, INPUT);
    pinMode(SWITCH_4_PIN, INPUT);
    pinMode(SWITCH_5_PIN, INPUT);
    pinMode(SWITCH_6_PIN, INPUT);
    pinMode(SWITCH_7_PIN, INPUT);

    // Setup the Valve drvier
    pinMode(VALVE_OPEN_PIN, OUTPUT);
    digitalWrite(VALVE_OPEN_PIN, LOW);
    pinMode(VALVE_CLOSE_PIN, OUTPUT);
    digitalWrite(VALVE_CLOSE_PIN, LOW);

    pinMode(SHIFT_DATA_PIN, OUTPUT);
    digitalWrite(SHIFT_DATA_PIN, LOW);
    pinMode(SHIFT_CLK_PIN, OUTPUT);
    digitalWrite(SHIFT_CLK_PIN, LOW);
    pinMode(SHIFT_LATCH_PIN, OUTPUT);
    digitalWrite(SHIFT_LATCH_PIN, LOW);

    // Setup the Start/cancel button
    pinMode(START_BTN_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(START_BTN_PIN), startCancelInterrupt, LOW);

    // Setup the Water meter
    analogComparator.setOn(WATER_METER_PIN, WATER_METER_REF);
    analogComparator.enableInterrupt(waterInterrupt, FALLING);
    pinMode(WATER_ANALOG_PIN, INPUT);
    pinMode(WATER_ANALOG_PIN, OUTPUT);

    // Init the timers
    unsigned long now = millis();
    WATER_COUNTER_TIMER = now;
    WATER_DEBOUNCE_TIMER = now;
    START_DEBOUNCE_TIMER = now;
    debug("STARTUP Complete");
}


void loop() {
    uint32_t water = 0;
    if (Serial.available() > 0) {
        char code = Serial.read();
        switch(code) {
            case 'C':
                printWaterMeterValues();
                break;

            case 'I':
                Serial.println('I');
                break;

            // Enter program mode
            case 'P':
                debug("Entering PROGRAM Mode");
                PROGRAM_RUNNING = true;
                allValvesOff();
                Serial.println('P');
                break;

            case 'p':
                debug("Leaving PROGRAM Mode");
                PROGRAM_RUNNING = false;
                Serial.println('p');
                break;

            // Status messages
            case 'S':
                Serial.println(START_STOP);
                START_STOP = 's';
                break;

            case 'V':
                printOpenValves();
                break;

            case 'W':
                noInterrupts();
                water = WATER_COUNTER;
                WATER_COUNTER = 0;
                interrupts();
                Serial.println(water);
                break;

            // Control valves
            case '1':
                VALVE_1_ON = handleValve(1, VALVE_1_ON);
                Serial.println("1");
                break;
            case '2':
                VALVE_2_ON = handleValve(2, VALVE_2_ON);
                Serial.println("2");
                break;
            case '3':
                VALVE_3_ON = handleValve(3, VALVE_3_ON);
                Serial.println("3");
                break;
            case '4':
                VALVE_4_ON = handleValve(4, VALVE_4_ON);
                Serial.println("4");
                break;
            case '5':
                VALVE_5_ON = handleValve(5, VALVE_5_ON);
                Serial.println("5");
                break;
            case '6':
                VALVE_6_ON = handleValve(6, VALVE_6_ON);
                Serial.println("6");
                break;
            case '7':
                VALVE_7_ON = handleValve(7, VALVE_7_ON);
                Serial.println("7");
                break;

            default:
                Serial.println('E');
                break;
        }
    }

    // Check Switches
    handleValveSwitches();
}
