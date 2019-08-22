# DeskControl

This is a program for controlling a standing desk with a LogicData controller (mine is of type LogicS-2-KTS-SIS, but it probably works with others too) with an ESP8266. The credit for reverse-engineering the protocol and some of the code goes to this post: https://www.mikrocontroller.net/topic/373579. Some other people have made a much cleaner implementation, and I intend to migrate the code to that at some point: https://github.com/mtfurlan/RoboDesk.

I have added WiFi and MQTT to the mix, allowing control of the desk remotely. You can control the height with the MQTT topic `deskcontrol/setheight`, and monitor the current height with `deskcontrol/currentheight`.

WiFi and MQTT credentials are saved in a seperate file. Here's an example:

````
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASS";
const char* mqtt_server = "MQTT_SERVER";

IPAddress ip(192, 168, 1, 2);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 1);
````