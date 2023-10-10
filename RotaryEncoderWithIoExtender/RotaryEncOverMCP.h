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

/* function pointer definition */
typedef void (*RotaryTurnFunc)(bool clockwise, int id);
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
      pinA(pinDT), pinB(pinCLK), pinSW(pinSW),
      turnFunc(turnFunc), m_switchPressedFunc(switchPressedFunc), id(id) {
    }

    /* Initialize object in the MCP */
    void init() {
        if(mcp != nullptr) {
            mcp->pinMode(pinA, INPUT);
            mcp->pullUp(pinA, 0); //disable pullup on this pin
            mcp->setupInterruptPin(pinA,CHANGE);
            mcp->pinMode(pinB, INPUT);
            mcp->pullUp(pinB, 0); //disable pullup on this pin
            mcp->setupInterruptPin(pinB,CHANGE);
            mcp->pinMode(pinSW, INPUT);
            mcp->pullUp(pinSW, 1); //enable pullup on this pin
            mcp->setupInterruptPin(pinSW,CHANGE);
        }
    }

    /* On an interrupt, can be called with the value of the GPIOAB register (or INTCAP) */
    void feedInput(uint16_t gpioAB) {
        uint8_t pinValA = bitRead(gpioAB, pinA);
        uint8_t pinValB = bitRead(gpioAB, pinB);
        uint8_t event = rot.process(pinValA, pinValB);
        if(event == DIR_CW || event == DIR_CCW) {
            //clock wise or counter-clock wise
            bool clockwise = event == DIR_CW;
            //Call into action function if registered
            if(turnFunc) {
                turnFunc(clockwise, id);
                return;
            }
        }

        uint8_t pinValSW = bitRead(gpioAB, pinSW);
        if(pinValSW == 0)
        {
          m_switchPressedFunc(id);
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
    uint8_t pinA = 0;
    uint8_t pinB = 0;           /* the pin numbers for output A and output B */
    uint8_t pinSW = 0;           /* the pin numbers for output A and output B */
    RotaryTurnFunc turnFunc = nullptr;  /* function pointer, will be called when there is an action happening */
    SwitchPressedFunc m_switchPressedFunc = nullptr;  
    int id = 0;                             /* optional ID for identification */

    bool m_fSwitchState = false;
};


#endif /* SRC_ROTARYENCOVERMCP_H_ */
