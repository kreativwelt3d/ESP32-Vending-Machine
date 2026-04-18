# Verkaufsautomat Firmware (ESP32-S3)

Diese Firmware steuert den Haupt-ESP eines Verkaufsautomaten. Der ESP32-S3 übernimmt Anzeige, Tastenfeld, Münzannahme, SD-Karte, WLAN/Weboberfläche und die Kommunikation mit dem separaten Motor-ESP.

Aktuelle Firmware-Version im Code: `1.5.0`

---

## Funktionsumfang

- 4x4-Keypad für Bedienung und Service-Menü
- 16x2-LCD über I2C
- Erststart- und Service-Keypad-Setup zur Tasten-Zuordnung
- Münzprüfer mit frei konfigurierbaren Puls/Wert-Mappings
- SD-Karte für Kassenbuch
- WLAN-Konfiguration
- Webserver für Konfiguration und Status
- Kommunikation mit separatem Motor-ESP per UART
- Ansteuerung von bis zu 24 Motoren ueber den separaten Motor-ESP
- Persistente Speicherung über Preferences (NVS)

---

## Hardware & Verdrahtung

### Gesamte Pinbelegung Haupt-ESP

Alle aktuell in der Firmware belegten GPIOs:

| Funktion | Signal | GPIO |
|----------|--------|------|
| LCD I2C | SDA | `8` |
| LCD I2C | SCL | `9` |
| Keypad | Row 1 | `10` |
| Keypad | Row 2 | `11` |
| Keypad | Row 3 | `12` |
| Keypad | Row 4 | `13` |
| Keypad | Col 1 | `14` |
| Keypad | Col 2 | `15` |
| Keypad | Col 3 | `16` |
| Keypad | Col 4 | `17` |
| Münzprüfer | Pulse input | `18` |
| Motor-ESP UART | TX vom Haupt-ESP | `4` |
| Motor-ESP UART | RX zum Haupt-ESP | `5` |
| SD-Karte SPI | SCK / CLK | `39` |
| SD-Karte SPI | MOSI / DI | `40` |
| SD-Karte SPI | MISO / DO | `41` |
| SD-Karte SPI | CS / SS | `42` |

Nicht aufgeführte GPIOs sind in der aktuellen Hauptplatinen-Firmware nicht belegt.

### LCD

- Display: `16x2`
- I2C-Start im Code: `Wire.begin(8, 9)`
- Typische I2C-Adresse: `0x27`

Verdrahtung:

- `SDA -> GPIO 8`
- `SCL -> GPIO 9`
- `VCC -> 5V` oder `3.3V` je nach LCD-Backpack
- `GND -> GND`

### Keypad

Logische 4x4-Matrix:

|      | Col 1 | Col 2 | Col 3 | Col 4 |
|------|-------|-------|-------|-------|
| Row 1 | 1 | 2 | 3 | A |
| Row 2 | 4 | 5 | 6 | B |
| Row 3 | 7 | 8 | 9 | C |
| Row 4 | * | 0 | # | D |

Physische Verdrahtung:

- `Row 1 -> GPIO 10`
- `Row 2 -> GPIO 11`
- `Row 3 -> GPIO 12`
- `Row 4 -> GPIO 13`
- `Col 1 -> GPIO 14`
- `Col 2 -> GPIO 15`
- `Col 3 -> GPIO 16`
- `Col 4 -> GPIO 17`

Hinweise:

- Beim ersten Start ohne gespeicherte Tasten-Zuordnung erscheint automatisch ein Keypad-Setup auf dem LCD.
- Das Setup kann später erneut im Service-Menü über `Keypad Setup` gestartet werden.
- Dadurch kann die Firmware auch mit unterschiedlich reagierenden oder anders verdrahteten 4x4-Keypads angepasst werden.

### Münzprüfer

- Pulssignal-Eingang: `GPIO 18`
- Im Code als `INPUT_PULLUP` konfiguriert

Verdrahtung:

- `Signal / pulse out -> GPIO 18`
- `GND -> GND`

### SD-Karte

Die SD-Karte wird im SPI-Modus betrieben.

Aktuell funktionierende Verdrahtung:

