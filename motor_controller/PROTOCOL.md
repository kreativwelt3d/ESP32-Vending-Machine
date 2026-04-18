# UART Protocol

Der Haupt-ESP bleibt Master. Der Motor-ESP fuehrt nur niederwertige Aktoren aus:

- 24 Stepper-Treiber ueber gemeinsame `STEP`-Leitung und individuelle `ENABLE`
- Magnetschloss

## Transport

- Physikalisch: UART
- Baudrate: `115200`
- Verdrahtung:
  - Haupt-ESP `TX` -> Motor-ESP `RX`
  - Haupt-ESP `RX` <- Motor-ESP `TX`
  - gemeinsame `GND`
- Nachrichtenformat: ASCII, eine Zeile pro Frame, `\n` als Abschluss

## Frame-Format

Request:

```text
@<id> <CMD> [args...]
```

Response:

```text
@<id> OK [payload...]
@<id> ERR <code> [details...]
```

Asynchrone Events:

```text
!READY fw=motor-esp-v2
!BUSY <id> <task>
!DONE <id>
!LOCK open duration_ms=<n>
```

Die `id` kommt vom Haupt-ESP und erlaubt Korrelation von Request und Response.

## Befehle

`PING`

```text
@1 PING
@1 OK PONG
```

`INFO`

```text
@2 INFO
@2 OK fw=motor-esp-v2 motors=24 step_pin=4 lock_pin=21
```

`STATUS`

```text
@3 STATUS
@3 OK busy=0 door=closed
```

`OFF`

```text
@4 OFF
@4 OK motors=off
```

`TEST <motor> <steps> <pulse_us>`

- `motor`: 1..24
- `steps`: 1..12000
- `pulse_us`: 100..5000

```text
@5 TEST 3 200 800
@5 OK motors=3 steps=200 pulse_us=800
```

`RUN <mask> <steps> <pulse_us>`

- `mask` ist eine Bitmaske fuer Motoren 1..24
- Bit 0 = Motor 1
- Bit 1 = Motor 2
- ...
- Bit 23 = Motor 24

Beispiel: Motor 9 und 10 gemeinsam

```text
@6 RUN 0x300 3200 800
@6 OK motors=9,10 steps=3200 pulse_us=800
```

Motor-zu-Pin-Zuordnung auf dem Motor-ESP:

- Motor 1=`GPIO5`, Motor 2=`GPIO6`, Motor 3=`GPIO7`, Motor 4=`GPIO8`
- Motor 5=`GPIO9`, Motor 6=`GPIO10`, Motor 7=`GPIO11`, Motor 8=`GPIO12`
- Motor 9=`GPIO13`, Motor 10=`GPIO14`, Motor 11=`GPIO17`, Motor 12=`GPIO18`
- Motor 13=`GPIO1`, Motor 14=`GPIO2`, Motor 15=`GPIO38`, Motor 16=`GPIO39`
- Motor 17=`GPIO40`, Motor 18=`GPIO41`, Motor 19=`GPIO42`, Motor 20=`GPIO47`
- Motor 21=`GPIO48`, Motor 22=`GPIO19`, Motor 23=`GPIO20`, Motor 24=`GPIO3`

`LOCK [duration_ms]`

- Standard ohne Argument: `5000`

```text
@7 LOCK 5000
!BUSY 7 LOCK
!LOCK open duration_ms=5000
@7 OK lock=closed
!DONE 7
```

## Fehlercodes

- `BAD_FRAME`
- `BAD_CMD`
- `BAD_ARGS`
- `BUSY`
- `BAD_MOTOR`
- `RUN_FAIL`
- `TEST_FAIL`
- `LOCK_FAIL`

## Architekturentscheidung

Die Schachtlogik bleibt auf dem Haupt-ESP:

- Zuordnung Schacht -> Motor(e)
- Preislogik
- Bestandslogik
- Webserver
- WLAN

Der Motor-ESP kennt nur generische Aktionen:

- einen oder mehrere Motoren mit Schrittzahl/Pulsweite fahren
- Magnetschloss pulsen

Dadurch bleibt der Motor-ESP einfach und testbar.
