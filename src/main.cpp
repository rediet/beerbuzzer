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
bool LED_NONE[21] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_CENTER_DOT[21] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_INNER_RING[21] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_OUTER_RING[21] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
bool LED_ALL[21] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
bool LED_INNER_TOP[21] = {0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_OUTER_TOP[21] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1};
bool LED_ALL_TOP[21] = {1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1};
bool LED_VOICE[21] = {1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0};
bool LED_QUARTER_1[21] = {1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
bool LED_QUARTER_2[21] = {1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0};
bool LED_QUARTER_3[21] = {1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0};
bool LED_QUARTER_4[21] = {1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1};

// Base states
const int STATE_READY = 0;
const int STATE_ERROR = 1;
// Higher states
const int STATE_PARTY = 2;
const int STATE_WAIT = 3;
const int STATE_SETTINGS = 4;
const int STATE_STANDBY = 5;

const int ERR_NO_WIFI = 1;

const int SETTINGS_EXIT = 0;
const int SETTINGS_VOICE = 1;
const int SETTINGS_IP = 2;
const int SETTINGS_CONNECTION_TEST = 3;

const char *WEBHOOK_RESPONSE_PARTY = "PARTY";
const char *WEBHOOK_RESPONSE_ANNOUNCED = "ANNOUNCED";
const char *WEBHOOK_RESPONSE_REFUSED = "REFUSED";
const char *WEBHOOK_RESPONSE_FAILED = "FAILED";

// sensors and output variables
NeoPixelBus<NeoRgbFeature, NeoEsp8266Uart0800KbpsMethod> strip(PixelCount);
OneButton button(D1, true);
AudioOutputI2S *audio = new AudioOutputI2S();
ESP8266SAM *sam = new ESP8266SAM;
ESP8266WebServer server(80);

// application objects
struct AnimationFrame
{
  bool *leds;
  void (*program)(void);
  unsigned long duration;
};

// application states
int applicationState = STATE_READY;
int applicationStatePrevious = -1;
int applicationErrorCode = 0;
int applicationSettingsCode = 0;
int longPressStage = 0;
bool connectionTest = false;
ESP8266SAM::SAMVoice voice = ESP8266SAM::VOICE_SAM;

unsigned long timeLongPressStart;
unsigned long timeLastWifiConnected;
unsigned long timeLastWifiCheck;
unsigned long timeLastStateChange;
unsigned long timeAnimationNext;
unsigned long timeAnimationPause;

String messageBuffer[50];
int messageHead = 0;
int messageNext = 0;
int messageSize = 0;

AnimationFrame animationBuffer[20];
int animationHead = 0;
int animationNext = 0;
int animationSize = 0;
int animationIndex = 0;
bool animationPaused = false;

/* LOGGING ------------------------------------------------------- */
void logMessage(String message)
{
  messageBuffer[messageNext] = message;
  messageNext = (messageNext + 1) % 50;
  if (messageSize < 50)
  {
    messageSize++;
  }
  else
  {
    messageHead = (messageHead + 1) % 50;
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

/* PIXEL HELPERS ------------------------------------------------- */
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

void ledShowPixels(bool *pixels)
{
  ledSetPixels(pixels);
  strip.Show();
}

void ledHidePixels()
{
  ledUnsetPixels();
  strip.Show();
}

/* BACKGROUND ANIMATIONS ----------------------------------------- */
void afterAddAnimationFrame()
{
  animationNext = (animationNext + 1) % 20;
  if (animationSize < 20)
  {
    animationSize++;
  }
  else
  {
    animationHead = (animationHead + 1) % 20;
  }

  // setup animation to start immediately at 'head'
  timeAnimationNext = millis();
  animationIndex = animationHead;
}

// adds an animation picture
void addAnimationFrame(bool *pixels, unsigned long duration)
{
  AnimationFrame frame;
  frame.leds = pixels;
  frame.duration = duration;
  animationBuffer[animationNext] = frame;
  afterAddAnimationFrame();
}

// adds an animation program
void addAnimationFrame(void (*program)(void), unsigned long duration)
{
  AnimationFrame frame;
  frame.program = program;
  frame.duration = duration;
  animationBuffer[animationNext] = frame;
  afterAddAnimationFrame();
}

void handleAnimation()
{
  if (animationSize == 0 || animationPaused)
  {
    return;
  }

  unsigned long time = millis();
  if (time < timeAnimationNext)
  {
    return;
  }

  AnimationFrame frame = animationBuffer[animationIndex];
  if (frame.leds != NULL)
  {
    ledShowPixels(frame.leds);
  }
  else
  {
    frame.program();
  }

  animationIndex = (animationIndex + 1) % animationSize;
  timeAnimationNext = timeAnimationNext + frame.duration;
  yield();
}

void clearAnimation()
{
  animationHead = 0;
  animationNext = 0;
  animationSize = 0;
  animationIndex = 0;
  animationPaused = false;

  ledHidePixels();
  delay(20);
}

void pauseAnimation()
{
  if (!animationPaused)
  {
    animationPaused = true;
    timeAnimationPause = millis();
  }
}

void resumeAnimation()
{
  if (animationPaused)
  {
    animationPaused = false;
    timeAnimationNext = timeAnimationNext + (millis() - timeAnimationPause);

    // repeat last image from before pause
    if (animationSize > 0)
    {
      int animationIndexRepeat = animationIndex - 1;
      if (animationIndexRepeat < 0)
      {
        animationIndexRepeat = animationSize - 1;
      }

      // show frame (only if static, i.e. not a program)
      AnimationFrame frame = animationBuffer[animationIndexRepeat];
      if (frame.leds != NULL)
      {
        ledShowPixels(frame.leds);
      }
    }
  }
}

/* REGULAR ANIMATIONS -------------------------------------------- */
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

void animateCircleForward()
{
  animateCircle(false);
}

void animateCircleBackward()
{
  animateCircle(true);
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

  ledHidePixels();
  delay(20);
}

void startAnimateWifiError()
{
  addAnimationFrame(LED_ALL_TOP, 1000);
  addAnimationFrame(LED_NONE, 6000);
}

/* INTERACTION --------------------------------------------------- */
void SamSayPartyStarted()
{
  switch (random(3))
  {
  case 0:
    sam->Say(audio, "The party is on.");
    break;
  case 1:
    sam->Say(audio, "Let's get the party started.");
    break;
  case 2:
    sam->Say(audio, "Oh my god. It's party time.");
    break;
  }
}

void SamSaySendingMessage()
{
  switch (random(3))
  {
  case 0:
    sam->Say(audio, "Time for disco music.");
    break;
  case 1:
    sam->Say(audio, "Connecting to universe.");
    break;
  case 2:
    sam->Say(audio, "Open the bottles.");
    break;
  }
}

void SamSayCheers()
{
  switch (random(5))
  {
  case 0:
    sam->Say(audio, "Hell yiaah.");
    break;
  case 1:
    sam->Say(audio, "Party.");
    break;
  case 2:
    sam->Say(audio, "Beer time.");
    break;
  case 3:
    sam->Say(audio, "Another one.");
    break;
  case 4:
    sam->Say(audio, "Cheers.");
    break;
  }
}

/* APPLICATION LOGIC --------------------------------------------- */
void enterApplicationState(int state)
{
  applicationStatePrevious = applicationState;
  applicationState = state;
  timeLastStateChange = millis();
}

void exitApplicationState()
{
  applicationState = applicationStatePrevious;
  timeLastStateChange = millis();
}

void enterErrorState(int errorCode)
{
  bool startAnimation = false;
  if (applicationState != STATE_ERROR)
  {
    applicationState = STATE_ERROR;
    applicationErrorCode = errorCode;
    timeLastStateChange = millis();
    startAnimation = true;
  }
  else if (applicationErrorCode != errorCode)
  {
    applicationErrorCode = errorCode;
    startAnimation = true;
  }

  if (startAnimation)
  {
    clearAnimation();
    startAnimateWifiError();
  }
}

void exitErrorState()
{
  if (applicationState != STATE_ERROR)
  {
    return;
  }

  applicationState = STATE_READY;
  applicationErrorCode = 0;
  timeLastStateChange = millis();
  clearAnimation();
}

void handleTimeouts()
{
  if (longPressStage > 0)
  {
    return;
  }

  unsigned int timeSinceStateChange = millis() - timeLastStateChange;

  // change into standby if showing errors for more than 30 minutes
  if (applicationState == STATE_ERROR && timeSinceStateChange >= 1800000)
  {
    enterApplicationState(STATE_STANDBY);
    clearAnimation();
    return;
  }
  // automatically exit timeout after 5 minutes
  else if (applicationState == STATE_WAIT && timeSinceStateChange >= 300000)
  {
    exitApplicationState();
    clearAnimation();
    return;
  }
  // exit party mode after 4 hours
  else if (applicationState == STATE_PARTY && timeSinceStateChange >= 14400000)
  {
    exitApplicationState();
    clearAnimation();
    return;
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
  else if (time - timeLastWifiConnected < 10000)
  {
    // try a few times before entering error state
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

// Calls the webhook and returns true if the application should stay in party mode.
// This method only returns false if the party is known to begin later. Errors in
// transmission do not prevent from partying.
bool callTeamsWebhook()
{
  WiFiClientSecure httpsClient;
  httpsClient.setFingerprint(fingerprint);
  // httpsClient.setInsecure(); // <- use for test purposes
  httpsClient.setTimeout(10000); // 10s

  // Connect to host (normally this step is fast)
  logTrace("Connecting to " + String(host));
  int retry = 0;
  int maxRetries = 3;
  while ((!httpsClient.connect(host, 443)) && (retry < maxRetries))
  {
    if (retry == 0)
    {
      ledShowPixels(LED_VOICE);
      sam->Say(audio, "The network is busy.");
      ledHidePixels();
    }
    else
    {
      delay(100);
    }
    retry++;
  }

  // Notify about result and abort if necessary
  if (retry == maxRetries)
  {
    logError("Connection failed. This may be due to a server downtime or an outdated SHA1 fingerprint.");

    if (connectionTest)
    {
      ledShowPixels(LED_VOICE);
      sam->Say(audio, "Connection test failed.");
      ledHidePixels();

      return false;
    }

    ledShowPixels(LED_VOICE);
    sam->Say(audio, "Connection failed.");
    ledHidePixels();
    delay(500);
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "Sorry guys.");
    ledHidePixels();
    delay(500);
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "You have to party alone.");
    ledHidePixels();
    delay(100);

    return true;
  }
  else if (connectionTest)
  {
    logTrace("Connection test successful");

    ledShowPixels(LED_VOICE);
    sam->Say(audio, "Connection test successful.");
    ledHidePixels();

    return false;
  }
  else
  {
    logTrace("Connected");

    ledShowPixels(LED_VOICE);
    SamSaySendingMessage();
    ledHidePixels();
  }

  // Do the actual request (this will take some time)
  String request = String("GET ") + resource + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n";
  logTrace(String("Requesting ") + host + resource);
  logTrace(request);
  httpsClient.print(request);

  // Read headers (they end with '\n\r\n' so read until '\n' and break at '\r')
  while (httpsClient.connected())
  {
    String line = httpsClient.readStringUntil('\n');
    logTrace(line);
    if (line == "\r")
    {
      break;
    }
  }

  // Read message body
  String line;
  while (httpsClient.available())
  {
    line = httpsClient.readString();
    logTrace(line);
  }

  if (line == NULL)
  {
    line = "";
  }

  // Verify response by index since the body may be enclosed (i.e '5\nPARTY\n0')
  if (line.indexOf(WEBHOOK_RESPONSE_PARTY) >= 0)
  {
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "O K. Let's hope they come.");
    ledHidePixels();

    return true;
  }
  else if (line.indexOf(WEBHOOK_RESPONSE_ANNOUNCED) >= 0)
  {
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "O K. In a few hours.");
    ledHidePixels();

    return false;
  }
  else if (line.indexOf(WEBHOOK_RESPONSE_REFUSED) >= 0)
  {
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "Oh. Its a bad time.");
    ledHidePixels();

    return false;
  }
  else
  {
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "That didn't go well.");
    ledHidePixels();
    delay(500);
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "I think nobody is coming.");
    ledHidePixels();

    return true;
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
    pauseAnimation();

    ledShowPixels(LED_VOICE);
    sam->Say(audio, "Sorry guys.");
    ledHidePixels();
    delay(500);
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "You have to party alone.");
    ledHidePixels();
    delay(500);
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "I have an error.");
    ledHidePixels();
    delay(500);

    resumeAnimation();
  }
  else if (applicationState == STATE_READY)
  {
    enterApplicationState(STATE_PARTY);
    animateCircleForward();
    SamSayPartyStarted();
    ledHidePixels();

    bool stayInPartyMode = callTeamsWebhook();
    bool wasConnectionTest = connectionTest;
    connectionTest = false;

    if (wasConnectionTest)
    {
      exitApplicationState();
      ledShowPixels(LED_ALL);
      animateCircleBackward();
    }
    else if (!stayInPartyMode)
    {
      exitApplicationState();
      ledShowPixels(LED_ALL);
      animateCircleBackward();

      // prevent spamming Teams for a few minutes
      enterApplicationState(STATE_WAIT);

      // setup background animation (delayed start)
      addAnimationFrame(LED_QUARTER_1, 2000);
      addAnimationFrame(LED_QUARTER_2, 2000);
      addAnimationFrame(LED_QUARTER_3, 2000);
      addAnimationFrame(LED_QUARTER_4, 2000);
      timeAnimationNext = millis() + 2000;
    }
    else
    {
      // setup background animation
      addAnimationFrame(LED_OUTER_RING, 6000);
      addAnimationFrame(LED_INNER_RING, 6000);
      addAnimationFrame(LED_CENTER_DOT, 6000);
      addAnimationFrame(animateBlink, 1000);
      addAnimationFrame(LED_OUTER_RING, 6000);
      addAnimationFrame(LED_CENTER_DOT, 6000);
      addAnimationFrame(LED_INNER_RING, 6000);
      addAnimationFrame(LED_ALL, 6000);
      addAnimationFrame(animateCircleBackward, 1000);
      addAnimationFrame(animateCircleForward, 6000);
      addAnimationFrame(LED_NONE, 1000);
      addAnimationFrame(LED_VOICE, 100);
      addAnimationFrame(LED_NONE, 100);
      addAnimationFrame(LED_VOICE, 100);
      addAnimationFrame(LED_NONE, 100);
      addAnimationFrame(LED_VOICE, 100);
      addAnimationFrame(LED_NONE, 1000);
    }
  }
  else if (applicationState == STATE_PARTY)
  {
    pauseAnimation();

    // Do some random fun stuff (exit using double click)
    ledShowPixels(LED_VOICE);
    SamSayCheers();
    ledHidePixels();
    delay(100);

    resumeAnimation();
  }
  else if (applicationState == STATE_WAIT)
  {
    pauseAnimation();

    // Prevent spamming and say some words instead (exit using double click)
    ledShowPixels(LED_VOICE);
    switch (random(5))
    {
    case 0:
      sam->Say(audio, "It's too early.");
      break;
    case 1:
      sam->Say(audio, "Not yet.");
      break;
    case 2:
      sam->Say(audio, "Maybee later.");
      break;
    case 3:
      sam->Say(audio, "Don't be impatient.");
      break;
    case 4:
      sam->Say(audio, "Please wait some time.");
      break;
    }
    ledHidePixels();
    delay(100);

    resumeAnimation();
  }
  else if (applicationState == STATE_SETTINGS)
  {
    // Navigate through settings
    applicationSettingsCode = (applicationSettingsCode + 1) % 4;
    if (applicationSettingsCode == SETTINGS_EXIT)
    {
      sam->Say(audio, "Exit.");
    }
    else if (applicationSettingsCode == SETTINGS_IP)
    {
      sam->Say(audio, "I P.");
    }
    else if (applicationSettingsCode == SETTINGS_VOICE)
    {
      sam->Say(audio, "Choose voice.");
    }
    else if (applicationSettingsCode == SETTINGS_CONNECTION_TEST)
    {
      sam->Say(audio, "Connection test.");
    }
  }
}