- `CLK / SCK -> GPIO 39`
- `MOSI / DI -> GPIO 40`
- `MISO / DO -> GPIO 41`
- `CS / SS -> GPIO 42`
- `3V3 -> 3.3V`
- `GND -> GND`

Hinweise:

- Diese Belegung wurde erfolgreich getestet.
- Der testweise Pinblock `35/36/37/38` kann auf diesem Board zu Boot-Problemen führen.
- Die Firmware probiert mehrere SPI-Frequenzen bei der Initialisierung und meldet den SD-Status im seriellen Monitor.

### Motor-ESP UART

Der Haupt-ESP kommuniziert mit dem separaten Motor-ESP über UART:

- `TX Haupt-ESP -> GPIO 4`
- `RX Haupt-ESP -> GPIO 5`
- Baudrate: `115200`

Verdrahtung:

- `GPIO 4` des Haupt-ESP an `RX` des Motor-ESP
- `GPIO 5` des Haupt-ESP an `TX` des Motor-ESP
- `GND` beider ESPs verbinden

Weitere Details zur Motor-Platine stehen in [motor_controller/README.md](d:/Github/testfg/Vending_Machine/motor_controller/README.md).

### Motor-Zuordnung auf dem Motor-ESP

Die Hauptplatinen-Firmware adressiert aktuell `24` Motoren. Die Zuordnung auf dem Motor-ESP ist:

1. Motor 1 -> `GPIO5`
2. Motor 2 -> `GPIO6`
3. Motor 3 -> `GPIO7`
4. Motor 4 -> `GPIO8`
5. Motor 5 -> `GPIO9`
6. Motor 6 -> `GPIO10`
7. Motor 7 -> `GPIO11`
8. Motor 8 -> `GPIO12`
9. Motor 9 -> `GPIO13`
10. Motor 10 -> `GPIO14`
11. Motor 11 -> `GPIO17`
12. Motor 12 -> `GPIO18`
13. Motor 13 -> `GPIO1`
14. Motor 14 -> `GPIO2`
15. Motor 15 -> `GPIO38`
16. Motor 16 -> `GPIO39`
17. Motor 17 -> `GPIO40`
18. Motor 18 -> `GPIO41`
19. Motor 19 -> `GPIO42`
20. Motor 20 -> `GPIO47`
21. Motor 21 -> `GPIO48`
22. Motor 22 -> `GPIO19`
23. Motor 23 -> `GPIO20`
24. Motor 24 -> `GPIO3`

Hinweis:

- In der Weboberflaeche unter `Tests` wird pro Motor ebenfalls der zugehoerige `ENABLE`-GPIO des Motor-ESP angezeigt.

---

## Bedienkonzept

### Wichtige Tasten

- `A` = Menü nach oben / Zeichensatz zurück
- `B` = Menü nach unten / Zeichensatz weiter
- `C` = Zurück / Abbrechen
- `D` = Bestätigen / Enter
- `*` = Löschen / Backspace
- `#` = reguläres Eingabezeichen in Textfeldern

### Service-Menü öffnen

Im Normalmodus wird das Service-Menü über eine schnelle Kombination aus `*` und `#` geöffnet. Danach muss die Service-PIN eingegeben werden.

### Service-Menü

Aktuelle Menüpunkte:

1. `Info`
2. `WiFi`
3. `Admin PIN`
4. `Sprache`
5. `Keypad Setup`
6. `Tuer oeffnen`

---

## Weboberfläche

Der integrierte Webserver läuft auf Port `80`.

Wichtige Bereiche:

- Übersicht
- WiFi
- E-Mail
- SumUp
- Münzen
- Schächte
- Kassenbuch
- Tests

### SumUp-Konfiguration

Im Tab `SumUp` werden die Parameter für die Kartenzahlung hinterlegt:

- `Aktiv`
- `Server Basis-URL`
- `Bearer Token`
- `Machine ID`
- `Currency`
- `Polling Intervall`
- `Timeout`

Beispiel für die `Server Basis-URL`:

```text
https://sumup.kreativwelt3d.de/sumup/public
```

Hinweise:

