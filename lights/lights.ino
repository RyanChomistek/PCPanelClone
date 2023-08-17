const int startPin = 2;

const int pinsPerLight = 3;

struct Light
{
  int r, g, b;
};

const int numLights = 1;
Light lights[numLights];

int RedPin(int iLight)
{
  return iLight * pinsPerLight + startPin;
}

int GreenPin(int iLight)
{
  return iLight * pinsPerLight + 1 + startPin;
}

int BluePin(int iLight)
{
  return iLight * pinsPerLight + 2 + startPin;
}

enum InputEventType
{
  color = 0
};

void setup()
{
  for(int iLight = 0; iLight < numLights; iLight++)
  {
    pinMode(RedPin(iLight), OUTPUT);
    pinMode(GreenPin(iLight), OUTPUT);
    pinMode(BluePin(iLight), OUTPUT);
  }
  
  Serial.begin(9600);
}


void loop()
{
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
        break;
      }
        
    }
  }

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