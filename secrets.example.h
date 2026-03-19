#pragma once

// WLAN-Zugangsdaten
static const char* WIFI_SSID = "DEIN_WLAN_NAME";
static const char* WIFI_PASSWORD = "DEIN_WLAN_PASSWORT";

// Lokaler Hostname für mDNS: http://birdwatch.local
static const char* DEVICE_HOSTNAME = "birdwatch";

// Optionaler externer Klassifikationsdienst im lokalen Netz.
// Leer lassen, wenn die Vogelerkennung vorerst deaktiviert bleiben soll.
static const char* CLASSIFIER_ENDPOINT = "";

// Optionaler API-Schlüssel für den Klassifikationsdienst.
// Leer lassen, wenn nicht benötigt.
static const char* CLASSIFIER_API_KEY = "";
