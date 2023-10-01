const int PinActivityStart = 22;

const int pinCLK = 49;
const int PinDT = 50;
const int PinSW = 51;

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
  bool isActive = false;
};

const int NumDials = 4;
EncoderState ecoders[NumDials];

void setup() {
  
  for(int iDial = 0; iDial < NumDials; iDial++)
  {
    pinMode(PinActivityStart + iDial, INPUT);
  }

  pinMode(pinCLK, INPUT);
  pinMode(PinDT, INPUT);
  pinMode(PinSW, INPUT_PULLUP);

  // Setup Serial Monitor
	Serial.begin(9600);
}

void loop() {
  char serialBuffer[255];
  
  int bitCLK = digitalRead(pinCLK);
  int bitDT = digitalRead(PinDT);
  int bitSW = digitalRead(PinSW);

  int a0 = digitalRead(PinActivityStart + 0);
  // int a1 = digitalRead(PinActivityStart + 1);
  // int a2 = digitalRead(PinActivityStart + 2);
  // int a3 = digitalRead(PinActivityStart + 3);

  // if(a0 != 1 ||
  //     a1 != 1 ||
  //     a2 != 1 ||
  //     a3 != 1)
  // {
  //   sprintf(serialBuffer, "%d %d %d %d\n", 
  //     a0, 
  //     a1, 
  //     a2,
  //     a3);
  //   Serial.write(serialBuffer);
  // }

  sprintf(serialBuffer, "%d %d %d\n", bitCLK, bitDT, bitSW);
  Serial.write(serialBuffer);

  //for(int iDial = 0; iDial < NumDials; iDial++)
  int iDial = 3;
  {
    int bitActivity = digitalRead(PinActivityStart + iDial);
    EncoderState& currentState = ecoders[iDial];
    currentState.currentStateCLK = bitCLK;

    if(bitActivity != 1)
    {
      currentState.isActive = true;
      // sprintf(serialBuffer, "%d %d %d\n", PinActivityStart + iDial, iDial, bitActivity);
      // Serial.write(serialBuffer);
    }

    // if(bitActivity == 1)
    // {
    //   currentState.lastStateCLK = currentState.currentStateCLK;
    //   continue;
    // }

    // if (currentState.currentStateCLK != currentState.lastStateCLK && currentState.currentStateCLK == 1 && currentState.isActive) {
    //   // If the DT state is different than the CLK state then
    //   // the encoder is rotating CCW so decrement
    //   if (bitDT != currentState.currentStateCLK) {
    //     currentState.counter--;
    //     currentState.currentDir = Direction::CCW;
    //   } else {
    //     // Encoder is rotating CW so increment
    //     currentState.counter++;
    //     currentState.currentDir = Direction::CW;
    //   }
      
    //   sprintf(serialBuffer, "%d %d %d %d\n", currentState.isActive, iDial, currentState.currentDir, currentState.counter);
    //   Serial.write(serialBuffer);
    //   currentState.isActive = false;
    // }

    // // Remember last CLK state
    // currentState.lastStateCLK = currentState.currentStateCLK;
  }

  // Put in a slight delay to help debounce the reading
  delay(1);
}
