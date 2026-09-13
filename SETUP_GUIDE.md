======================================================================

CLASSROOM MONITOR - STAGE 2 \& 3 SETUP WALKTHROUGH

From "ESP32 prints to Serial" to "data in a Grafana dashboard"

======================================================================



You have never done this before - that is fine. Follow the parts in

order. Do not skip. Each part ends with a CHECK: do not move on until

the check passes.



What you are building (recap):



&#x20; ESP32  --Wi-Fi/MQTT-->  Mosquitto (broker)  -->  Node-RED (clean +

&#x20; rules)  -->  InfluxDB (storage)  -->  Grafana (dashboard)



&#x20; MQTTX is just a viewer you use to confirm messages are flowing.



Time: about 2-3 hours the first time, most of it waiting on downloads.



Files in this folder that this guide uses:

&#x20; docker-compose.yml   - starts the broker + Node-RED + InfluxDB + Grafana

&#x20; mosquitto.conf       - broker config (already filled in)

&#x20; CR\_Monitor.cpp       - the ESP32 sketch

&#x20; CR\_Monitor\_NOTES.txt - design reference





======================================================================

PART 0 - PREREQUISITES

======================================================================



0.1  Find your PC's LAN IP address. Open PowerShell and run:



&#x20;       ipconfig



&#x20;    Look for "IPv4 Address" under your active adapter (Wi-Fi or

&#x20;    Ethernet). It looks like 192.168.1.23 or 10.0.0.5. WRITE IT DOWN

&#x20;    - this guide calls it  <PC\_IP>.



&#x20;    Your ESP32 and your PC must be on the SAME network (same router,

&#x20;    same Wi-Fi). A "guest" Wi-Fi usually blocks this - use the main

&#x20;    one.



0.2  Decide which sketch file is real.



&#x20;    Your project currently has TWO copies:

&#x20;       D:\\Suvin's Project\\CR\_Monitor.cpp            <- the one we edit

&#x20;       D:\\Suvin's Project\\CR\_Monitor\\CR\_Monitor.ino <- older, stale



&#x20;    The Arduino IDE compiles a folder named X containing X.ino. So:

&#x20;      - Make a folder  D:\\Suvin's Project\\CR\_Monitor\\

&#x20;      - Put the CURRENT code in it as  CR\_Monitor.ino

&#x20;      - Easiest: open CR\_Monitor.cpp, Select All, Copy. Open

&#x20;        CR\_Monitor\\CR\_Monitor.ino in the Arduino IDE, Select All,

&#x20;        Paste, Save.

&#x20;      - From now on only edit the .ino. Delete or ignore the .cpp so

&#x20;        you never flash the old version by mistake.



&#x20;    (If you already do it a different way that works - keep doing

&#x20;    that. The point is: ONE file is the real one.)



CHECK 0:  You have <PC\_IP> written down, and you know which file you

&#x20;         flash to the board.





======================================================================

PART 1 - ESP32 SIDE (finish Stage 2)

======================================================================



1.1  Install the MQTT library.

&#x20;    Arduino IDE > Tools > Manage Libraries > search "PubSubClient" >

&#x20;    install the one by Nick O'Leary.

&#x20;    (You should already have: Adafruit SHT31, Adafruit BusIO, BH1750.)



1.2  Edit the config at the top of the sketch:



&#x20;       WIFI\_SSID / WIFI\_PASSWORD   - your Wi-Fi (replace the placeholders)

&#x20;       MQTT\_HOST = "<PC\_IP>"       - the address from Part 0

&#x20;       ROOM\_ID   = "room-101"      - any short name for the room



1.3  Flash the board. Open Serial Monitor at 115200 baud.



1.4  You should see, within \~15 seconds:

&#x20;       Wi-Fi: connected, IP 192.168.x.x

&#x20;       MQTT: node id a1b2c3, topic base classroom/room-101/a1b2c3

