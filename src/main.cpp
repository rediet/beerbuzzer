#include <Arduino.h>
#include <AudioOutputI2S.h>
#include <ESP8266SAM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <NeoPixelBus.h>
#include <OneButton.h>
#include <secrets.h>

// constants
// secrets must be declared in a separate file <secrets.h>
const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;
const char *host = SECRET_HOST;
const char *resource = SECRET_RESOURCE;
const char fingerprint[] PROGMEM = SECRET_SHA1;

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
ESP8266WebServer server(80);

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

int nextMessage = 0;
int messageSize = 0;
int messageStart = 0;
String messages[50];

void logMessage(String message)
{
  messages[nextMessage] = message;
  nextMessage = (nextMessage + 1) % 50;
  if (messageSize < 50)
  {
    messageSize++;
  }
  else
  {
    messageStart = (messageStart + 1) % 50;
  }
}

void logTrace(String message)
{
  logMessage(String("[Trace] " + message));
}

void logError(String message)
{
  logMessage(String("[Error] " + message));
}

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

    logTrace("Connecting to " + String(host));
    int retry = 0;
    int maxRetries = 3;
    while ((!httpsClient.connect(host, 443)) && (retry < maxRetries))
    {
      delay(100);
      retry++;
    }

    if (retry == maxRetries)
    {
      logError("Connection failed");
      sam->Say(audio, "Connection failed.");
    }
    else
    {
      logTrace("Connected");
      sam->Say(audio, "Connected to server.");
    }

    // Do request
    String request = String("GET ") + resource + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "Connection: close\r\n\r\n";
    logTrace(String("Requesting ") + host + resource);
    logTrace(request);
    httpsClient.print(request);

    // Get headers (they usualy end by '\n\r\n')
    // so read until '\n' and break at '\r'
    while (httpsClient.connected())
    {
      String line = httpsClient.readStringUntil('\n');
      logTrace(line);
      if (line == "\r")
      {
        break;
      }
    }

    // Read result line by line
    String line;
    while (httpsClient.available())
    {
      line = httpsClient.readString();
      logTrace(line);
    }

    if (line.length() > 3)
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

void serverSendState()
{
  String log = "Last log entries:\n";
  for (int i = 0; i < messageSize; i++)
  {
    int index = (i + messageStart) % 50;
    log.concat(messages[index]);
    if (!messages[index].endsWith("\n"))
    {
      log.concat("\n");
    }
  }

  server.send(200, "text/plain", log);
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

  // setup webserver
  server.on("/", serverSendState);
  server.begin();

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
  button.tick();
  server.handleClient();

  // do not run other background tasks in standby
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
