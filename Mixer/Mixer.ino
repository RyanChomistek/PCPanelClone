// Rotary Encoder Inputs
#define CLK 2
#define DT 3
#define SW 4

#define NumKnobs 4
#define NumPinsPerKnob 3

enum Direction
{
  CCW = 0,
  CW = 1
};

struct EncoderState
{
  int counter = 0;
  int currentStateCLK;
  int lastStateCLK;
  Direction currentDir;
  unsigned long lastButtonPress = 0;
};

enum EventType
{
  Button = 0,
  Dial = 1
};

EncoderState ecoders[4];

void setup() {
        
  for(int iKnob = 0; iKnob < NumKnobs; iKnob++)
  {
    // Set encoder pins as inputs
    pinMode(CLK + NumPinsPerKnob*iKnob,INPUT);
    pinMode(DT + NumPinsPerKnob*iKnob,INPUT);
    pinMode(SW + NumPinsPerKnob*iKnob, INPUT_PULLUP);
  }

	// Setup Serial Monitor
	Serial.begin(9600);

  for(int iKnob = 0; iKnob < NumKnobs; iKnob++)
  {
    ecoders[iKnob].lastStateCLK = digitalRead(CLK);
  }
}

void loop() {
  char serialBuffer[255];

  for(int iKnob = 0; iKnob < NumKnobs; iKnob++)
  {
    EncoderState& currentState = ecoders[iKnob];

    // Read the current state of CLK
    currentState.currentStateCLK = digitalRead(CLK + NumPinsPerKnob*iKnob);

    // If last and current state of CLK are different, then pulse occurred
    // React to only 1 state change to avoid double count
    if (currentState.currentStateCLK != currentState.lastStateCLK && currentState.currentStateCLK == 1){

      // If the DT state is different than the CLK state then
      // the encoder is rotating CCW so decrement
      if (digitalRead(DT + NumPinsPerKnob*iKnob) != currentState.currentStateCLK) {
        currentState.counter--;
        currentState.currentDir = Direction::CCW;
      } else {
        // Encoder is rotating CW so increment
        currentState.counter++;
        currentState.currentDir = Direction::CW;
      }
      
      sprintf(serialBuffer, "%d %d %d %d;", iKnob, EventType::Dial, currentState.currentDir, currentState.counter);
      Serial.write(serialBuffer);
    }

    // Remember last CLK state
    currentState.lastStateCLK = currentState.currentStateCLK;

    // Read the button state
    int btnState = digitalRead(SW + NumPinsPerKnob*iKnob);

    //If we detect LOW signal, button is pressed
    if (btnState == LOW) {
      //if 50ms have passed since last LOW pulse, it means that the
      //button has been pressed, released and pressed again
      if (millis() - currentState.lastButtonPress > 50) {
        sprintf(serialBuffer, "%d %d;", iKnob, EventType::Button);
        Serial.write(serialBuffer);
      }

      // Remember last button press event
      currentState.lastButtonPress = millis();
    }

    // Put in a slight delay to help debounce the reading
    delay(1);
  }

}