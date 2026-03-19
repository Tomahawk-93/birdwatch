# Birdwatch mit ESP32-Kamera

Dieses Projekt stellt eine Arduino-/C++-basierte Grundlage für einen lokalen Vogelbeobachter am Futterhaus bereit.

## Was dieses Projekt jetzt konkret macht

Der ESP32 mit OV3660-Kamera und TF-/microSD-Slot übernimmt folgende Aufgaben:

- Verbindung mit dem lokalen WLAN.
- Bereitstellung eines lokalen Livebilds im Browser.
- Erreichbarkeit per mDNS, z. B. über `http://birdwatch.local`.
- Deutsche, moderne und bewusst minimal gehaltene Weboberfläche.
- Manueller Schnappschuss direkt über die Webseite.
- Speicherung der Bilder auf der SD-Karte.
- Galerie der zuletzt gespeicherten Bilder direkt in der Webseite.
- Begrenzung der gespeicherten Bilder, damit die SD-Karte nicht mit zu vielen ähnlichen Aufnahmen vollläuft.

## Was aktuell bewusst deaktiviert bleibt

- **Vogelklassifikation:** bleibt standardmäßig deaktiviert.
- **Automatische Aufnahmeserien:** bleiben standardmäßig deaktiviert, damit ohne Bewegungserkennung nicht ständig sehr ähnliche Bilder gespeichert werden.
- **Zusätzliche Sensoren:** es ist kein PIR-Sensor oder anderer externer Sensor vorgesehen.
- **Batterie-/Solar-Themen:** nicht relevant, weil das System laut Zielbild dauerhaft an einem Netzteil betrieben wird.

## Wichtiger technischer Hinweis zur Vogelerkennung

Eine echte, zuverlässige **Vogelart-Erkennung direkt auf dem ESP32** ist in der Praxis mit einer OV3660-Kamera und den typischen RAM-/CPU-Grenzen nur sehr eingeschränkt sinnvoll. Live-Stream, WLAN, Webserver und Bildklassifikation gleichzeitig auf einem klassischen ESP32 sind schnell an der Stabilitätsgrenze.

Darum ist die Firmware so aufgebaut, dass sie lokal sofort nützlich ist, auch wenn die KI-Erkennung komplett deaktiviert bleibt:

1. Livebild im Browser,
2. manuelle Schnappschüsse,
3. Speicherung auf SD,
4. Galerie der letzten Bilder.

Die Firmware enthält weiterhin **optional** einen HTTP-Hook für einen externen lokalen Klassifikationsdienst, falls später doch noch ein Raspberry Pi oder Mini-PC für KI hinzukommt.

## Weboberfläche

### Verfügbare Endpunkte

- `/` – Startseite mit Livebild, Status, manuellem Schnappschuss und Galerie
- `/stream` – MJPEG-Live-Stream
- `/api/status` – JSON-Status für die Oberfläche
- `/api/gallery` – JSON-Liste der letzten Bilder
- `/capture` – manueller Schnappschuss per HTTP-POST
- `/capture-file?name=...` – Ausgabe eines gespeicherten JPEGs

### Funktionen in der Webseite

- Livebild
- Statusanzeige unter dem Video
- gut sichtbarer Button zum manuellen Auslösen eines Schnappschusses
- Galerie der letzten gespeicherten Bilder
- vollständig deutsche Oberfläche
- modernes, aber bewusst reduziertes Layout

## Umgang mit vielen Bildern

Ohne Bewegungserkennung oder echte Klassifikation wäre ein regelmäßiger Auto-Snapshot schnell zu viel des Guten. Deshalb gilt aktuell:

- automatische Speicherung ist standardmäßig **aus**,
- Bilder werden primär manuell ausgelöst,
- auf der SD werden nur die **neuesten 48 Bilder** behalten,
- ältere Bilder werden automatisch entfernt.

So entsteht keine unkontrollierte Flut fast identischer Aufnahmen vom gleichen Vogel.

## Ist Bewegungserkennung mit dem ESP32 möglich?

**Kurz gesagt: ja, eingeschränkt.**

Mit dem Kameramodul allein ist eine einfache Bewegungserkennung grundsätzlich möglich, zum Beispiel durch den Vergleich aufeinanderfolgender Bilder in niedriger Auflösung. Für ein stabiles, energiesparendes und fehlalarmarmes System ist das auf einem ESP32 aber deutlich anspruchsvoller als ein bloßer Live-Stream.

