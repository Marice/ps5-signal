/* Beep and LED steps from JSON, checked against the player's limits; shared by the HTTP API and config.json. */
#pragma once

#include "json.h"
#include "player.h"

/* A beep step: {"tone": 0-3, "pause_ms": 100-2000 (default 200)}; the last step's pause is unused. */
#define BEEP_STEPS_RULE "1-%d steps with tone 0-3 and pause_ms 100-2000"
/* A LED step: {"ms": 250-10000} with "level" 0-2, "color" blue/white/orange/off, or "blue", "white" and "amber"
 * 0-255. */
#define LED_STEPS_RULE "1-32 steps with ms 250-10000 and either level 0-2, color blue/white/orange/off, or blue/white/amber 0-255"

/* Reads the steps of a JSON array into steps (up to max); false when the array or a step is wrong. */
bool patterns_beeps(const json_document *doc, const json_node *array, int max, beep_step *steps, int *count);
bool patterns_leds(const json_document *doc, const json_node *array, led_step *steps, int *count);
/* An integer member in [min, max], or fallback when absent (fallback below min: required). */
bool patterns_int(const json_document *doc, const json_node *object, const char *key, int min, int max, int fallback,
                  int *out);
