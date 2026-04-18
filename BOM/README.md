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
- Wo noch kein direkter Produktlink vorhanden ist, steht ein kurzer Kaufhinweis.

### Elektronik-Teile

| Bauteil | Menge | Beschreibung | Verwendung | Kauflink |
|---------|-------|--------------|------------|----------|
| ESP32-S3 Dev Board fuer Hauptsteuerung | 1 | Hauptcontroller fuer Display, Weboberflaeche, WLAN, SD-Karte, Muenzen und SumUp-Anbindung. Beim Kauf die Option `N16R8 KIT A` auswaehlen. | Haupt-ESP | https://de.aliexpress.com/item/1005008790807170.html |
| ESP32-S3 Dev Board fuer Motorsteuerung | 1 | Separater Controller fuer Motoren und Magnetschloss. Beim Kauf die Option `N16R8 KIT A` auswaehlen. | Motor-ESP | https://de.aliexpress.com/item/1005008790807170.html |
| 16x2 LCD mit I2C-Backpack | 1 | Anzeige fuer Menues, Status und Bedienung | Benutzeroberflaeche | https://de.aliexpress.com/item/1005007531187322.html |
| 4x4 Matrix-Keypad | 1 | Eingabe fuer Auswahl und Service-Menue | Benutzeroberflaeche | https://de.aliexpress.com/item/1005007728795501.html |
| Elektronischer Muenzpruefer | 1 | Gibt Pulse fuer erkannte Muenzen aus | Bezahlmodul | https://de.aliexpress.com/item/1005007423643042.html |
| MicroSD-Karte | 1 | Speicher fuer Kassenbuch und Daten | Speicher | Jede SD-Karte mit maximal `16 GB` funktioniert. |
| MicroSD-Kartenmodul oder integrierter SD-Anschluss | 1 | SPI-Anbindung der SD-Karte an den Haupt-ESP | Speicheranbindung | https://de.aliexpress.com/item/1005007211417449.html |
| DRV8825 Stepper-Treiber | 12 | Treiber fuer die einzelnen Ausgabemotoren | Motorsteuerung | https://de.aliexpress.com/item/1005009321177962.html |
| Stepper-Treiber-Erweiterungsplatine | 12 | Zum Stecken und Verkabeln der DRV8825-Treiber. Bitte die Variante mit der schwarzen Platine kaufen. | Motorsteuerung | https://de.aliexpress.com/item/1005006770144608.html |
| Schrittmotoren | 12 | Motoren fuer die Produktausgabe | Ausgabemechanik | https://de.aliexpress.com/item/1005003874936862.html |
| Magnetschloss / Solenoid Lock | 1 | Elektrische Verriegelung fuer die Tuer | Schloss | https://de.aliexpress.com/item/1005009789600010.html |
| SumUp Solo Terminal | 1 optional | Kartenzahlung ueber die SumUp API Bridge | Kartenzahlung | https://www.sumup.com/de-de/solo-kartenlesegeraet/ |
| 12V Netzteil, mindestens 4A | 1 | Versorgung fuer Motoren und leistungsstaerkere Verbraucher | Stromversorgung | Bitte ein hochwertiges Netzteil mit Hohlstecker waehlen. |
| LM2596 DC-DC Step-Down-Converter | 1 | Wandelt die Versorgungsspannung einzelner Baugruppen von `12V` auf `5V` | Stromversorgung | https://de.aliexpress.com/item/1005007003796615.html |
| 12V Relais-Modul | 1 | Schaltet die Versorgung fuer das Magnetschloss, wenn es benoetigt wird | Stromversorgung | https://de.aliexpress.com/item/1005007109343076.html |
| Anschlusskabel, Dupont-Kabel, Schraubklemmen | nach Bedarf | Verdrahtung aller Komponenten | Verkabelung | https://de.aliexpress.com/item/1005003219096948.html |
| Hohlstecker-Buchse | nach Bedarf | Anschluss fuer das 12V-Netzteil | Verkabelung | https://de.aliexpress.com/item/1005007870346260.html |

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
- The `Purchase link` column can contain direct buying links or short buying notes.
- Where no direct product link is available, a short purchase note is used instead.

### Electronic parts

| Part | Qty | Description | Usage | Purchase link |
|------|-----|-------------|-------|---------------|
| ESP32-S3 dev board for main controller | 1 | Main controller for display, web interface, Wi-Fi, SD card, coin handling and SumUp integration. Select the `N16R8 KIT A` option when ordering. | Main ESP | https://de.aliexpress.com/item/1005008790807170.html |
| ESP32-S3 dev board for motor controller | 1 | Separate controller for motors and the magnetic lock. Select the `N16R8 KIT A` option when ordering. | Motor ESP | https://de.aliexpress.com/item/1005008790807170.html |
| 16x2 LCD with I2C backpack | 1 | Display for menus, status information and user interaction | User interface | https://de.aliexpress.com/item/1005007531187322.html |
| 4x4 matrix keypad | 1 | Input device for selection and service menu | User interface | https://de.aliexpress.com/item/1005007728795501.html |
| Electronic coin acceptor | 1 | Outputs pulses for detected coins | Payment module | https://de.aliexpress.com/item/1005007423643042.html |
| MicroSD card | 1 | Storage for the cashbook and related data | Storage | Any SD card up to `16 GB` works. |
| MicroSD card module or integrated SD connector | 1 | SPI connection for the SD card on the main ESP | Storage interface | https://de.aliexpress.com/item/1005007211417449.html |
| DRV8825 stepper drivers | 12 | Drivers for the individual dispensing motors | Motor control | https://de.aliexpress.com/item/1005009321177962.html |
| Stepper driver expansion board | 12 | Used to plug in and wire the DRV8825 drivers. Buy the version with the black PCB. | Motor control | https://de.aliexpress.com/item/1005006770144608.html |
| Stepper motors | 12 | Motors used for product dispensing | Dispensing mechanism | https://de.aliexpress.com/item/1005003874936862.html |
| Magnetic lock / solenoid lock | 1 | Electric lock for the door | Lock | https://de.aliexpress.com/item/1005009789600010.html |
| SumUp Solo terminal | 1 optional | Card payment through the SumUp API bridge | Card payment | https://www.sumup.com/de-de/solo-kartenlesegeraet/ |
| 12V power supply, at least 4A | 1 | Power supply for motors and other higher-power loads | Power | Please choose a high-quality power supply with a barrel plug. |
| LM2596 DC-DC step-down converter | 1 | Converts the supply voltage for individual modules from `12V` to `5V` | Power | https://de.aliexpress.com/item/1005007003796615.html |
| 12V relay module | 1 | Switches power to the magnetic lock when needed | Power | https://de.aliexpress.com/item/1005007109343076.html |
| Wiring, Dupont cables, terminal blocks | as needed | Wiring for all components | Cabling | https://de.aliexpress.com/item/1005003219096948.html |
| Barrel jack socket | as needed | Connection for the 12V power supply | Cabling | https://de.aliexpress.com/item/1005007870346260.html |

### Notes

- The exact choice of modules may vary slightly depending on the hardware you use.
- This setup currently uses `12` DRV8825 drivers and `12` motors, even though the firmware can generally address up to `24` outputs.
- The SumUp terminal is only required if card payments are used.
- More technical details are available in [README.md](../README.md) and [motor_controller/README.md](../motor_controller/README.md).
