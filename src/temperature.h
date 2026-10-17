/* The console's temperature, and the LED colour that shows it: blue when cool, white when normal, amber when
 * warm. The sensors are libkernel's (sceKernelGetCpuTemperature, sceKernelGetSocSensorTemperature), as used by
 * ps5-vitals. */
#pragma once

#include <stdbool.h>

/* Default range: reviewers measured the SoC at about 60-70 C while gaming; the console's own sensor may read
 * differently, so the range is the player's to set (temperature_led in config.json). */
enum { TEMPERATURE_COOL = 45, TEMPERATURE_WARM = 70, TEMPERATURE_MIN = 20, TEMPERATURE_MAX = 100 };

/* Degrees Celsius, or -1 when the sensor doesn't answer. */
int temperature_cpu(void);
int temperature_soc(void);
/* The hotter of the two, or -1 when neither answers. */
int temperature_now(void);

/* The colour for a temperature: blue up to cool, white halfway to warm, amber from warm on, blended between. */
void temperature_color(int celsius, int cool, int warm, int *blue, int *white, int *amber);
