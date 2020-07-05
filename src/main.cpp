#include <Arduino.h>
#include <AudioOutputI2S.h>
#include <ESP8266SAM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <NeoPixelBus.h>
#include <OneButton.h>
#include <WiFiClient.h>
#include <secrets.h>

// constants
// secrets must be declared in a separate file <secrets.h>
const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;
const char *url = SECRET_URL;
const char *fingerprint = SECRET_SHA1;

const uint16_t PixelCount = 21;
bool LED_CENTER_DOT[21] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_INNER_RING[21] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_OUTER_RING[21] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
bool LED_ALL[21] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
bool LED_INNER_TOP[21] = {0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_OUTER_TOP[21] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1};
bool LED_QUARTER_1[21] = {0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_QUARTER_2[21] = {0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0};
bool LED_QUARTER_3[21] = {0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0};
bool LED_QUARTER_4[21] = {0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1};

const int STATE_READY = 0;
const int STATE_ERROR = 1;
const int STATE_STANDBY = 2;
const int STATE_PARTY = 3;

const int ERR_NO_WIFI = 1;

// sensors and output variables
NeoPixelBus<NeoRgbFeature, NeoEsp8266Uart0800KbpsMethod> strip(PixelCount);
OneButton button(D1, true);
AudioOutputI2S *audio = new AudioOutputI2S();
ESP8266SAM *sam = new ESP8266SAM;

// application states
int schedulerPass = 0;
int applicationState = 0;
int errorCode = 0;
bool buttonActive = false;
int longPressStage = 0;

unsigned long timeLongPressStart;
unsigned long timeLastWifiConnected;
unsigned long timeLastWifiCheck;
unsigned long timeFirstError;
unsigned long timeLastErrorAnimation;

// Sets or adds all pixels according to a boolean array (pixel mask)
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
  strip.ClearTo(RgbColor(0));
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

void animateWiFiConnect(int duration)
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

void animateWifiError()
{
  ledSetPixels(LED_CENTER_DOT);
  ledSetPixels(LED_INNER_TOP, true);
  ledSetPixels(LED_OUTER_TOP, true);
  strip.Show();
  delay(1000);

  ledUnsetPixels();
  strip.Show();
  delay(20);
}

void enterErrorState(int errorCode)
{
  errorCode = errorCode;

  if (applicationState != STATE_ERROR)
  {
    applicationState = STATE_ERROR;
    timeFirstError = millis();
    timeLastErrorAnimation = 0;
  }
}

void exitErrorState()
{
  if (applicationState != STATE_ERROR)
  {
    return;
  }

  applicationState = STATE_READY;
  errorCode = 0;
}

void handleErrorState()
{
  if (applicationState != STATE_ERROR || longPressStage > 0)
  {
    return;
  }

  unsigned long time = millis();

  // change into standby if blinking for more than 30 minutes
  if (time - timeFirstError >= 1800000)
  {
    applicationState = STATE_STANDBY;
    return;
  }

  // blink each 6s
  if (time - timeLastErrorAnimation >= 6000)
  {
    timeLastErrorAnimation = time;
    switch (errorCode)
    {
    case ERR_NO_WIFI:
      animateWifiError();
      break;
    }
  }
}

void checkWifiSignal()
{
  unsigned long time = millis();

  // only check Wifi each 2s
  if (time - timeLastWifiCheck < 2000)
  {
    return;
  }

  timeLastWifiCheck = time;
  if (WiFi.status() == WL_CONNECTED)
  {
    timeLastWifiConnected = time;
    exitErrorState();
  }
  else
  {
    // in party mode we do not care about Wifi...
    // we just dance all night long!
    if (applicationState != STATE_PARTY)
    {
      enterErrorState(ERR_NO_WIFI);
    }
  }
}

void click()
{
  if (applicationState == STATE_STANDBY)
  {
    ESP.reset();
  }
  else if (applicationState == STATE_ERROR)
  {
    return;
  }
  else if (applicationState == STATE_READY)
  {
    applicationState = STATE_PARTY;
    animateCircle(false);
    sam->Say(audio, "The party is on.");

    WiFiClientSecure httpsClient;
    httpsClient.setFingerprint(fingerprint);
    httpsClient.setTimeout(10000); // 10s
    delay(1000);

    sam->Say(audio, "Connecting.");
    int retry = 0;
    while ((!httpsClient.connect(url, 443)) && (retry < 5))
    {
      delay(100);
      retry++;
    }

    if (retry == 5)
    {
      sam->Say(audio, "Connection failed.");
    }
    else
    {
      sam->Say(audio, "Connected to server.");
    }

    // Get headers (they usualy end by '\n\r\n')
    // so read until '\n' and break at '\r'
    while (httpsClient.connected())
    {
      String line = httpsClient.readStringUntil('\n');
      if (line == "\r")
      {
        break;
      }
    }

    // Read result line by line
    String line;
    while (httpsClient.available())
    {
      line = httpsClient.readStringUntil('\n');
    }

    if (line == "}")
    {
      sam->Say(audio, "Success.");
    }
    else
    {
      sam->Say(audio, "Error.");
    }
  }
  else if (applicationState == STATE_PARTY)
  {
    applicationState = STATE_READY;
    animateCircle(true);
  }
}

void longPressStart()
{
  timeLongPressStart = millis();

  // advance to stage 1
  longPressStage = 1;
  ledSetPixels(LED_CENTER_DOT);
  strip.Show();
  delay(20);
}

void longPress()
{
  unsigned long longPressTimeDiff = millis() - timeLongPressStart;
  if (longPressTimeDiff >= 1000 && longPressStage < 2)
  {
    // advance to stage 2
    longPressStage = 2;
    ledSetPixels(LED_CENTER_DOT);
    ledSetPixels(LED_INNER_RING, true);
    strip.Show();
    delay(20);
  }
  else if (longPressTimeDiff >= 2000 && longPressStage < 3)
  {
    // advance to stage 3
    longPressStage = 3;
    ledSetPixels(LED_CENTER_DOT);
    ledSetPixels(LED_INNER_RING, true);
    ledSetPixels(LED_OUTER_RING, true);
    strip.Show();
    delay(20);
  }
  else if (longPressTimeDiff >= 3000 && longPressStage < 4)
  {
    // advance to stage 4 (= application reset)
    longPressStage = 4;

    ledUnsetPixels();
    strip.Show();
    delay(100);

    animateBlink();
  }
}

void longPressStop()
{
  bool reset = longPressStage == 4;
  longPressStage = 0;

  ledUnsetPixels();
  strip.Show();
  delay(20);

  if (reset)
  {
    ESP.restart();
  }
}

void setup()
{
  // setup wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // setup and reset all the neopixels to an off state
  strip.Begin();
  strip.Show();

  // setup button
  button.attachClick(click);
  button.attachLongPressStart(longPressStart);
  button.attachLongPressStop(longPressStop);
  button.attachDuringLongPress(longPress);

  // setup audio
  audio->begin();
  audio->SetGain(0.1);

  // check Wifi signal for 10s
  int numChecks = 0;
  bool connected = false;
  while (!connected)
  {
    connected = WiFi.status() == WL_CONNECTED;
    timeLastWifiCheck = millis();
    numChecks++;

    if (connected)
    {
      timeLastWifiConnected = timeLastWifiCheck;
    }
    else if (numChecks < 20)
    {
      animateWiFiConnect(500);
    }
    else
    {
      enterErrorState(ERR_NO_WIFI);
      break;
    }
  }

  // singal setup complete
  if (applicationState != STATE_ERROR)
  {
    applicationState = STATE_READY;
    delay(200);
    animateBlink();
  }
}

void loop()
{
  // check for button press
  button.tick();

  // the only thing to do in standby is a button press wakeup
  if (applicationState == STATE_STANDBY)
  {
    return;
  }

  // primitive scheduling
  schedulerPass = (schedulerPass + 1) % 2;
  switch (schedulerPass)
  {
  case 0:
    checkWifiSignal();
    break;
  case 1:
    handleErrorState();
    break;
  }
}
