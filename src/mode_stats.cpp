/*
  Stats Mode - read game stats from a phone over Wi-Fi
  The ESP32 opens its own Wi-Fi network (STATS_WIFI_SSID / STATS_WIFI_PASS, no internet).
  Join it and open http://192.168.4.1 to see games played + high score per difficulty.
  GREEN breathes while the mode is active. Wi-Fi runs only in this mode, never during a game.
  Hold YELLOW 3s to exit (handled by main.cpp).
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include "shimon.h"
#include "hw.h"
#include "stats.h"
#include "mode_stats.h"

static WebServer server(80);

static void handleRoot() {
  String html =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Shimon Stats</title><style>"
    "body{font-family:-apple-system,sans-serif;background:#111;color:#eee;margin:0;padding:24px}"
    "h1{font-size:24px;margin:0 0 20px}"
    ".games{font-size:20px;margin-bottom:20px}.games b{font-size:32px}"
    "table{width:100%;border-collapse:collapse;font-size:20px}"
    "td{padding:12px 8px;border-bottom:1px solid #333}td:last-child{text-align:right;font-weight:bold}"
    ".dot{display:inline-block;width:14px;height:14px;border-radius:50%;margin-right:10px}"
    "button{margin-top:24px;width:100%;padding:14px;font-size:18px;border:0;border-radius:10px;background:#333;color:#eee}"
    "</style></head><body><h1>Shimon &mdash; Stats</h1>";
  html += "<div class='games'>Games played: <b>" + String(stats_games()) + "</b></div>";
  html += "<table><tr><td colspan='2' style='color:#999;font-size:16px'>High scores</td></tr>";
  static const char* const DOT[4] = {"#2a6cff", "#e53935", "#2bb34a", "#f5c518"};
  for (uint8_t d = 0; d < 4; d++) {
    html += "<tr><td><span class='dot' style='background:" + String(DOT[d]) + "'></span>";
    html += String(stats_diff_name(d)) + "</td><td>" + String(stats_hiscore(d)) + "</td></tr>";
  }
  html += "</table><button onclick='location.reload()'>Refresh</button></body></html>";
  server.send(200, "text/html", html);
  Serial.println("[STATS-MODE] Page served.");
}

void stats_mode_init() {
  Serial.println("[STATS-MODE] Init");
  hw_led_all_off();
  stats_load();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(STATS_WIFI_SSID, STATS_WIFI_PASS);
  server.on("/", handleRoot);
  server.onNotFound(handleRoot);
  server.begin();
  Serial.printf("[STATS-MODE] Wi-Fi \"%s\" up — open http://%s\n",
                STATS_WIFI_SSID, WiFi.softAPIP().toString().c_str());
}

void stats_mode_tick() {
  server.handleClient();

  // Slow GREEN breathing — signals Stats Mode
  unsigned long now = millis();
  uint8_t duty = (uint8_t)(PWM_MIN_EFFECTIVE_DUTY + 60.0f * (0.5f + 0.5f * sinf((float)(now % 3000) * 6.2832f / 3000.0f)));
  hw_led_duty(GREEN, duty);
}

void stats_mode_stop() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  hw_led_all_off();
  Serial.println("[STATS-MODE] Mode stopped.");
}
