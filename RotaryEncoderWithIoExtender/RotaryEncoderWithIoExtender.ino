#include "EncoderManager.h"
EncoderManager s_encoderManager;

void setup(){
    Serial.begin(115200);
    s_encoderManager.setup();
}

void loop() {
    s_encoderManager.loop();
}