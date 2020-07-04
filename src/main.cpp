#include <Arduino.h>
#include <AudioOutputI2S.h>
#include <ESP8266SAM.h>
#include <ESP8266WiFi.h>
#include <NeoPixelBus.h>
#include <OneButton.h>
#include <WiFiClient.h>

// This file includes the declare statement for SECRET_SSID, SECRERT_PASS
#include <secrets.h>

// constants
const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;
const uint16_t PixelCount = 21;
bool LED_CENTER_DOT[21] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_INNER_RING[21] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_OUTER_RING[21] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
bool LED_ALL[21] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
bool LED_INNER_TOP[21] = {0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_OUTER_TOP[21] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1};

// sensors and output variables
NeoPixelBus<NeoRgbFeature, NeoEsp8266Uart0800KbpsMethod> strip(PixelCount);
OneButton button(D1, true);
AudioOutputI2S *audio = new AudioOutputI2S();
ESP8266SAM *sam = new ESP8266SAM;

// application states
bool buttonActive;

// Sets or adds all pixels according to a boolean array
void ledSetPixels(bool *pixels, bool add = false)
{
  for (int i = 0; i < PixelCount; i++)
  {
    if (pixels[i])
    {
      strip.SetPixelColor(i, RgbColor(255, 255, 255));
    }
    else if (!add)
    {
      strip.SetPixelColor(i, RgbColor(0));
    }
  }
}

// Resets all pixels
void ledUnsetPixels()
{
  for (int i = 0; i < PixelCount; i++)
  {
    strip.SetPixelColor(i, RgbColor(0));
  }
}

void animateCircle(bool reverse)
{
  for (int j = 0; j < PixelCount + 1; j++)
  {
    for (int i = 0; i < PixelCount; i++)
    {
      if ((!reverse && i < j) || (reverse && i >= j))
      {
        strip.SetPixelColor(i, RgbColor(255, 255, 255));
      }
      else
      {
        strip.SetPixelColor(i, RgbColor(0));
      }
    }

    strip.Show();
    delay(20);
  }
}

void animateBlink()
{
  int passes = 3;
  for (int j = 0; j < passes * 2; j++)
  {
    if (j % 2 == 0)
    {
      ledSetPixels(LED_ALL);
    }
    else
    {
      ledUnsetPixels();
    }

    strip.Show();
    delay(100);
  }
}

void animateWiFi(int duration)
{
  int waitdelay = (duration - 20) / 4;
  if (waitdelay < 20)
  {
    waitdelay = 20;
  }

  // animate top half from center to outside
  for (int j = 0; j < 3; j++)
  {
    ledSetPixels(LED_CENTER_DOT);
    if (j > 0)
    {
      ledSetPixels(LED_INNER_TOP, true);
    }
    if (j > 1)
    {
      ledSetPixels(LED_OUTER_TOP, true);
    }
    strip.Show();
    delay(waitdelay);
  }

  // wait one cicle before reset
  delay(waitdelay);

  ledUnsetPixels();
  strip.Show();
  delay(20);
}

void click()
{
  buttonActive = !buttonActive;
  animateCircle(!buttonActive);

  if (buttonActive)
  {
    sam->Say(audio, "The party is on.");
  }
}

void setup()
{
  // this resets all the neopixels to an off state
  strip.Begin();
  strip.Show();

  // setup wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    animateWiFi(500);
  }

  // setup button
  button.attachClick(click);

  // setup audio
  audio->begin();
  audio->SetGain(0.5);

  // singal setup complete
  animateBlink();
}

void loop()
{
  // check for button press
  button.tick();
}
