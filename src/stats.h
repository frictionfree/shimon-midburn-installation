#pragma once
#include <Arduino.h>

// Persistent game stats (NVS namespace "shimon"): game counter + per-difficulty high scores.
// Difficulty index: 0=Blue, 1=Red, 2=Green, 3=Yellow.
// Written by Game Mode, read by Stats Mode.

void        stats_load();                                   // read NVS + log summary
void        stats_game_start(uint8_t difficulty);           // counter++ (saved) + log
bool        stats_game_end(uint8_t difficulty, uint8_t score); // log; true + saved on new high score
uint32_t    stats_games();                                  // total games started
uint8_t     stats_hiscore(uint8_t difficulty);              // high score for difficulty
const char* stats_diff_name(uint8_t difficulty);            // "Blue"/"Red"/"Green"/"Yellow"
