# Classroom Environment Monitor

An IoT system that measures classroom conditions — temperature, humidity, light and
sound level — and pairs those objective readings with anonymous human feedback, so
decisions about the learning environment can be based on evidence rather than guesswork.

An ESP32-S3 node samples its sensors and publishes JSON telemetry over MQTT. A local
Docker stack (Mosquitto → Node-RED → InfluxDB → Grafana) validates, stores and
visualises the stream.

## Documentation

**[Read the full documentation](https://reyvanair.github.io/CR_Monitor/)**

Five parts, covering the whole build:

| | |
|---|---|
| [Overview](https://reyvanair.github.io/CR_Monitor/) | Architecture, what it measures, project stages |
| [Pins & wiring](https://reyvanair.github.io/CR_Monitor/hardware.html) | Full GPIO map, sensors, display shield |
| [Firmware](https://reyvanair.github.io/CR_Monitor/firmware.html) | Boot order, sampling loop, sound maths, alarms |
| [MQTT contract](https://reyvanair.github.io/CR_Monitor/mqtt.html) | Topics, payload schema, retained state |
| [Build & bring-up](https://reyvanair.github.io/CR_Monitor/setup.html) | Toolchain, flashing, calibration, backend |

The same pages are in [`docs/`](docs/) and open offline straight from the filesystem.

## Hardware

| Part | Function | Connection |
|---|---|---|
| ESP32-S3 DevKitC-1 (N16R8) | Controller | — |
| SHT3x | Temperature + humidity | I²C `0x44` — SDA `GPIO8`, SCL `GPIO9` |
| BH1750 | Illuminance (lux) | I²C `0x23` — same bus |
| MAX4466 | Sound level (dB) | `GPIO5` (ADC1), **3.3 V only** |
| Piezo buzzer | Threshold alert | `GPIO7` |

The mic must stay on an ADC1 pin — ADC2 stops working whenever Wi-Fi is on. Full
wiring notes, rationale and the history of dropped parts (DHT22, the dead I²C LCD)
are in [`CR_Monitor_NOTES.txt`](CR_Monitor_NOTES.txt).

## Privacy

Sound is captured as a **decibel level only**. No audio is recorded, buffered or
transmitted. Occupancy is likewise reported as a bucket (Unoccupied / Low / Medium /
High) rather than a headcount. Both are deliberate design choices, not limitations.

## Repository layout

```
CR_Monitor/CR_Monitor.ino   ESP32-S3 sketch — sensors, Wi-Fi, MQTT
CR_Monitor_NOTES.txt        Design notes: wiring, decisions, the "why"
SETUP_GUIDE.txt             Step-by-step build and deployment guide
docs/                       Project documentation (HTML, also served as a site)
docker-compose.yml          Mosquitto + InfluxDB + Node-RED + Grafana
mosquitto.conf              Broker config (anonymous — LAN use only)
diagram.json                Wokwi simulation layout
```

## Getting started

**1 — Flash the node.** Open `CR_Monitor/CR_Monitor.ino` in the Arduino IDE as board
*ESP32S3 Dev Module* (PSRAM: OPI, Flash: 16 MB). Install the `Adafruit SHT31`,
`BH1750` and `PubSubClient` libraries, then fill in the config at the top:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_HOST     = "192.168.1.5";   // your broker's LAN IP
const char* ROOM_ID       = "room-101";
```

**2 — Start the backend.**

```bash
docker compose up -d
```

Grafana lands on <http://localhost:3000> (`admin` / `admin`), Node-RED on
<http://localhost:1880>, InfluxDB on <http://localhost:8086>.

**3 — Watch the data.** Telemetry publishes to `classroom/<room>/<node>/telemetry`,
with `status` and `alert` on sibling topics.

`SETUP_GUIDE.txt` walks through all of this in detail, including the Node-RED flow
and the InfluxDB token setup.

## Security note

The credentials in the sketch are placeholders — put your own in locally and keep
them out of commits. The broker ships with `allow_anonymous true` for convenience on
a trusted LAN; add authentication before deploying anywhere real.
