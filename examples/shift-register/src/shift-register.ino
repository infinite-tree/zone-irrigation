#include <Arduino.h>

#define DATA_PIN 12
#define CLK_PIN 14
#define LATCH_PIN 13

uint8_t SHIFT_VALUE = 255;


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

void outputOn(uint8_t valve) {
    bitClear(SHIFT_VALUE, 8-valve);
    switchOutput();
}

void outputOff(uint8_t valve) {
    bitSet(SHIFT_VALUE, 8-valve);
    switchOutput();
}

void allOutputsOff()
{
    SHIFT_VALUE = 255;
    switchOutput();
}


void firstOutput() {
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

void nextOutput() {
    // The first output already shifted one into the register (DATA_PIN, LOW)
    // This just bumps it forward by one
    digitalWrite(CLK_PIN, LOW);

    digitalWrite(LATCH_PIN, HIGH);
    digitalWrite(DATA_PIN, HIGH);

    delay(20);
    digitalWrite(CLK_PIN, HIGH);
    delay(20);
}



void setup() {
    Serial.begin(115200);
    Serial.println("####### ESP32 Sensor INIT #######");

    pinMode(DATA_PIN, OUTPUT);
    pinMode(LATCH_PIN, OUTPUT);
    pinMode(CLK_PIN, OUTPUT);
    digitalWrite(DATA_PIN, LOW);
    digitalWrite(LATCH_PIN, LOW);
    digitalWrite(CLK_PIN, LOW);

}


void loop() {
    Serial.println("ON cycle");
    for (int x=1; x<8;x++) {
        outputOn(x);
        delay(1500);
    }
    Serial.println("OFF cycle");
    delay(2000);
    for (int x = 1; x < 8; x++)
    {
        outputOff(x);
        delay(1500);
    }
    Serial.println("SHIFT forward cycle");
    firstOutput();
    delay(1500);
    for (int x=2; x<=7;x++) {
        nextOutput();
        delay(1500);
    }
    Serial.println("All outputs off");
    allOutputsOff();
    delay(1500);
    Serial.println("All done");
    delay(2000);
}