void doubleClick()
{
  if (applicationState == STATE_PARTY)
  {
    // exit party mode
    exitApplicationState();
    clearAnimation();

    ledShowPixels(LED_ALL);
    sam->Say(audio, "Byye bye. See you soon.");
    animateCircleBackward();
  }
  else if (applicationState == STATE_WAIT)
  {
    // someone is really tired of waiting...
    // exit waiting mode
    exitApplicationState();
    clearAnimation();

    delay(500); // delay for button noise
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "I told you. It's too early.");
    ledHidePixels();
    delay(500);
    ledShowPixels(LED_VOICE);
    sam->Say(audio, "But you seem to know better.");
    ledHidePixels();
  }
  else if (applicationState == STATE_READY ||
           applicationState == STATE_ERROR)
  {
    pauseAnimation();

    enterApplicationState(STATE_SETTINGS);
    ledShowPixels(LED_OUTER_RING);

    delay(500); // delay for button noise
    sam->Say(audio, "Settup.");
  }
  else if (applicationState == STATE_SETTINGS)
  {
    if (applicationSettingsCode == SETTINGS_EXIT)
    {
      // exit settings mode
      delay(500); // delay for button noise
      sam->Say(audio, "Leaving settup.");

      exitApplicationState();
      ledHidePixels();

      resumeAnimation();
    }
    else if (applicationSettingsCode == SETTINGS_IP)
    {
      // Read IP address
      delay(1000); // delay for button noise
      String ip = WiFi.localIP().toString() + ".";
      char ipString[16];
      ip.toCharArray(ipString, 16);
      sam->Say(audio, ipString);
    }
    else if (applicationSettingsCode == SETTINGS_VOICE)
    {
      voice = static_cast<ESP8266SAM::SAMVoice>((voice + 1) % 6);
      sam->SetVoice(voice);
      delay(500); // delay for button noise
      switch (voice)
      {
      case ESP8266SAM::VOICE_SAM:
        sam->Say(audio, "Sam.");
        break;
      case ESP8266SAM::VOICE_ELF:
        sam->Say(audio, "Elf.");
        break;
      case ESP8266SAM::VOICE_ROBOT:
        sam->Say(audio, "Robot.");
        break;
      case ESP8266SAM::VOICE_STUFFY:
        sam->Say(audio, "Stuffy.");
        break;
      case ESP8266SAM::VOICE_OLDLADY:
        sam->Say(audio, "Old lady.");
        break;
      case ESP8266SAM::VOICE_ET:
        sam->Say(audio, "E T.");
        break;
      }
      delay(500);
      sam->Say(audio, "The party is on.");
    }
    else if (applicationSettingsCode == SETTINGS_CONNECTION_TEST)
    {
      connectionTest = true;
      sam->Say(audio, "Test connection activated once.");
    }
  }
}