&#x20;       MQTT: connecting to <PC\_IP>:1883... failed, rc=-2



&#x20;    "failed" is EXPECTED right now - the broker does not exist yet.

&#x20;    Everything else (Wi-Fi connected, node id printed) must be there.



CHECK 1:  Serial Monitor shows "Wi-Fi: connected" and an IP address,

&#x20;         and the status panel prints every 2 seconds.





======================================================================

PART 2 - INSTALL DOCKER DESKTOP

======================================================================



Docker lets you run the broker, Node-RED, InfluxDB and Grafana as one

bundle instead of four separate installers. Strongly recommended.



2.1  Download "Docker Desktop for Windows":

&#x20;       https://www.docker.com/products/docker-desktop/



2.2  Run the installer. Accept the WSL2 option if it asks. Reboot if

&#x20;    told to.



2.3  Launch Docker Desktop. Wait until the whale icon in the system

&#x20;    tray is steady (not animating). First start takes a few minutes.



2.4  Confirm it works - in PowerShell:

&#x20;       docker run hello-world

&#x20;    You should see "Hello from Docker!".



CHECK 2:  "docker run hello-world" prints the hello message.



&#x20;    ---- No Docker? (fallback) ----

&#x20;    You can install each service natively instead:

&#x20;       Mosquitto : https://mosquitto.org/download/  (Windows installer)

&#x20;       Node.js   : https://nodejs.org/  then:  npm install -g node-red

&#x20;       InfluxDB 2: https://www.influxdata.com/downloads/ (Windows)

&#x20;       Grafana   : https://grafana.com/grafana/download?platform=windows

&#x20;    Then start each one and skip to Part 5. The rest of the guide

&#x20;    still applies, only the "bring the stack up" step differs.





======================================================================

PART 3 - THE STACK FILES  (already created for you)

======================================================================



Two files are in this folder next to the guide:



&#x20; mosquitto.conf     - tells the broker to listen on port 1883 and

&#x20;                      allow anonymous connections (fine for a LAN /

&#x20;                      school project; add passwords before anything

&#x20;                      real).



&#x20; docker-compose.yml - defines the four services and how they connect.

&#x20;                      It also creates local folders for saved data so

&#x20;                      nothing is lost when you restart.



You do not need to edit either one to get started. Open them in a text

editor if you want to see what they do.





======================================================================

PART 4 - BRING THE STACK UP

======================================================================



4.1  In PowerShell, go to this folder:



&#x20;       cd "D:\\Suvin's Project"



4.2  Start everything:



&#x20;       docker compose up -d



&#x20;    First run downloads the images (a few hundred MB). "-d" means it

&#x20;    runs in the background.



4.3  Check all four are running:



&#x20;       docker compose ps



&#x20;    You want STATUS "Up" or "running" for: mosquitto, nodered,

&#x20;    influxdb, grafana.



4.4  Web pages now available (open in a browser):

&#x20;       Node-RED   http://localhost:1880

&#x20;       InfluxDB   http://localhost:8086

&#x20;       Grafana    http://localhost:3000   (login admin / admin)



&#x20;    To STOP everything later:   docker compose down

&#x20;    Your data survives a  down/up  cycle.



CHECK 4:  "docker compose ps" shows 4 services Up, and

&#x20;         http://localhost:1880 loads the Node-RED editor.





======================================================================

PART 5 - CONFIRM THE ESP32 IS PUBLISHING  (MQTTX)

======================================================================



5.1  The broker is live now. Look at the ESP32 Serial Monitor - within

&#x20;    5 seconds the line should change to:



&#x20;       MQTT: connecting to <PC\_IP>:1883... connected



&#x20;    and the status panel's MQTT line becomes:



&#x20;       | MQTT : connected, seq 12, time NOT synced



&#x20;    ("time NOT synced" fixes itself within \~10 s once NTP responds.)



5.2  Install MQTTX (the desktop client):

&#x20;       https://mqttx.app/  > Download for Windows



