/*

    Config.h

    Simple config loading and saving based (ripped off) from the examples in ArduinoJson
*/

#ifndef _Config_h
#define _Config_h

#include <Arduino.h>

/* Sample Config:
{
    "Magic": "######",
    "WifiSSID": "XXXXXXXXXXXXXXXXXXXX",
    "WifiPassword": "XXXXXXXXXXXXXXXXXX",
    "Location": "XXXXXXXXXXXX",
    "Sensor": "XXXXXXXXXXX",
    "InfluxHostname": "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
    "InfluxDatabase": "XXXXXXXXXXXX",
    "InfluxUser": "XXXXXXXXXXX",
    "InfluxPaddword": "XXXXXXXXXXXX",

    "Zone1Duration": 0,
    "Zone2Duration": 0,
    "Zone3Duration": 0,
    "Zone4Duration": 0,
    "Zone5Duration": 0,
    "Zone6Duration": 0,
    "DrainDuration": 0
}

Paste this into https://arduinojson.org/v6/assistant/ for every change
*/

#define CONFIG_FILE     "/config.json"
#define CONFIG_SIZE     597
#define CONFIG_MAGIC    "1337"

struct Config
{
    String Magic;
    String WifiSSID;
    String WifiPassword;

    String Location;
    String Sensor;

    String InfluxHostname;
    String InfluxDatabase;
    String InfluxUser;
    String InfluxPassword;

    int Zone1Duration;
    int Zone2Duration;
    int Zone3Duration;
    int Zone4Duration;
    int Zone5Duration;
    int Zone6Duration;
    int DrainDuration;
};


void loadConfig(Config &config);
void saveConfig(Config &config);
void printConfig(Config &config);
void askForPreferences(Config &config);
void askForSettings(Config &config);

#endif