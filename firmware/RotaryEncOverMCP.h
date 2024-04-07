/*
 * RotaryEncOverMCP.h
 *
 *  Created on: 21.05.2018
 *      Author: Maxi
 */

#ifndef SRC_ROTARYENCOVERMCP_H_
#define SRC_ROTARYENCOVERMCP_H_

/* Describes new objects based on the Rotary and Adafruit MCP23017 library */
#include "Adafruit_MCP23017.h"
#include "Rotary.h"
#include "Serialization.h"

/* function pointer definition */
typedef void (*RotaryTurnFunc)(Direction direction, int id, int count);
typedef void (*SwitchPressedFunc)(int id);

/* We describe an object in which we instantiate a rotary encoder
 * over an I2C expander.
 * It holds the information:
 *  * to which MCP object it's connected
 *  * which pin it is connected to
 *  * what function to call when there is a change
 * */
class RotaryEncOverMCP {
public:
  RotaryEncOverMCP(
    Adafruit_MCP23017* mcp, byte pinDT, byte pinCLK, byte pinSW, 
    RotaryTurnFunc turnFunc, SwitchPressedFunc switchPressedFunc, int id = 0)
      : rot(pinDT, pinCLK), 
    mcp(mcp),
    pinDT(pinDT), pinCLK(pinCLK), pinSW(pinSW),
    turnFunc(turnFunc), m_switchPressedFunc(switchPressedFunc), id(id) {
  }

  /* Initialize object in the MCP */
  void init() {
    if(mcp != nullptr) {
      mcp->pinMode(pinDT, INPUT);
      mcp->pullUp(pinDT, 0); //disable pullup on this pin
      mcp->setupInterruptPin(pinDT, CHANGE);

      mcp->pinMode(pinCLK, INPUT);
      mcp->pullUp(pinCLK, 0); //disable pullup on this pin
      mcp->setupInterruptPin(pinCLK, CHANGE);
      
      mcp->pinMode(pinSW, INPUT);
      mcp->pullUp(pinSW, 1); //enable pullup on this pin
      mcp->setupInterruptPin(pinSW, CHANGE);
    }
  }

  /* On an interrupt, can be called with the value of the GPIOAB register (or INTCAP) */
  void feedInput(uint16_t gpioAB) {
    uint8_t pinValDT = bitRead(gpioAB, pinDT);
    uint8_t pinValCLK = bitRead(gpioAB, pinCLK);
    uint8_t event = rot.process(pinValDT, pinValCLK);

    if(event == DIR_CW || event == DIR_CCW) {
      //clock wise or counter-clock wise
      bool clockwise = event == DIR_CW;
      Direction direction = clockwise ? Direction::CW : Direction::CCW;

      m_cnt += clockwise ? 1 : -1;

      //Call into action function if registered
      if(turnFunc) {
        turnFunc(direction, id, m_cnt);
        return;
      }
    }

    uint8_t pinValSW = bitRead(gpioAB, pinSW);

    if ((millis() - lastDebounceTime) > debounceDelay
      && pinValSW == 0) 
    {
      //char serialBuffer[255];
      //sprintf(serialBuffer, "switch pressed %lu|", millis() - lastDebounceTime);
      //Serial.write(serialBuffer);
      m_switchPressedFunc(id);
      lastDebounceTime = millis();
    }
  }

  /* Poll the encoder. Will cause an I2C transfer. */
  void poll() {
      if(mcp != nullptr) {
          feedInput(mcp->readGPIOAB());
      }
  }

  Adafruit_MCP23017* getMCP() {
      return mcp;
  }

  int getID() {
      return id;
  }

private:
  Rotary rot;                         /* the rotary object which will be created*/
  Adafruit_MCP23017* mcp = nullptr;   /* pointer the I2C GPIO expander it's connected to */
  uint8_t pinDT = 0;
  uint8_t pinCLK = 0;           /* the pin numbers for output A and output B */
  uint8_t pinSW = 0;           /* the pin numbers for output A and output B */
  RotaryTurnFunc turnFunc = nullptr;  /* function pointer, will be called when there is an action happening */
  SwitchPressedFunc m_switchPressedFunc = nullptr;  
  int id = 0;                             /* optional ID for identification */
  int m_cnt = 0;

  bool m_fSwitchState = false;

  unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
  unsigned long debounceDelay = 150;    // the debounce time; increase if the output flickers
};


#endif /* SRC_ROTARYENCOVERMCP_H_ */