5.3  In MQTTX:  + New Connection

&#x20;       Name:  local

&#x20;       Host:  localhost      Port: 1883

&#x20;       (leave username/password empty)

&#x20;     Click Connect.



5.4  Add a subscription:  + New Subscription

&#x20;       Topic:  classroom/#

&#x20;     Click Confirm.



5.5  Every 2 seconds a message appears on

&#x20;    classroom/room-101/<node>/telemetry  like:



&#x20;       {"ts":1724835600,"ts\_valid":true,"uptime\_ms":40100,"seq":20,

&#x20;        "temp\_c":24.30,"hum\_pct":46.70,"lux":420.0,

&#x20;        "sound\_avg\_db":41.2,"sound\_peak\_db":58.6,

&#x20;        "sound\_calibrated":true,"alerts":"---","rssi":-43}



5.6  Test the Last Will: unplug the ESP32. After \~45 seconds a

&#x20;    message  "offline"  appears on  .../status.  Plug it back in ->

&#x20;    "online".



CHECK 5:  telemetry JSON arrives in MQTTX every 2 s, and ts\_valid is

&#x20;         true.  If not, see TROUBLESHOOTING at the bottom.





======================================================================

PART 6 - INFLUXDB: MAKE A PLACE TO STORE DATA

======================================================================



6.1  Open http://localhost:8086. First-time setup screen:

&#x20;       Username:      suvin\_RS

&#x20;       Password:      suvincrproject

&#x20;       Organization:  classroom

&#x20;       Bucket:        sensors

&#x20;     Click Continue / Save.



6.2  Create an API token for Node-RED to use:

&#x20;       Load Data (left menu) > API Tokens > Generate API Token >

&#x20;       "All Access API Token" (fine for a project) > name it

&#x20;       "node-red" > Save.

&#x20;     COPY the token string now - you cannot see it again later.

&#x20;     Paste it somewhere safe.



**APITOKENS:DRPVRH2J8aCVB03BCFGuVjh0jQd6sZeKmg7Lza2VSRuOZeTSZ0Q3Mtw7wPAu8oxaoMGSUzmoagxVfF-M6Mg4vA==**



CHECK 6:  You have: org = classroom, bucket = sensors, and a copied

&#x20;         API token.





======================================================================

PART 7 - NODE-RED: CLEAN THE DATA AND STORE IT

======================================================================



Node-RED is a visual wiring tool. You drag "nodes" onto a canvas and

connect them. We build: MQTT in -> parse JSON -> sanity check ->

InfluxDB out.



7.1  Open http://localhost:1880.



7.2  Install the InfluxDB nodes:

&#x20;       top-right menu (three bars) > Manage palette > Install tab >

&#x20;       search "node-red-contrib-influxdb" > Install.



7.3  Build the flow. From the left palette, drag these onto the canvas

&#x20;    left to right and wire them in a line (drag from the little grey

&#x20;    port on the right of one node to the left port of the next):



&#x20;    (a) "mqtt in" node.  Double-click it:

&#x20;           Server: click the pencil > Server: mosquitto  Port: 1883

&#x20;                   > Add / Update

&#x20;           Topic:  classroom/+/+/telemetry

&#x20;           Output: "a parsed JSON object"

&#x20;           QoS: 0

&#x20;        Done.



&#x20;    (b) "function" node - name it "validate". Paste this code:



&#x20;           const d = msg.payload;

&#x20;           // drop anything not shaped like our telemetry

&#x20;           if (typeof d !== 'object' || d.seq === undefined) return null;

&#x20;           // range sanity - reject impossible readings

&#x20;           const bad =

&#x20;             (d.temp\_c !== null \&\& (d.temp\_c < -10 || d.temp\_c > 60)) ||

&#x20;             (d.hum\_pct !== null \&\& (d.hum\_pct < 0 || d.hum\_pct > 100)) ||

