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

static icc_set_fn set_led_fn;
static icc_get_fn get_led_fn, get_mute_fn, get_volume_fn;

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
    get_led_fn = (icc_get_fn)resolve("sceKernelIccGetDynamicLedDimSetting");
    get_mute_fn = (icc_get_fn)resolve("sceKernelIccGetBuzzerOffSetting");
    get_volume_fn = (icc_get_fn)resolve("sceKernelIccGetBuzzerDimSetting");
    log_printf("icc: led set %s, led get %s (level %d), mute get %s (%d), volume get %s (%d)\n",
               set_led_fn ? "found" : "missing", get_led_fn ? "found" : "missing", icc_led_level(),
               get_mute_fn ? "found" : "missing", icc_beeper_muted(),
               get_volume_fn ? "found" : "missing", icc_beeper_volume());
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
int icc_beeper_muted(void) {
    int value = read_setting(get_mute_fn, 2);
    return value < 0 ? -1 : value != 0;
}

int icc_beeper_volume(void) { return read_setting(get_volume_fn, 2); }
