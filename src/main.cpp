#include <Arduino.h>
#include <NeoPixelBus.h>
#include <OneButton.h>
#include <ESP8266SAM.h>
#include <AudioOutputI2S.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>

// This file includes the declare statement for SECRET_SSID, SECRERT_PASS
#include <secrets.h>

const uint16_t PixelCount = 21;
const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;

NeoPixelBus<NeoRgbFeature, NeoEsp8266Uart0800KbpsMethod> strip(PixelCount);
OneButton button(D1, true);
AudioOutputI2S *audio = new AudioOutputI2S();
ESP8266SAM *sam = new ESP8266SAM;

bool state;

void click()
{
  state = !state;

  for(int j = 0; j < PixelCount + 1; j++)
    {
      for(int i = 0; i < PixelCount; i++)
      {
        if((state && i < j) || (!state && i >= j))
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

    if(state)
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
    delay(500);
  }

  // setup button
  button.attachClick(click);

  // setup audio
  audio->begin();
  audio->SetGain(0.5);    
}

void loop()
{
  // check for button press
  button.tick();
}
