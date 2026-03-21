You have to add a "secrets.h" header file for this to work. 

#pragma once

// WLAN-Zugangsdaten
static const char* WIFI_SSID = "YOUR_SSID";
static const char* WIFI_PASSWORD = "YOUR_PW";

// Lokaler Hostname für mDNS: http://birdwatch.local
static const char* DEVICE_HOSTNAME = "birdwatch";
