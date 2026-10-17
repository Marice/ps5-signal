#include "temperature.h"

/* In the SDK's libkernel stubs. */
extern int sceKernelGetCpuTemperature(int *celsius);
extern int sceKernelGetSocSensorTemperature(int sensor, int *celsius);

static int checked(int result, int celsius) {
    return result == 0 && celsius > 0 && celsius < 150 ? celsius : -1;
}

int temperature_cpu(void) {
    int celsius = -1;
    int result = sceKernelGetCpuTemperature(&celsius);
    return checked(result, celsius);
}

int temperature_soc(void) {
    int celsius = -1;
    int result = sceKernelGetSocSensorTemperature(0, &celsius);
    return checked(result, celsius);
}

int temperature_now(void) {
    int cpu = temperature_cpu(), soc = temperature_soc();
    return cpu > soc ? cpu : soc;
}

static int blend(int from, int to, int step, int steps) {
    return from + (to - from) * step / steps;
}

void temperature_color(int celsius, int cool, int warm, int *blue, int *white, int *amber) {
    const int middle = (cool + warm) / 2;
    if (celsius <= cool) {
        *blue = 255; *white = 0; *amber = 0;
    } else if (celsius < middle) {
        /* Blue to white. */
        *blue = blend(255, 0, celsius - cool, middle - cool);
        *white = blend(0, 255, celsius - cool, middle - cool);
        *amber = 0;
    } else if (celsius < warm) {
        /* White to amber. */
        *blue = 0;
        *white = blend(255, 0, celsius - middle, warm - middle);
        *amber = blend(0, 255, celsius - middle, warm - middle);
    } else {
        *blue = 0; *white = 0; *amber = 255;
    }
}