&#x20;             (d.lux !== null \&\& (d.lux < 0 || d.lux > 120000)) ||

&#x20;             (d.sound\_peak\_db < 0 || d.sound\_peak\_db > 140);

&#x20;           if (bad) { node.warn('rejected: ' + JSON.stringify(d)); return null; }

&#x20;           // split topic classroom/<room>/<node>/telemetry

&#x20;           const parts = msg.topic.split('/');

&#x20;           msg.room = parts\[1];

&#x20;           msg.node = parts\[2];

&#x20;           return msg;



&#x20;    (c) "influxdb out" node.  Double-click it:

&#x20;           Server: pencil >

&#x20;               Version: 2.0

&#x20;               URL: http://influxdb:8086

&#x20;               Token: (paste the API token from Part 6)

&#x20;               Organization: classroom

&#x20;               Bucket: sensors

&#x20;             > Add / Update

&#x20;           Measurement: reading



&#x20;    (d) OPTIONAL "debug" node wired off the "validate" output so you

&#x20;        can watch messages in the right-hand Debug panel.



7.4  Click "Deploy" (top right).



7.5  Watch the Debug panel (bug icon, top right). Every 2 seconds a

&#x20;    validated message should appear. The mqtt node should say

&#x20;    "connected" under it.



CHECK 7:  Debug panel shows a telemetry object every 2 s and the

&#x20;         InfluxDB node shows no error.





======================================================================

PART 8 - INFLUXDB: CONFIRM DATA IS LANDING

======================================================================



8.1  Open http://localhost:8086 > Data Explorer (graph icon, left).



8.2  Build a quick query:

&#x20;       FROM: sensors

&#x20;       Filter: \_measurement = reading

&#x20;       Filter: \_field = temp\_c

&#x20;       > Submit

&#x20;    You should see your temperature line, updating.



CHECK 8:  Data Explorer shows a temp\_c series with recent points.





======================================================================

PART 9 - GRAFANA: THE DASHBOARD

======================================================================



9.1  Open http://localhost:3000 (admin / admin, set a new password).



9.2  Add InfluxDB as a data source:

&#x20;       Connections > Data sources > Add data source > InfluxDB

&#x20;         Query language: Flux

&#x20;         URL: http://influxdb:8086

&#x20;         Organization: classroom

&#x20;         Token: (the same API token)

&#x20;         Default bucket: sensors

&#x20;       > Save \& test  -> "datasource is working".



9.3  New dashboard > Add visualization > pick the InfluxDB source.

&#x20;    Paste this Flux query for temperature:



&#x20;       from(bucket: "sensors")

&#x20;         |> range(start: -6h)

&#x20;         |> filter(fn: (r) => r.\_measurement == "reading")

&#x20;         |> filter(fn: (r) => r.\_field == "temp\_c")



&#x20;    You get a live temperature graph. Click "Apply".



9.4  Repeat "Add visualization" for hum\_pct, lux, sound\_peak\_db -

&#x20;    change the last filter line each time. Arrange the panels, Save

&#x20;    the dashboard.



CHECK 9:  A Grafana dashboard shows temperature/humidity/lux/sound

&#x20;         updating every few seconds.



&#x20; >>> At this point Stage 2 and the core of Stage 3-4 are DONE. <<<

&#x20;     The rest is the parts that make it a real study, not a demo.





======================================================================

PART 10 - OCCUPANCY INPUT  (Stage 3, second data stream)

======================================================================



The readings only make sense against how full the room is. You want a

bucket: Unoccupied / Low / Medium / High - never an exact headcount

(privacy).



Simplest version to start:

&#x20; - In Node-RED add an "inject" node set to a string ("Low", "Medium",

&#x20;   ...) and a second "influxdb out" writing measurement "occupancy".

&#x20;   You manually inject the current level during a lesson.

&#x20; - Later, replace the manual inject with a real source (a PIR-based

&#x20;   counter, a door sensor, timetable data) publishing to

