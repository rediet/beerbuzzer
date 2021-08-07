# beerbuzzer
IoT-Button for Beer-Notifications.


Technical details
------------------------------------------------------------
IoT Device: ESP8266 (WEMOS D1 mini)\
Developer-Environment: Platform IO (Visual Studio Code)

Platform: espressif8266\
Board: d1_mini\
Framework: Arduino

Sensors:
- Push Button with integrated LED pixel strip
- Speaker


Project setup
------------------------------------------------------------
The file "secrets.h" containing sensitive information like WiFi SSID and password
as well as the host and resource of the incoming webhook is not under source control.\
Below is a sample structure:

#define SECRET_SSID "your-wifi-ssid"\
#define SECRET_PASS "your-wifi-password"\
#define SECRET_HOST "jsonplaceholder.typicode.com"\
#define SECRET_RESOURCE "/todos/1"\
#define SECRET_SHA1 "B7 CB ... C4"
