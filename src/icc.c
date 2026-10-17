#include "icc.h"
#include "log.h"

#include <stdint.h>
#include <unistd.h>

#include <ps5/kernel.h>
#include <ps5/nid.h>

/* In the SDK's libkernel stubs. */
extern int sceKernelIccSetBuzzer(int value);

/* libkernel in this process (kernel_dynlib_handle(getpid()) doesn't find it). */
#define LIBKERNEL_HANDLE 0x1
/* A system process's identity, which the ICC settings calls require. */
#define SYSTEM_AUTHID 0x3800000000010003ull

typedef int (*icc_set_fn)(int);
/* The getters' signature isn't documented: called with an out pointer, a value comes back either there (with
 * 0 returned) or as the return value. A void function ignores the extra argument on x86-64. */
typedef int (*icc_get_fn)(int *);

static icc_set_fn set_led_fn, set_volume_fn;
/* The volume this service set, while the getter can't say (-1: none set). */
static int volume_set = -1;
/* The beeper's getters (sceKernelIccGetBuzzerOffSetting, ...GetBuzzerDimSetting) are not called: they never
 * returned a value here and, unlike the LED's getter, never showed in the kernel's ICC trace, so what the names
 * resolve to is unclear. The volume is the one this service set. */
static icc_get_fn get_led_fn;

static intptr_t resolve(const char *name) {
    intptr_t address = kernel_dynlib_dlsym(getpid(), LIBKERNEL_HANDLE, name);
    if (address != 0) return address;
    char nid[12];
    nid_encode(name, nid);
    return kernel_dynlib_resolve(getpid(), LIBKERNEL_HANDLE, nid);
}

/* A setting in 0..max from a getter, or -1. */
static int read_setting(icc_get_fn getter, int max) {
    if (getter == NULL) return -1;
    int value = -1;
    int result = getter(&value);
    if (result == 0 && value >= 0 && value <= max) return value;
    if (value == -1 && result >= 0 && result <= max) return result;
    return -1;
}

void icc_init(void) {
    uint8_t caps[16];
    for (int i = 0; i < 16; i++) caps[i] = 0xff;
    kernel_set_ucred_authid(getpid(), SYSTEM_AUTHID);
    kernel_set_ucred_caps(getpid(), caps);

    set_led_fn = (icc_set_fn)resolve("sceKernelIccSetDynamicLedDimSetting");
    set_volume_fn = (icc_set_fn)resolve("sceKernelIccSetBuzzerDimSetting");
    get_led_fn = (icc_get_fn)resolve("sceKernelIccGetDynamicLedDimSetting");
    log_printf("icc: led set %s, led get %s (level %d), volume set %s\n", set_led_fn ? "found" : "missing",
               get_led_fn ? "found" : "missing", icc_led_level(), set_volume_fn ? "found" : "missing");
}

bool icc_beep_available(void) { return true; }

/* The getter's signature is a guess that fails on some consoles; setting still works without it. */
bool icc_led_available(void) { return set_led_fn != NULL; }

int icc_beep(int tone) {
    if (tone < 0 || tone > ICC_TONE_MAX) return -1;
    return sceKernelIccSetBuzzer(tone);
}

int icc_set_led(int level) {
    if (set_led_fn == NULL || level < 0 || level > ICC_LED_MAX) return -1;
    return set_led_fn(level);
}

int icc_led_level(void) { return read_setting(get_led_fn, ICC_LED_MAX); }

/* The off setting is 0 (beeps on), or 1 or 2 (both silence them). */
int icc_beeper_muted(void) { return -1; }

int icc_beeper_volume(void) { return volume_set; }

int icc_set_beeper_volume(int volume) {
    if (set_volume_fn == NULL || volume < 0 || volume > ICC_VOLUME_MAX) return -1;
    int result = set_volume_fn(volume);
    if (result == 0) volume_set = volume;
    else log_printf("beeper volume %d failed: 0x%x\n", volume, result);
    return result;
}
