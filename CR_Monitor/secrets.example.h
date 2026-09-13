/* Credentials template - Classroom Environment Monitor
 *
 * Copy this file to  secrets.h  in the same folder and fill in the real
 * values. secrets.h is listed in .gitignore and must never be committed:
 * anything that lands in git history stays there, and a repo that is
 * private today may not be tomorrow.
 *
 * Every node in a deployment can share this file; the node id that
 * separates them on MQTT is derived from the chip's own MAC.
 */

#pragma once

// ---- Wi-Fi ----
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---- MQTT broker auth. Leave both "" for an anonymous broker. ----
const char* MQTT_USER = "";
const char* MQTT_PASS = "";
