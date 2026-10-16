#include "patterns.h"
#include "icc.h"

#include <string.h>

bool patterns_int(const json_document *doc, const json_node *object, const char *key, int min, int max, int fallback,
                  int *out) {
    const json_node *member = json_member(doc, object, key);
    if (member == NULL) {
        *out = fallback;
        return fallback >= min;
    }
    if (member->type != JSON_NUMBER || member->number < min || member->number > max) return false;
    *out = (int)member->number;
    return true;
}

bool patterns_beeps(const json_document *doc, const json_node *array, int max, beep_step *steps, int *count) {
    *count = 0;
    for (const json_node *step = json_first(doc, array); step; step = json_next(doc, step)) {
        if (*count == max || step->type != JSON_OBJECT ||
            !patterns_int(doc, step, "tone", 0, ICC_TONE_MAX, -1, &steps[*count].tone) ||
            !patterns_int(doc, step, "pause_ms", PLAYER_MIN_PAUSE_MS, PLAYER_MAX_PAUSE_MS, 200,
                          &steps[*count].pause_ms)) {
            return false;
        }
        (*count)++;
    }
    /* The last beep's pause is unused, so it doesn't count. */
    int total = 0;
    for (int i = 0; i + 1 < *count; i++) total += steps[i].pause_ms;
    return *count > 0 && total <= PLAYER_MAX_TOTAL_MS;
}

/* A colour step: "color" by name, or the channels blue, white and amber (0-255, each 0 when left out). */
static bool color_step(const json_document *doc, const json_node *step, led_step *out) {
    const json_node *name = json_member(doc, step, "color");
    if (name != NULL) {
        static const struct { const char *name; int blue, white, amber; } named[] = {
            { "blue", 255, 0, 0 }, { "white", 0, 255, 0 }, { "orange", 0, 0, 255 }, { "off", 0, 0, 0 },
        };
        if (name->type != JSON_STRING || json_member(doc, step, "blue") != NULL ||
            json_member(doc, step, "white") != NULL || json_member(doc, step, "amber") != NULL) {
            return false;
        }
        for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
            if (name->length == strlen(named[i].name) && memcmp(name->text, named[i].name, name->length) == 0) {
                out->blue = named[i].blue;
                out->white = named[i].white;
                out->amber = named[i].amber;
                return true;
            }
        }
        return false;
    }
    return patterns_int(doc, step, "blue", 0, 255, 0, &out->blue) &&
           patterns_int(doc, step, "white", 0, 255, 0, &out->white) &&
           patterns_int(doc, step, "amber", 0, 255, 0, &out->amber);
}

bool patterns_leds(const json_document *doc, const json_node *array, led_step *steps, int *count) {
    *count = 0;
    for (const json_node *step = json_first(doc, array); step; step = json_next(doc, step)) {
        if (*count == PLAYER_MAX_LED_STEPS || step->type != JSON_OBJECT) return false;
        led_step *s = &steps[*count];
        s->level = s->blue = s->white = s->amber = -1;
        bool brightness = json_member(doc, step, "level") != NULL;
        bool ok = brightness ? patterns_int(doc, step, "level", 0, ICC_LED_MAX, -1, &s->level) &&
                                   json_member(doc, step, "color") == NULL && json_member(doc, step, "blue") == NULL &&
                                   json_member(doc, step, "white") == NULL && json_member(doc, step, "amber") == NULL
                             : color_step(doc, step, s);
        if (!ok || !patterns_int(doc, step, "ms", PLAYER_MIN_LED_MS, PLAYER_MAX_LED_MS, -1, &s->ms)) return false;
        (*count)++;
    }
    return *count > 0 && patterns_led_ms(steps, *count) <= PLAYER_MAX_TOTAL_MS;
}

int patterns_led_ms(const led_step *steps, int count) {
    int total = 0;
    for (int i = 0; i < count; i++) total += steps[i].ms;
    return total;
}
