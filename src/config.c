#include "config.h"
#include "json.h"
#include "log.h"
#include "patterns.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Written when there is no config.json. The tune is "Shave and a Haircut" (ta, ta-ta ta ta ... ta TAA); the light
 * runs through the three LEDs and their mixes while it plays, and ends on the colour it then keeps. */
static const char default_config[] =
    "{\n"
    "  \"startup\": {\n"
    "    \"beep\": [\n"
    "      { \"tone\": 1, \"pause_ms\": 300 }, { \"tone\": 1, \"pause_ms\": 150 }, { \"tone\": 1, \"pause_ms\": 150 },\n"
    "      { \"tone\": 1, \"pause_ms\": 300 }, { \"tone\": 1, \"pause_ms\": 600 }, { \"tone\": 1, \"pause_ms\": 300 },\n"
    "      { \"tone\": 3 }\n"
    "    ],\n"
    "    \"led\": [\n"
    "      { \"color\": \"blue\", \"ms\": 250 }, { \"color\": \"white\", \"ms\": 250 }, { \"color\": \"orange\", \"ms\": 250 },\n"
    "      { \"color\": \"blue\", \"ms\": 250 }, { \"color\": \"white\", \"ms\": 250 }, { \"color\": \"orange\", \"ms\": 250 },\n"
    "      { \"blue\": 255, \"white\": 255, \"ms\": 300 }, { \"white\": 255, \"amber\": 255, \"ms\": 300 },\n"
    "      { \"blue\": 255, \"amber\": 255, \"ms\": 300 }, { \"color\": \"off\", \"ms\": 250 },\n"
    "      { \"blue\": 255, \"white\": 80, \"amber\": 200, \"ms\": 800 }\n"
    "    ]\n"
    "  },\n"
    "  \"led_color\": { \"blue\": 255, \"white\": 80, \"amber\": 200 }\n"
    "}\n";

static json_document doc;

static bool fail(char *error, size_t size, const char *message) {
    snprintf(error, size, "%s", message);
    return false;
}

static bool empty_or_absent(const json_node *node) {
    return node == NULL || (node->type == JSON_ARRAY && node->first_child < 0);
}

bool config_parse(const char *text, size_t length, config *out, char *error, size_t error_size) {
    memset(out, 0, sizeof(*out));
    if (!json_parse(&doc, text, length) || doc.nodes[0].type != JSON_OBJECT) {
        return fail(error, error_size, "the config must be a JSON object");
    }
    const json_node *root = &doc.nodes[0];
    const json_node *startup = json_member(&doc, root, "startup");
    if (startup != NULL) {
        if (startup->type != JSON_OBJECT) return fail(error, error_size, "startup must be an object");
        const json_node *beep = json_member(&doc, startup, "beep");
        const json_node *led = json_member(&doc, startup, "led");
        if (!empty_or_absent(beep) &&
            !patterns_beeps(&doc, beep, PLAYER_MAX_CONFIG_BEEPS, out->beeps, &out->beep_count)) {
            snprintf(error, error_size, "startup.beep: " BEEP_STEPS_RULE, PLAYER_MAX_CONFIG_BEEPS);
            return false;
        }
        if (!empty_or_absent(led) && !patterns_leds(&doc, led, out->leds, &out->led_count)) {
            return fail(error, error_size, "startup.led: " LED_STEPS_RULE);
        }
    }
    const json_node *color = json_member(&doc, root, "led_color");
    if (color != NULL && color->type != JSON_NULL) {
        if (color->type != JSON_OBJECT || !patterns_int(&doc, color, "blue", 0, 255, 0, &out->blue) ||
            !patterns_int(&doc, color, "white", 0, 255, 0, &out->white) ||
            !patterns_int(&doc, color, "amber", 0, 255, 0, &out->amber)) {
            return fail(error, error_size, "led_color: null, or blue, white and amber 0-255");
        }
        out->has_color = true;
    }
    return true;
}

const char *config_text(size_t *length) {
    static char text[CONFIG_MAX_SIZE];
    FILE *file = fopen(CONFIG_PATH, "rb");
    if (file == NULL) {
        if (errno != ENOENT) return NULL;
        *length = sizeof(default_config) - 1;
        return default_config;
    }
    *length = fread(text, 1, sizeof(text), file);
    bool whole = feof(file);
    fclose(file);
    return whole ? text : NULL;
}

static bool write_file(const char *text, size_t length) {
    mkdir(CONFIG_DIRECTORY, 0777);
    const char *temporary = CONFIG_PATH ".new";
    FILE *file = fopen(temporary, "wb");
    if (file == NULL) return false;
    bool written = fwrite(text, 1, length, file) == length;
    written = fclose(file) == 0 && written;
    if (!written || rename(temporary, CONFIG_PATH) != 0) {
        remove(temporary);
        return false;
    }
    return true;
}

bool config_load(config *out, char *error, size_t error_size) {
    struct stat info;
    bool exists = stat(CONFIG_PATH, &info) == 0;
    size_t length;
    const char *text = config_text(&length);
    if (text == NULL) return fail(error, error_size, "config.json can't be read");
    if (!exists) {
        if (write_file(text, length)) log_printf("wrote the default %s\n", CONFIG_PATH);
        else log_printf("couldn't write the default %s\n", CONFIG_PATH);
    }
    return config_parse(text, length, out, error, error_size);
}

bool config_save(const char *text, size_t length, config *out, char *error, size_t error_size) {
    if (length > CONFIG_MAX_SIZE) return fail(error, error_size, "the config is limited to 8192 bytes");
    if (!config_parse(text, length, out, error, error_size)) return false;
    if (!write_file(text, length)) return fail(error, error_size, "config.json can't be written");
    return true;
}
