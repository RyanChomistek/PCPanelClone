#include <Adafruit_NeoPixel.h>

constexpr int c_cLed = 5;
constexpr int c_dataPin = 9;

Adafruit_NeoPixel leds = Adafruit_NeoPixel(c_cLed, c_dataPin, NEO_GRB + NEO_KHZ800);

struct Color 
{
  int m_r, m_g, m_b;
  int m_brightness;

  Color(int r, int g, int b, int brightness)
  {
    m_r = r;
    m_b = b;
    m_g = g;
    m_brightness = brightness;
  }
};

Color color(255,0,0,32);

void ClearLEDs()
{
  for (int iLED = 0; iLED < c_cLed; iLED++)
  {
    leds.setPixelColor(iLED, 0);
  }

  leds.show();
}

void setup() {
  // put your setup code here, to run once:
  leds.begin();
  ClearLEDs();
}

void loop() {
  for (int iLED = 0; iLED < c_cLed; iLED++)
  {
    leds.setPixelColor(iLED, color.m_r, color.m_g, color.m_b);
    leds.setBrightness(color.m_brightness);
  }
  leds.show();
}



