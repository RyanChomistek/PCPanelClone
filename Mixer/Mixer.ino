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

const int encodersStartPin = 22;
const int CLK = 0;
const int DT = 1;
const int SW = 2;
const int NumDials = 4;
const int NumPinsPerDial = 3;
EncoderState ecoders[NumDials];

enum OutputEventType
{
  Button = 0,
  Dial = 1,
  StartUp = 2
};

const int lightStartPin = 2;

const int pinsPerLight = 3;

struct Light
{
  int r, g, b;
};

const int numLights = 4;
Light lights[numLights];

int RedPin(int iLight)
{
  return iLight * pinsPerLight + lightStartPin;
}

int GreenPin(int iLight)
{
  return iLight * pinsPerLight + 1 + lightStartPin;
}

int BluePin(int iLight)
{
  return iLight * pinsPerLight + 2 + lightStartPin;
}

enum InputEventType
{
  color = 0
};

int CLKPin(int iDial)
{
  return CLK + NumPinsPerDial*iDial + encodersStartPin;
}

int DTPin(int iDial)
{
  return DT + NumPinsPerDial*iDial + encodersStartPin;
}

int SWPin(int iDial)
{
  return SW + NumPinsPerDial*iDial + encodersStartPin;
}

void setup() {
        
  for(int iDial = 0; iDial < NumDials; iDial++)
  {
    pinMode(CLKPin(iDial), INPUT);
    pinMode(DTPin(iDial), INPUT);
    pinMode(SWPin(iDial), INPUT_PULLUP);
  }

  for(int iLight = 0; iLight < numLights; iLight++)
  {
    pinMode(RedPin(iLight), OUTPUT);
    pinMode(GreenPin(iLight), OUTPUT);
    pinMode(BluePin(iLight), OUTPUT);
  }

	// Setup Serial Monitor
	Serial.begin(9600);

  for(int iDial = 0; iDial < NumDials; iDial++)
  {
    ecoders[iDial].lastStateCLK = digitalRead(CLK);
  }

  for(int iLight = 0; iLight < numLights; iLight++)
  {
    pinMode(RedPin(iLight), OUTPUT);
    pinMode(GreenPin(iLight), OUTPUT);
    pinMode(BluePin(iLight), OUTPUT);
  }

  Serial.print(OutputEventType::StartUp);
  Serial.print(";");
  Serial.flush();
}

void loop() {
  ReadSerial();
  OutputLEDs();
  OutputEncoderData();
}

void ReadSerial()
{
  char outputBuffer[255];

  if (Serial.available() > 0) {
    InputEventType inputEvent = (InputEventType) Serial.parseInt();

    switch(inputEvent)
    {
      case InputEventType::color:
      {
        int iLight = Serial.parseInt();
        Light& light =  lights[iLight];
        light.r = Serial.parseInt();
        light.g = Serial.parseInt();
        light.b = Serial.parseInt();

        // the input will have a trailing ";" that we can just drop
        Serial.read();
        break;
      }
    }
  }
}

void OutputLEDs()
{
  for(int iLight = 0; iLight < numLights; iLight++)
  {
    showRGB(iLight);
  }
}

void showRGB(int iLight)
{
  Light& light = lights[iLight];
  analogWrite(RedPin(iLight), light.r);
  analogWrite(GreenPin(iLight), light.g);
  analogWrite(BluePin(iLight), light.b);  
}

void OutputEncoderData()
{
  char serialBuffer[255];

  for(int iDial = 0; iDial < NumDials; iDial++)
  {
    EncoderState& currentState = ecoders[iDial];

    // Read the current state of CLK
    currentState.currentStateCLK = digitalRead(CLKPin(iDial));

    // If last and current state of CLK are different, then pulse occurred
    // React to only 1 state change to avoid double count
    if (currentState.currentStateCLK != currentState.lastStateCLK && currentState.currentStateCLK == 1){

      // If the DT state is different than the CLK state then
      // the encoder is rotating CCW so decrement
      if (digitalRead(DTPin(iDial)) != currentState.currentStateCLK) {
        currentState.counter--;
        currentState.currentDir = Direction::CCW;
      } else {
        // Encoder is rotating CW so increment
        currentState.counter++;
        currentState.currentDir = Direction::CW;
      }
      
      sprintf(serialBuffer, "%d %d %d %d;", iDial, OutputEventType::Dial, currentState.currentDir, currentState.counter);
      Serial.write(serialBuffer);
    }

    // Remember last CLK state
    currentState.lastStateCLK = currentState.currentStateCLK;

    // Read the button state
    int btnState = digitalRead(SWPin(iDial));

    //If we detect LOW signal, button is pressed
    if (btnState == LOW) {
      //if 50ms have passed since last LOW pulse, it means that the
      //button has been pressed, released and pressed again
      if (millis() - currentState.lastButtonPress > 50) {
        sprintf(serialBuffer, "%d %d;", iDial, OutputEventType::Button);
        Serial.write(serialBuffer);
      }

      // Remember last button press event
      currentState.lastButtonPress = millis();
    }

    // Put in a slight delay to help debounce the reading
    delay(1);
  }
}