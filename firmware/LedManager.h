#include <Adafruit_NeoPixel.h>

struct Color 
{
  int m_r, m_g, m_b;

  Color()
    : m_r(0), m_g(0), m_b(0)
  {

  }

  Color(int r, int g, int b)
    : m_r(r), m_g(g), m_b(b)
  {
  }
};

int softwareIdToPhysicalIdMap[4] = {0, 1, 2, 3};

// for the v1 board use this LED ordering
// int softwareIdToPhysicalIdMap[4] = {0, 2, 1, 3};

class LedManager
{
public:
  void Setup(int dataPin)
  {
    m_dataPin = dataPin;
    m_leds = Adafruit_NeoPixel(c_cLed, m_dataPin, NEO_GRB + NEO_KHZ800);

    m_leds.begin();
    ClearLEDs();
  }

  void UpdateLEDFromSerial()
  {
    int iLed = softwareIdToPhysicalIdMap[Serial.parseInt()];
    Color& color = m_rgColors[iLed];
    color.m_r = Serial.parseInt();
    color.m_g = Serial.parseInt();
    color.m_b = Serial.parseInt();
    
    char serialBuffer[255];
    sprintf(serialBuffer, "ID: %d, RGB: %d, %d, %d|", iLed, color.m_r, color.m_g, color.m_b);
    Serial.write(serialBuffer);

    ShowLeds();
  }

  void UpdateBrightnessFromSerial()
  {
    if(Serial.available() == 0)
    {
      return;
    }

    m_brightness = Serial.parseInt();

    // the input will have a trailing "n" that we can just drop
    Serial.read();

    ShowLeds();
  }

  void ClearLEDs()
  {
    for(int iLed = 0; iLed < c_cLed; iLed++)
    {
      m_rgColors[iLed] = Color(0,0,0);
    }

    for (int iLed = 0; iLed < c_cLed; iLed++)
    {
      SetLedColor(iLed);
    }

    m_leds.show();
  }

  // clears the physical LEDs, but doesn't wipe the state of the leds
  void TemporaryClearLEDs()
  {
    for (int iLed = 0; iLed < c_cLed; iLed++)
    {
      m_leds.setPixelColor(iLed, 0, 0, 0);
    }

    m_leds.setBrightness(m_brightness);
    m_leds.show();
  }

  void ShowLeds()
  {
    for (int iLed = 0; iLed < c_cLed; iLed++)
    {
      const Color& color = m_rgColors[iLed];
      m_leds.setPixelColor(iLed, color.m_r, color.m_g, color.m_b);
    }

    m_leds.setBrightness(m_brightness);
    m_leds.show();
  }

  void SetLedColor(int iLed)
  {
    const Color& color = m_rgColors[iLed];
    m_leds.setPixelColor(iLed, color.m_r, color.m_g, color.m_b);
    m_leds.setBrightness(m_brightness);
  }

private:
  static constexpr int c_cLed = 5;
  Color m_rgColors[c_cLed];
  int m_brightness = 32;
  Adafruit_NeoPixel m_leds;
  int m_dataPin = 9;
};