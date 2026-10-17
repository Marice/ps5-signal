/* The console's embedded controller (ICC): beeper and LED brightness, through libkernel's sceKernelIcc
 * functions. Research by StonedModder (github.com/StonedModder/ps5-beeper-LED-Controller). */
#pragma once

#include <stdbool.h>

enum { ICC_TONE_SILENT = 0, ICC_TONE_SHORT = 1, ICC_TONE_ERROR = 2, ICC_TONE_LONG = 3, ICC_TONE_MAX = 3 };
enum { ICC_LED_BRIGHT = 0, ICC_LED_MEDIUM = 1, ICC_LED_DIM = 2, ICC_LED_MAX = 2 };
enum { ICC_VOLUME_HIGH = 0, ICC_VOLUME_MEDIUM = 1, ICC_VOLUME_LOW = 2, ICC_VOLUME_MAX = 2 };

/* Gives this process the rights the ICC calls need and looks the functions up. Call once at start. */
void icc_init(void);

/* Whether beeps, and whether LED patterns, are possible on this console. LED patterns need the brightness
 * getter: without the player's own level the service can't put it back afterwards. */
bool icc_beep_available(void);
bool icc_led_available(void);

/* Plays one of the beeper's tones (ICC_TONE_*); 0 on success. */
int icc_beep(int tone);
/* Sets the LED brightness (ICC_LED_*); 0 on success. */
int icc_set_led(int level);
/* Sets the beeper's volume (ICC_VOLUME_*), the setting the console keeps; 0 on success, -1 when it can't. */
int icc_set_beeper_volume(int volume);

/* The current settings, or -1 when the console doesn't say. */
int icc_led_level(void);
int icc_beeper_muted(void);
int icc_beeper_volume(void);