Für die aktuelle Version ist diese Funktion **noch nicht aktiviert**, weil der Fokus auf einem robusten lokalen Stream mit manuellen Schnappschüssen liegt.

## Sind Push-Benachrichtigungen aufs Handy über die Webseite möglich?

**Nur eingeschränkt und nicht zuverlässig rein lokal.**

Eine lokal gehostete Seite wie `birdwatch.local` kann im geöffneten Browser natürlich Status anzeigen oder regelmäßig pollen. **Echte Push-Benachrichtigungen** auf ein Handy benötigen in der Praxis aber meist zusätzliche Infrastruktur, z. B.:

- HTTPS,
- Service Worker,
- einen Push-Dienst oder ein Gateway,
- oder eine separate App / einen Messenger-Bot.

Für die aktuelle lokale Offline-/Heimnetz-Variante ist deshalb die pragmatische Antwort:

- **Live im Browser beobachten:** ja,
- **Status im Browser aktualisieren:** ja,
- **echte Handy-Pushs im Hintergrund nur über die Webseite:** eher nein, zumindest nicht sauber und plattformübergreifend ohne zusätzliche Dienste.

## Kameraauflösung und empfohlene Konfiguration

Der OV3660-Sensor unterstützt grundsätzlich hohe Auflösungen. Für dieses Projekt zählt aber nicht nur die Maximalauflösung, sondern die Stabilität des Gesamtsystems mit WLAN, MJPEG-Stream und Webserver.

### Praktische Empfehlung für dieses Futterhaus-Projekt

- **Live-Stream:** `VGA (640x480)`
- **JPEG-Qualität:** etwa `12`
- **Frame Buffer:** `2`, wenn PSRAM vorhanden ist
- **Auto-Speicherung:** zunächst deaktiviert

### Warum nicht maximale Auflösung?

Bei einem Vogelhaus bringt eine sehr hohe Auflösung auf dem ESP32 oft weniger als gedacht:

- geringere Bildrate,
- mehr Speicherbedarf,
- mehr Last auf WLAN und PSRAM,
- schlechtere Gesamtstabilität.

Für die Beobachtung von anfliegenden Vögeln ist deshalb ein **flüssiger VGA-Stream** meist sinnvoller als ein träger Hochauflösungs-Stream.

## Arduino-IDE Einrichtung

### 1. Board-Paket

In der Arduino-IDE das ESP32-Boardpaket von Espressif installieren.

### 2. Board wählen

Für viele Module mit Kamera und TF-Slot passt:

- **AI Thinker ESP32-CAM**

Falls dein Board eine andere Pinbelegung hat, muss die Kamera-Konfiguration in `birdwatch.ino` angepasst werden.

### 3. Zugangsdaten eintragen

```bash
cp secrets.example.h secrets.h
```

Danach `secrets.h` mit WLAN-Zugangsdaten befüllen.

### 4. Typische stabile Arduino-Einstellungen

- PSRAM: **Enabled**
- Partition Scheme: **Huge APP** oder ähnlich
- CPU Frequency: **240 MHz**
- Flash Frequency: Standard laut Board

## SD-Karten-Speicherung

Gespeicherte Bilder liegen unter:

- `/captures/capture-000001.jpg`
- `/captures/capture-000002.jpg`
- usw.

## Optionaler externer Klassifikationsdienst

Wenn `CLASSIFIER_ENDPOINT` leer bleibt, ist die Vogelerkennung deaktiviert.

Wenn später ein externer Dienst aktiviert werden soll, erwartet die Firmware eine JSON-Antwort in dieser Form:

```json
{
  "name": "Blaumeise",
  "confidence": 0.93
}
```

## Sinnvolle nächste Schritte für genau dieses Setup

- Gehäuse mit Wetterschutz und Sichtfenster für das Kameramodul
- sinnvolle Positionierung am Futterhaus für konstanten Bildausschnitt
- Test verschiedener Blickwinkel und Entfernungen
- optional später einfache Bewegungserkennung auf Basis der Kamera
- optional später externer KI-Dienst, falls die Vogelart-Erkennung wirklich gebraucht wird