&#x20;   classroom/<room>/occupancy.



In Grafana, add the occupancy series as a second query on a panel, or

use it to split the sensor panels (group by occupancy level) so you

can compare "quiet room" vs "full room" conditions.





======================================================================

PART 11 - THE HUMAN FEEDBACK CHANNEL  (Stage 5)

======================================================================



Runs beside the sensors, not through them.



11.1  Make an anonymous form (Google Forms / Microsoft Forms):

&#x20;       - "How comfortable is the room right now?" (1-5)

&#x20;       - "Too hot / too cold / about right"

&#x20;       - "Too bright / too dark / about right"

&#x20;       - "Too noisy / about right"

&#x20;       - "Is this monitor useful?" (yes/no + comment)

&#x20;     No names, no email collection.



11.2  Collect responses to a spreadsheet. In the offline analysis

&#x20;     stage you join these against the sensor data by timestamp and

&#x20;     room, and check where the numbers and the perception agree or

&#x20;     diverge (e.g. 24 C but people say "too cold").





======================================================================

PART 12 - OFFLINE ANALYSIS  (Stage 4, the report)

======================================================================



Export from InfluxDB (Data Explorer > CSV) or query it from a Python /

R notebook. Planned analyses:

&#x20; - descriptive stats per variable, per occupancy bucket

&#x20; - time-series: daily/weekly patterns, does a full room heat up

&#x20; - threshold-exceedance counts (how often over the noise limit, etc.)

&#x20; - before/after: pick an intervention (open a window on a schedule,

&#x20;   change a light setting), compare the periods

&#x20; - sensor vs feedback: correlation / agreement between measured

&#x20;   conditions and perceived comfort





======================================================================

TROUBLESHOOTING

======================================================================



ESP32 says "MQTT: ... failed, rc=-2"  (can't reach broker)

&#x20; - Wrong MQTT\_HOST. Re-check <PC\_IP> with ipconfig.

&#x20; - PC firewall blocking port 1883. Allow it:

&#x20;     PowerShell (as Admin):

&#x20;     New-NetFirewallRule -DisplayName "MQTT 1883" -Direction Inbound `

&#x20;       -Protocol TCP -LocalPort 1883 -Action Allow

&#x20; - ESP32 and PC on different networks (guest Wi-Fi). Same SSID.

&#x20; - "docker compose ps" - is mosquitto actually Up?



ESP32 "rc=-4" or "rc=-2" intermittently

&#x20; - Broker up but slow / name clash. Usually harmless; it retries.



MQTTX connects but no messages

&#x20; - Wrong topic filter. Use exactly:  classroom/#

&#x20; - ESP32 Serial shows MQTT connected? If not, fix that first.



"time NOT synced" never clears

&#x20; - No internet on the PC/router, or UDP 123 blocked. NTP needs

&#x20;   outbound UDP. Data still flows; ts stays 0 and ts\_valid false.



Node-RED mqtt node stuck "connecting"

&#x20; - Server host must be  mosquitto  (the container name), NOT

&#x20;   localhost, because Node-RED runs inside Docker. Port 1883.



InfluxDB node error "unauthorized"

&#x20; - Wrong token, or org/bucket name mismatch. Must be exactly

&#x20;   classroom / sensors and the token from Part 6.



Grafana "datasource is working" but panels empty

&#x20; - range too short, or field name typo. Confirm data in InfluxDB

&#x20;   Data Explorer first (Part 8).



Everything was working, now nothing after a reboot

&#x20; - Start Docker Desktop, then:  cd "D:\\Suvin's Project"; docker compose up -d

&#x20; - Re-flash / power-cycle the ESP32.



Ports already in use (1880/8086/3000/1883)

&#x20; - Something else is using them. Stop it, or edit the left-hand port

&#x20;   numbers in docker-compose.yml (e.g. "3001:3000") and use the new

&#x20;   number in the browser.



