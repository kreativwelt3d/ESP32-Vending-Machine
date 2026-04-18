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
| ESP32-S3 Dev Board fuer Hauptsteuerung | 1 | Hauptcontroller fuer Display, Weboberflaeche, WLAN, SD-Karte, Muenzen und SumUp-Anbindung | Haupt-ESP | TODO |
| ESP32-S3 Dev Board fuer Motorsteuerung | 1 | Separater Controller fuer Motoren und Magnetschloss | Motor-ESP | TODO |
| 16x2 LCD mit I2C-Backpack | 1 | Anzeige fuer Menues, Status und Bedienung | Benutzeroberflaeche | TODO |
| 4x4 Matrix-Keypad | 1 | Eingabe fuer Auswahl und Service-Menue | Benutzeroberflaeche | TODO |
| Elektronischer Muenzpruefer | 1 | Gibt Pulse fuer erkannte Muenzen aus | Bezahlmodul | TODO |
| MicroSD-Karte | 1 | Speicher fuer Kassenbuch und Daten | Speicher | TODO |
| MicroSD-Kartenmodul oder integrierter SD-Anschluss | 1 | SPI-Anbindung der SD-Karte an den Haupt-ESP | Speicheranbindung | TODO |
| A4988 Stepper-Treiber | 24 | Treiber fuer die einzelnen Ausgabemotoren | Motorsteuerung | TODO |
| Schrittmotoren | 24 | Motoren fuer die Produktausgabe | Ausgabemechanik | TODO |
| Magnetschloss / Solenoid Lock | 1 | Elektrische Verriegelung fuer die Tuer | Schloss | TODO |
| SumUp Solo Terminal | 1 optional | Kartenzahlung ueber SumUp API Bridge | Kartenzahlung | TODO |
| 5V Netzteil | 1 | Versorgung fuer Logik und 5V-Komponenten | Stromversorgung | TODO |
| 12V Netzteil | 1 | Versorgung fuer Motoren und leistungsstaerkere Verbraucher | Stromversorgung | TODO |
| DC-DC Wandler / Spannungswandler | 1 optional | Wandelt Versorgungsspannung fuer einzelne Baugruppen bei Bedarf | Stromversorgung | TODO |
| Anschlusskabel, Dupont-Kabel, Schraubklemmen | nach Bedarf | Verdrahtung aller Komponenten | Verkabelung | TODO |

### Hinweise

- Die genaue Auswahl einzelner Module kann je nach verfuegbarer Hardware leicht variieren.
- Fuer die Motorsektion sind `24` A4988-Treiber und `24` Motoren in der aktuellen Firmware-Dokumentation vorgesehen.
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
| 4x4 matrix keypad | 1 | Input device for selection and service menu | User interface | TODO |
| Electronic coin acceptor | 1 | Outputs pulses for detected coins | Payment module | TODO |
| MicroSD card | 1 | Storage for cashbook and related data | Storage | TODO |
| MicroSD card module or integrated SD connector | 1 | SPI connection for the SD card on the main ESP | Storage interface | TODO |
| A4988 stepper drivers | 24 | Drivers for the individual dispensing motors | Motor control | TODO |
| Stepper motors | 24 | Motors for product dispensing | Dispensing mechanism | TODO |
| Magnetic lock / solenoid lock | 1 | Electric lock for the door | Lock | TODO |
| SumUp Solo terminal | 1 optional | Card payment through the SumUp API bridge | Card payment | TODO |
| 5V power supply | 1 | Power supply for logic and 5V components | Power | TODO |
| 12V power supply | 1 | Power supply for motors and higher-power loads | Power | TODO |
| DC-DC converter / voltage regulator | 1 optional | Converts supply voltage for individual modules if needed | Power | TODO |
| Wiring, Dupont cables, terminal blocks | as needed | Wiring for all components | Cabling | TODO |

### Notes

- The exact module choice may vary slightly depending on the hardware you use.
- The motor section currently documents `24` A4988 drivers and `24` motors.
- The SumUp terminal is only required if card payments are used.
- More technical details are available in [README.md](../README.md) and [motor_controller/README.md](../motor_controller/README.md).
