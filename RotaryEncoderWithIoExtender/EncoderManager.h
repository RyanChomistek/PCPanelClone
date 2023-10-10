#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "Rotary.h"
#include "RotaryEncOverMCP.h"

#if defined(ESP32) || defined(ESP8266)
#define INTERRUPT_FUNC_ATTRIB IRAM_ATTR
#else
#define INTERRUPT_FUNC_ATTRIB  
#endif

/* variable to indicate that an interrupt has occured */
volatile boolean awakenByInterrupt = false;

class EncoderManager
{
public:
static void RotaryEncoderChanged(bool clockwise, int id) {
    Serial.println("Encoder " + String(id) + ": "
            + (clockwise ? String("clockwise") : String("counter-clock-wise")));
}

static void RotaryEncoderSwitchPressed(int id)
{
  Serial.println("pressed " + String(id));
}

void setup()
{
  pinMode(arduinoIntPin,INPUT);

  mcp.begin();      // use default address 0
  mcp.readINTCAPAB(); //read this so that the interrupt is cleared

  Serial.println("MCP23007 Interrupt Cleared");

  //initialize all rotary encoders

  //Setup interrupts, OR INTA, INTB together on both ports.
  //thus we will receive an interrupt if something happened on
  //port A or B with only a single INT connection.
  mcp.setupInterrupts(true,false,LOW);

  //Initialize input encoders (pin mode, interrupt)
  for(int i=0; i < numEncoders; i++) {
      rotaryEncoders[i].init();
  }

  attachInterrupt(digitalPinToInterrupt(arduinoIntPin), intCallBack, FALLING);

  Serial.println("MCP23007 done setup");
}

// The int handler will just signal that the int has happened
// we will do the work from the main loop.
static void INTERRUPT_FUNC_ATTRIB intCallBack() {
    awakenByInterrupt=true;
}

void checkInterrupt() {
    if(awakenByInterrupt) {
        // disable interrupts while handling them.
        detachInterrupt(digitalPinToInterrupt(arduinoIntPin));
        handleInterrupt();
        attachInterrupt(digitalPinToInterrupt(arduinoIntPin),intCallBack,FALLING);
    }
}

void handleInterrupt(){
    //Read the entire state when the interrupt occurred
    Serial.println("interrupt");

    //An interrupt occurred on some MCP object.
    //since all of them are ORed together, we don't
    //know exactly which one has fired.
    //just read all of them, pre-emptively.

    for(int j = 0; j < numMCPs; j++) {
        uint16_t gpioAB = allMCPs[j]->readINTCAPAB();
        // we need to read GPIOAB to clear the interrupt actually.
        volatile uint16_t dummy = allMCPs[j]->readGPIOAB();
        for (int i=0; i < numEncoders; i++) {
            //only feed this in the encoder if this
            //is coming from the correct MCP
            if(rotaryEncoders[i].getMCP() == allMCPs[j])
                rotaryEncoders[i].feedInput(gpioAB);
        }
    }

    cleanInterrupts();
}

// handy for interrupts triggered by buttons
// normally signal a few due to bouncing issues
void cleanInterrupts(){
#ifdef __AVR__
    EIFR=0x01;
#endif
    awakenByInterrupt=false;
}

void loop() {
    //Check if an interrupt has occurred and act on it
    checkInterrupt();
}

private:

  /* Our I2C MCP23017 GPIO expanders */
  Adafruit_MCP23017 mcp;

  //Array of pointers of all MCPs if there is more than one
  Adafruit_MCP23017* allMCPs[1] = { &mcp };
  const int numMCPs = (int)(sizeof(allMCPs) / sizeof(*allMCPs));

  /* the INT pin of the MCP can only be connected to
  * an interrupt capable pin on the Arduino, either
  * D3 or D2.
  * */
  byte arduinoIntPin = 3;

  /* Array of all rotary encoders and their pins */
  RotaryEncOverMCP rotaryEncoders[2] = {
      RotaryEncOverMCP(&mcp, 6 /*pinDT*/, 7 /*pinCLK*/, 5 /*pinSW*/, &RotaryEncoderChanged, &RotaryEncoderSwitchPressed, 0),
      RotaryEncOverMCP(&mcp, 3, 4, 2, &RotaryEncoderChanged, &RotaryEncoderSwitchPressed, 1),
  };
  const int numEncoders = (int)(sizeof(rotaryEncoders) / sizeof(*rotaryEncoders));
};