void longPressStart()
{
  timeLongPressStart = millis();
  pauseAnimation();

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

    ledHidePixels();
    delay(100);

    animateBlink();
  }
}

void longPressStop()
{
  bool reset = longPressStage == 4;
  longPressStage = 0;

  ledHidePixels();
  delay(20);

  if (reset)
  {
    ESP.restart();
  }
  else
  {
    resumeAnimation();
  }
}

void serverSendState()
{
  String log = "Last log entries:\n";
  for (int i = 0; i < messageSize; i++)
  {
    // index according circular buffer overflow
    int index = (i + messageHead) % messageSize;
    log.concat(messageBuffer[index]);
    if (!messageBuffer[index].endsWith("\n"))
    {
      log.concat("\n");
    }
  }

  server.send(200, "text/plain", log);
}

/* SETUP AND LOOP ------------------------------------------------ */
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
  button.attachDoubleClick(doubleClick);
  button.attachLongPressStart(longPressStart);
  button.attachLongPressStop(longPressStop);
  button.attachDuringLongPress(longPress);

  // setup audio
  audio->begin();
  audio->SetGain(0.8);

  // setup webserver
  server.on("/", serverSendState);
  server.begin();

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

  // complete setup
  randomSeed(millis());
  if (applicationState != STATE_ERROR)
  {
    animateCircleForward();
    delay(1000);
    ledHidePixels();
    delay(20);
  }
  else
  {
    timeAnimationNext = millis() + 3000;
  }
}

void loop()
{
  button.tick();
  server.handleClient();

  // do not run other background tasks in standby
  if (applicationState != STATE_STANDBY)
  {
    handleAnimation();
    handleTimeouts();
    checkWifiSignal();
  }
}