- Die Basis-URL muss genau auf den öffentlich erreichbaren PHP-Bridge-Endpunkt zeigen.
- Der Bearer-Token schützt die Bridge-Endpunkte `/start` und `/status`.
- `Machine ID` muss mit dem Wert übereinstimmen, den die Bridge für dieselbe Maschine erwartet.

### Produkt- und Kartenzahlungsablauf

Die Produktauswahl am 4x4-Keypad erfolgt zweistufig:

- zuerst `Reihe 1-6`
- danach `Fach 1-8`

Beispiel:

- `1` dann `2` = Reihe 1, Fach 2

Wenn genügend Guthaben vorhanden ist:

- Das Produkt wird direkt ausgegeben.

Wenn Guthaben fehlt und SumUp korrekt konfiguriert ist:

- Das LCD zeigt `Kartenzahlung->A`
- mit `A` wird die Kartenzahlung für genau diesen Artikel gestartet
- das LCD zeigt anschließend `Terminal beachten`
- nach erfolgreicher Transaktion wird die Ware automatisch ausgegeben
- bei fehlgeschlagener oder abgebrochener Zahlung erfolgt keine Ausgabe

Wichtig:

- `A` startet im Normalmodus keine freie Betragsaufladung mehr.
- Die Kartenzahlung ist jetzt direkt an den ausgewählten Artikel gekoppelt.

### Münzkonfiguration

- Frei konfigurierbare Puls/Wert-Mappings
- Bis zu `20` Einträge
- Weboberfläche mit `Eintrag hinzufügen` und `Eintrag entfernen`

### Login

- Login mit Service-/Admin-PIN
- Session-Cookie: `ESPSESSIONID`

---

## Persistente Einstellungen (NVS)

Namespace: `vending`

Gespeichert werden unter anderem:

- Admin-PIN
- WLAN-Daten
- Sprache
- Währung
- Keypad-Zuordnung
- Münz-Mappings
- E-Mail-Konfiguration
- Schacht-Konfiguration

---

## Setup & Inbetriebnahme

1. Board `esp32-s3-devkitc-1-n16r8` in PlatformIO verwenden.
2. Firmware bauen und flashen.
3. Seriellen Monitor mit `115200` Baud öffnen.
4. Beim Erststart gegebenenfalls das Keypad-Setup auf dem LCD durchlaufen.
5. SD-Karte und WLAN prüfen.
6. Service-Menü und Weboberfläche für weitere Konfiguration verwenden.

---

## Ablauf im Hauptloop

Der `loop()` erledigt zyklisch:

1. UART-Verarbeitung zum Motor-ESP
2. Münzsignal-Erkennung
3. WLAN-Verbindungsaufbau und Reconnect
4. Auswertung laufender Pulsfolgen
5. Webserver-Clientbearbeitung
6. Keypad-Polling und Modus-abhängige Tastenverarbeitung

---

## Fehlersuche

### SD-Karte wird nicht erkannt

- Prüfen, ob wirklich folgende Verdrahtung verwendet wird:
  - `CLK -> GPIO 39`
  - `MOSI -> GPIO 40`
  - `MISO -> GPIO 41`
  - `CS -> GPIO 42`
- `3.3V` und `GND` prüfen
- Seriellen Monitor beobachten
- Andere testweise Pinblöcke nicht ohne Prüfung übernehmen

### Keypad reagiert falsch

- `Keypad Setup` im Service-Menü erneut ausführen
- Verdrahtung von Rows und Cols prüfen

### Kein Zugriff auf die Webseite

- WLAN-Verbindung prüfen
- IP-Adresse im Info-Screen oder seriellen Monitor nachsehen
- Browser im selben Netzwerk verwenden

---

## Sicherheit & Grenzen

- Authentifizierung erfolgt über PIN und Session-Cookie
- Kein HTTPS/TLS in der aktuellen Firmware
- Für produktive Umgebungen sollte ein zusätzliches Sicherheitskonzept ergänzt werden

---

## Hinweise

- Diese README beschreibt die Hauptplatinen-Firmware in `src/sketch.ino`.
- Die Motor-Platine hat eine eigene Firmware und eigene Dokumentation im Ordner `motor_controller`.
