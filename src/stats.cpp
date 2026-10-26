/*
  Persistent Stats (NVS)
  Game counter + per-difficulty high scores, stored in NVS namespace "shimon".
  Written only at game start (counter) and on a new high score, so flash wear is negligible.
*/

#include <Preferences.h>
#include "shimon.h"
#include "stats.h"

static const char* const DIFF_NAMES[4] = {"Blue", "Red", "Green", "Yellow"};
// "hs*" keys: score = longest completed sequence (Oct 2026). Earlier test builds used "hi*"
// with score = rounds completed — those values are ignored.
static const char* const HISCORE_KEYS[4] = {"hs0", "hs1", "hs2", "hs3"};
static uint32_t statsGameCount = 0;
static uint8_t  statsHiScore[4];

void stats_load() {
  Preferences prefs;
  prefs.begin("shimon", false);  // read-write: creates the namespace on a fresh board (no nvs_open error)
  statsGameCount = prefs.getUInt("games", 0);
  for (int i = 0; i < 4; i++) statsHiScore[i] = prefs.getUChar(HISCORE_KEYS[i], HISCORE_INIT[i]);
  prefs.end();
  Serial.printf("[STATS] Games played: %lu | Hi-scores: Blue=%u Red=%u Green=%u Yellow=%u\n",
                (unsigned long)statsGameCount,
                statsHiScore[0], statsHiScore[1], statsHiScore[2], statsHiScore[3]);
}

void stats_game_start(uint8_t d) {
  statsGameCount++;
  Preferences prefs;
  prefs.begin("shimon", false);
  prefs.putUInt("games", statsGameCount);
  prefs.end();
  Serial.printf("[STATS] Game #%lu start - difficulty=%s, hi-score=%u\n",
                (unsigned long)statsGameCount, DIFF_NAMES[d], statsHiScore[d]);
}

bool stats_game_end(uint8_t d, uint8_t score) {
  bool newHigh = score > statsHiScore[d];
  if (newHigh) {
    statsHiScore[d] = score;
    Preferences prefs;
    prefs.begin("shimon", false);
    prefs.putUChar(HISCORE_KEYS[d], score);
    prefs.end();
  }
  Serial.printf("[STATS] Game #%lu end - difficulty=%s, score=%u, hi-score=%u%s\n",
                (unsigned long)statsGameCount, DIFF_NAMES[d], score, statsHiScore[d],
                newHigh ? " (NEW HIGH SCORE!)" : "");
  return newHigh;
}

uint32_t    stats_games()               { return statsGameCount; }
uint8_t     stats_hiscore(uint8_t d)    { return statsHiScore[d]; }
const char* stats_diff_name(uint8_t d)  { return DIFF_NAMES[d]; }
