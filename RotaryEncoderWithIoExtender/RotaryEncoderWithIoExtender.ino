#include "EncoderManager.h"
#include "LedManager.h"
#include "Serialization.h"

EncoderManager s_encoderManager;
LedManager s_ledManager;

void setup(){
  Serial.begin(115200);
  s_encoderManager.setup();
  s_ledManager.Setup(9);

  Serial.print(OutputEventType::StartUp);
  Serial.print("\n");
  Serial.flush();
}

void loop() {
    s_encoderManager.loop();

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