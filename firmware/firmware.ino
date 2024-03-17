#include "EncoderManager.h"
#include "LedManager.h"
#include "Serialization.h"

EncoderManager s_encoderManager;
LedManager s_ledManager;
unsigned long timeLastHeartBeat = 0;

void setup(){
  Serial.begin(115200);
  s_encoderManager.setup();
  s_ledManager.Setup(9);

  Serial.print(static_cast<long int>(OutputEventType::StartUp));
  Serial.print("\n");
  Serial.flush();

  timeLastHeartBeat = millis();
}



void loop() {
  s_encoderManager.loop();

  if(!s_encoderManager.fAnyEncoderChanged && millis() - timeLastHeartBeat > 1000)
  {
    timeLastHeartBeat = millis();
    Serial.print(static_cast<long int>(OutputEventType::HeartBeat));
    Serial.print("\n");
    Serial.flush();
  }

  // handle serial input
  while (Serial.available() > 0) 
  {
    InputEventType inputEvent = (InputEventType) Serial.parseInt();
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
    }
  }
}