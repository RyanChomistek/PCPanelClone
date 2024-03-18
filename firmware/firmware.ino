#include "EncoderManager.h"
#include "LedManager.h"
#include "Serialization.h"

EncoderManager s_encoderManager;
LedManager s_ledManager;
unsigned long timeLastHeartBeatSent = 0;
unsigned long timeLastInputRecieved = 0;

void setup(){
  Serial.begin(115200);
  s_encoderManager.setup();
  s_ledManager.Setup(9);

  Serial.print(static_cast<long int>(OutputEventType::StartUp));
  Serial.print("\n");
  Serial.flush();

  timeLastHeartBeatSent = millis();
  timeLastInputRecieved = millis();
}



void loop() {
  s_encoderManager.loop();

  if(!s_encoderManager.fAnyEncoderChanged && millis() - timeLastHeartBeatSent > 1000)
  {
    timeLastHeartBeatSent = millis();

    char serialBuffer[255];
    sprintf(serialBuffer, "%ld\n", OutputEventType::HeartBeat);
    Serial.write(serialBuffer);
  }

  if(millis() - timeLastInputRecieved > 5000)
  {
    s_ledManager.TemporaryClearLEDs();
    timeLastInputRecieved = millis();

    char serialBuffer[255];
    sprintf(serialBuffer, "clearing LEDs|");
    Serial.write(serialBuffer);
  }

  // handle serial input
  while (Serial.available() > 0) 
  {
    // subtract by '0' to convert ascii char to int
    char next = Serial.read();

    if(next == ' ' || next == '\n')
    {
      // deliniation chars just skip
      continue;
    }

    // all of the inputs start with a number so if its not that its probably a bug
    if(next < '0' || next > '9')
    {
      char serialBuffer[255];
      sprintf(serialBuffer, "ERROR: BAD CHAR %c|", next);
      Serial.write(serialBuffer);
      continue;
    }

    InputEventType inputEvent = (InputEventType) next - '0';

    // char serialBuffer[255];
    // sprintf(serialBuffer, "in: %d %u|", inputEvent, millis() - timeLastInputRecieved);
    // Serial.write(serialBuffer);

    switch(inputEvent)
    {
      case InputEventType::color:
      {
        s_ledManager.UpdateLEDFromSerial();
        break;
      }
      case InputEventType::brightness:
      {
        s_ledManager.UpdateBrightnessFromSerial();
        break;
      }
      case InputEventType::HeartBeat:
      {
        break;
      }
    }

    timeLastInputRecieved = millis();
  }
}