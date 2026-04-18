# Bill of Materials (BOM)

Language:

- [Deutsch](#deutsch)
- [English](#english)

---

## Deutsch

Diese BOM beschreibt die wichtigsten Elektronik-Teile fuer das ESP32-Vending-Machine-Projekt.

Hinweis:

- Die unten aufgefuehrten Komponenten basieren auf der aktuell dokumentierten Hardware in diesem Repository.
- In der Spalte `Kauflink` koennen direkte Bezugsquellen eingetragen werden.
- Wo noch kein Link hinterlegt ist, steht vorerst `TODO`.

### Elektronik-Teile

| Bauteil | Menge | Beschreibung | Verwendung | Kauflink |
|---------|-------|--------------|------------|----------|
| ESP32-S3 Dev Board fuer Hauptsteuerung | 1 | Hauptcontroller fuer Display, Weboberflaeche, WLAN, SD-Karte, Muenzen und SumUp-Anbindung (Option N16R8 KIT A auswählen!)| Haupt-ESP | https://de.aliexpress.com/item/1005008790807170.html |
| ESP32-S3 Dev Board fuer Motorsteuerung | 1 | Separater Controller fuer Motoren und Magnetschloss (Option N16R8 KIT A auswählen!)| Motor-ESP | https://de.aliexpress.com/item/1005008790807170.html |
| 16x2 LCD mit I2C-Backpack | 1 | Anzeige fuer Menues, Status und Bedienung | Benutzeroberflaeche | https://de.aliexpress.com/item/1005007531187322.html |
| 4x4 Matrix-Keypad | 1 | Eingabe fuer Auswahl und Service-Menue | Benutzeroberflaeche | https://de.aliexpress.com/item/1005007728795501.html |
| Elektronischer Muenzpruefer | 1 | Gibt Pulse fuer erkannte Muenzen aus | Bezahlmodul | https://de.aliexpress.com/item/1005007423643042.html |
| MicroSD-Karte | 1 | Speicher fuer Kassenbuch und Daten | Speicher | Jede SD karte mit maximal 16Gb funktioniert |
| MicroSD-Kartenmodul oder integrierter SD-Anschluss | 1 | SPI-Anbindung der SD-Karte an den Haupt-ESP | Speicheranbindung | https://de.aliexpress.com/item/1005007211417449.html |
| DRV8825 Stepper-Treiber | 12 | Treiber fuer die einzelnen Ausgabemotoren | Motorsteuerung | https://de.aliexpress.com/item/1005009321177962.html |
| Stepper-Treiber Erweiterungsplatine | 12 | Zum stecken und verkabeln der DRV8825 Treiber (Die mit der schwarzen Platine kaufen!) | Motorsteuerung | https://de.aliexpress.com/item/1005006770144608.html |
| Schrittmotoren | 12 | Motoren fuer die Produktausgabe | Ausgabemechanik | https://de.aliexpress.com/item/1005003874936862.html |
| Magnetschloss / Solenoid Lock | 1 | Elektrische Verriegelung fuer die Tuer | Schloss | https://de.aliexpress.com/item/1005009789600010.html |
| SumUp Solo Terminal | 1 optional | Kartenzahlung ueber SumUp API Bridge | Kartenzahlung | https://www.sumup.com/de-de/solo-kartenlesegeraet/ |
| 12V Netzteil minimum 4A | 1 | Versorgung fuer Motoren und leistungsstaerkere Verbraucher | Stromversorgung | Bitte ein hochwertiges mit hohlstecker kaufen kaufen! |
| LM2596 DC DC Step Down Converter | 1 | Wandelt Versorgungsspannung fuer einzelne Baugruppen von 12 zu 5V | Stromversorgung | https://de.aliexpress.com/item/1005007003796615.html |
| 12V Relais Modul | 1 | Gibt den Magnetschlössern Spannung sobald diese es brauchen | Stromversorgung | https://de.aliexpress.com/item/1005007109343076.html |
| Anschlusskabel, Dupont-Kabel, Schraubklemmen | nach Bedarf | Verdrahtung aller Komponenten | Verkabelung | https://de.aliexpress.com/item/1005003219096948.html |
| Hohlstecker Buchse | nach Bedarf | Anschluss für das 12V Netzteil | Verkabelung | https://de.aliexpress.com/item/1005007870346260.html |

### Hinweise

- Die genaue Auswahl einzelner Module kann je nach verfuegbarer Hardware leicht variieren.
- In diesem Aufbau werden aktuell `12` DRV8825-Treiber und `12` Motoren genutzt, auch wenn die Firmware grundsaetzlich bis zu `24` Ausgaenge abbilden kann.
- Das SumUp-Terminal ist nur notwendig, wenn Kartenzahlung genutzt werden soll.
- Weitere technische Details findest du in [README.md](../README.md) und [motor_controller/README.md](../motor_controller/README.md).

---

## English

This BOM lists the main electronic parts used for the ESP32 Vending Machine project.

Note:

- The components below are based on the hardware currently documented in this repository.
- The `Purchase link` column is intended for direct buying links.
- If no link has been added yet, it is marked as `TODO`.

### Electronic parts

| Part | Qty | Description | Usage | Purchase link |
|------|-----|-------------|-------|---------------|
| ESP32-S3 dev board for main controller | 1 | Main controller for display, web interface, Wi-Fi, SD card, coin handling and SumUp integration | Main ESP | TODO |
| ESP32-S3 dev board for motor controller | 1 | Separate controller for motors and magnetic lock | Motor ESP | TODO |
| 16x2 LCD with I2C backpack | 1 | Display for menus, status and user interaction | User interface | TODO |
| 4x4 matrix keypad | 1 | Input device for selection and service menu | User interface | https://de.aliexpress.com/item/1005007728795501.html |
| Electronic coin acceptor | 1 | Outputs pulses for detected coins | Payment module | TODO |
| MicroSD card | 1 | Storage for cashbook and related data | Storage | TODO |
| MicroSD card module or integrated SD connector | 1 | SPI connection for the SD card on the main ESP | Storage interface | TODO |
| DRV8825 stepper drivers | 12 | Drivers for the individual dispensing motors | Motor control | TODO |
| Stepper motors | 12 | Motors for product dispensing | Dispensing mechanism | TODO |
| Magnetic lock / solenoid lock | 1 | Electric lock for the door | Lock | TODO |
| SumUp Solo terminal | 1 optional | Card payment through the SumUp API bridge | Card payment | TODO |
| 5V power supply | 1 | Power supply for logic and 5V components | Power | TODO |
| 12V power supply | 1 | Power supply for motors and higher-power loads | Power | TODO |
| DC-DC converter / voltage regulator | 1 optional | Converts supply voltage for individual modules if needed | Power | TODO |
| Wiring, Dupont cables, terminal blocks | as needed | Wiring for all components | Cabling | TODO |

### Notes

- The exact module choice may vary slightly depending on the hardware you use.
- This setup currently uses `12` DRV8825 drivers and `12` motors, even though the firmware can generally address up to `24` outputs.
- The SumUp terminal is only required if card payments are used.
- More technical details are available in [README.md](../README.md) and [motor_controller/README.md](../motor_controller/README.md).
