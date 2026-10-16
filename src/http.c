#include "config.h"
#include "http.h"
#include "icc.h"
#include "indicator.h"
#include "json.h"
#include "log.h"
#include "patterns.h"
#include "player.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define VERSION "0.1.0"
#define MAX_HEAD 4096
#define MAX_BODY CONFIG_MAX_SIZE

static void respond(int fd, int status, const char *reason, const char *json) {
    char head[512];
    size_t length = json ? strlen(json) : 0;
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Connection: close\r\n\r\n",
                     status, reason, length);
    send(fd, head, (size_t)n, 0);
    if (length > 0) send(fd, json, length, 0);
}

static void respond_error(int fd, int status, const char *reason, const char *message) {
    char json[384];
    size_t n = (size_t)snprintf(json, sizeof(json), "{\"error\":\"");
    for (const char *c = message; *c && n < sizeof(json) - 4; c++) {
        if (*c == '"' || *c == '\\') json[n++] = '\\';
        json[n++] = (*c >= 0x20) ? *c : ' ';
    }
    snprintf(json + n, sizeof(json) - n, "\"}");
    respond(fd, status, reason, json);
}

/* "null" or the number. */
static const char *setting(char *buffer, size_t size, int value) {
    if (value < 0) return "null";
    snprintf(buffer, size, "%d", value);
    return buffer;
}

static void handle_status(int fd) {
    char json[768], led[8], volume[8], blue[8], white[8], amber[8], held_json[64] = "null";
    int muted = icc_beeper_muted();
    int b, w, a, hb, hw, ha;
    indicator_levels(&b, &w, &a);
    if (player_held_color(&hb, &hw, &ha)) {
        snprintf(held_json, sizeof(held_json), "{\"blue\":%d,\"white\":%d,\"amber\":%d}", hb, hw, ha);
    }
    snprintf(json, sizeof(json),
             "{\"service\":\"ps5-signal\",\"version\":\"" VERSION "\","
             "\"beep\":{\"playing\":%s,\"muted\":%s,\"volume\":%s},"
             "\"led\":{\"playing\":%s,\"available\":%s,\"level\":%s,"
             "\"color\":{\"available\":%s,\"blue\":%s,\"white\":%s,\"amber\":%s,\"held\":%s}}}",
             player_beeping() ? "true" : "false", muted < 0 ? "null" : (muted ? "true" : "false"),
             setting(volume, sizeof(volume), icc_beeper_volume()), player_lighting() ? "true" : "false",
             icc_led_available() ? "true" : "false", setting(led, sizeof(led), icc_led_level()),
             indicator_available() ? "true" : "false", setting(blue, sizeof(blue), b),
             setting(white, sizeof(white), w), setting(amber, sizeof(amber), a), held_json);
    respond(fd, 200, "OK", json);
}

static void respond_result(int fd, player_result result, const char *busy);

/* GET /config: config.json as it is. */
static void handle_config_get(int fd) {
    size_t length;
    const char *text = config_text(&length);
    if (text == NULL) {
        respond_error(fd, 500, "Internal Server Error", "config.json can't be read");
        return;
    }
    static char copy[CONFIG_MAX_SIZE + 1];
    memcpy(copy, text, length);
    copy[length] = '\0';
    respond(fd, 200, "OK", copy);
}

/* PUT /config: checked like the startup config, written, and its held colour applied at once. */
static void handle_config_put(int fd, const char *body, size_t length) {
    static config settings;
    char error[256];
    if (!config_save(body, length, &settings, error, sizeof(error))) {
        bool written = strstr(error, "can't be written") == NULL;
        respond_error(fd, written ? 400 : 500, written ? "Bad Request" : "Internal Server Error", error);
        return;
    }
    player_hold_color(settings.has_color, settings.blue, settings.white, settings.amber);
    respond(fd, 200, "OK", "{\"saved\":true}");
}

/* POST /color: holds a colour now, without saving it ({"blue", "white", "amber"} 0-255), or the system's again
 * ({"system": true}). For trying colours before saving them with PUT /config. */
static void handle_color(int fd, const json_document *doc) {
    const json_node *root = &doc->nodes[0];
    const json_node *system = json_member(doc, root, "system");
    int b = 0, w = 0, a = 0;
    bool hold;
    if (system != NULL) {
        bool alone = doc->nodes[root->first_child].next < 0;
        if (system->type != JSON_BOOL || system->number == 0 || !alone) {
            respond_error(fd, 400, "Bad Request", "{\"system\": true} alone, or blue, white and amber 0-255");
            return;
        }
        hold = false;
    } else if (patterns_int(doc, root, "blue", 0, 255, 0, &b) && patterns_int(doc, root, "white", 0, 255, 0, &w) &&
               patterns_int(doc, root, "amber", 0, 255, 0, &a)) {
        hold = true;
    } else {
        respond_error(fd, 400, "Bad Request", "{\"system\": true} alone, or blue, white and amber 0-255");
        return;
    }
    respond_result(fd, player_hold_color(hold, b, w, a), "");
}

static void respond_result(int fd, player_result result, const char *busy) {
    switch (result) {
    case PLAYER_STARTED:
        respond(fd, 202, "Accepted", "{\"playing\":true}");
        break;
    case PLAYER_BUSY:
        respond_error(fd, 409, "Conflict", busy);
        break;
    case PLAYER_LIMITED:
        respond_error(fd, 429, "Too Many Requests", "over the per-minute limit");
        break;
    case PLAYER_UNAVAILABLE:
        respond_error(fd, 503, "Service Unavailable", "not available on this console");
        break;
    }
}

static void handle_beep(int fd, const json_document *doc) {
    beep_step steps[PLAYER_MAX_API_BEEPS];
    int count;
    if (!patterns_beeps(doc, json_member(doc, &doc->nodes[0], "pattern"), PLAYER_MAX_API_BEEPS, steps, &count)) {
        char message[96];
        snprintf(message, sizeof(message), "pattern: " BEEP_STEPS_RULE, PLAYER_MAX_API_BEEPS);
        respond_error(fd, 400, "Bad Request", message);
        return;
    }
    respond_result(fd, player_beep(steps, count), "a beep sequence is playing");
}

static void handle_led(int fd, const json_document *doc) {
    const json_node *root = &doc->nodes[0];
    led_step steps[PLAYER_MAX_LED_STEPS];
    int count, repeat;
    if (!patterns_int(doc, root, "repeat", 1, PLAYER_MAX_LED_REPEAT, 1, &repeat)) {
        respond_error(fd, 400, "Bad Request", "repeat must be 1-5");
        return;
    }
    if (!patterns_leds(doc, json_member(doc, root, "pattern"), steps, &count)) {
        respond_error(fd, 400, "Bad Request", "pattern: " LED_STEPS_RULE);
        return;
    }
    respond_result(fd, player_led(steps, count, repeat), "an LED sequence is playing");
}

#if defined(SIGNAL_DEV)
/* Development build: the LED state as the console holds it, to find out how many channel records (and so how
 * many independently lit zones) it has. */
static void handle_indicator_raw(int fd) {
    uint8_t state[INDICATOR_STATE_SIZE];
    if (!indicator_read(state)) {
        respond_error(fd, 503, "Service Unavailable", "the LED state can't be read");
        return;
    }
    char json[INDICATOR_STATE_SIZE * 2 + 32];
    int n = snprintf(json, sizeof(json), "{\"state\":\"");
    for (int i = 0; i < INDICATOR_STATE_SIZE; i++) n += snprintf(json + n, sizeof(json) - (size_t)n, "%02x", state[i]);
    snprintf(json + n, sizeof(json) - (size_t)n, "\"}");
    respond(fd, 200, "OK", json);
}
#endif

/* Reads one request (head and body) and answers it. */
static void handle(int fd) {
    static char buffer[MAX_HEAD + MAX_BODY + 1];
    static json_document doc;
    size_t used = 0;
    char *body = NULL;
    long content_length = 0;
    for (;;) {
        ssize_t n = recv(fd, buffer + used, sizeof(buffer) - 1 - used, 0);
        if (n <= 0) return;
        used += (size_t)n;
        buffer[used] = '\0';
        if (body == NULL) {
            char *end = strstr(buffer, "\r\n\r\n");
            if (end == NULL) {
                if (used >= MAX_HEAD) {
                    respond_error(fd, 431, "Request Header Fields Too Large", "request head too large");
                    return;
                }
                continue;
            }
            body = end + 4;
            for (char *line = strstr(buffer, "\r\n"); line && line < end; line = strstr(line + 2, "\r\n")) {
                if (strncasecmp(line + 2, "Content-Length:", 15) == 0) content_length = strtol(line + 17, NULL, 10);
            }
            if (content_length < 0 || content_length > MAX_BODY) {
                respond_error(fd, 413, "Payload Too Large", "the body is limited to 4096 bytes");
                return;
            }
        }
        if ((size_t)(buffer + used - body) >= (size_t)content_length) break;
    }

    char method[8] = { 0 }, path[64] = { 0 };
    if (sscanf(buffer, "%7s %63s", method, path) != 2) {
        respond_error(fd, 400, "Bad Request", "malformed request line");
        return;
    }
    char *query = strchr(path, '?');
    if (query) *query = '\0';
    if (strcmp(method, "OPTIONS") == 0) {
        respond(fd, 204, "No Content", NULL);
        return;
    }
    bool get = strcmp(method, "GET") == 0, post = strcmp(method, "POST") == 0;
    if (strcmp(path, "/status") == 0) {
        if (get) handle_status(fd);
        else respond_error(fd, 405, "Method Not Allowed", "use GET");
        return;
    }
#if defined(SIGNAL_DEV)
    if (strcmp(path, "/indicator/raw") == 0) {
        if (get) handle_indicator_raw(fd);
        else respond_error(fd, 405, "Method Not Allowed", "use GET");
        return;
    }
#endif
    if (strcmp(path, "/stop") == 0) {
        if (!post) {
            respond_error(fd, 405, "Method Not Allowed", "use POST");
            return;
        }
        player_stop();
        respond(fd, 200, "OK", "{\"stopped\":true}");
        return;
    }
    /* Ends the service, so a new build can take the port without a restart of the console. */
    if (strcmp(path, "/quit") == 0) {
        if (!post) {
            respond_error(fd, 405, "Method Not Allowed", "use POST");
            return;
        }
        player_stop();
        respond(fd, 200, "OK", "{\"quitting\":true}");
        close(fd);
        log_printf("quit requested\n");
        usleep(300 * 1000); /* lets a sequence that was stopped put the LED back */
        _exit(0);
    }
    if (strcmp(path, "/config") == 0) {
        if (get) handle_config_get(fd);
        else if (strcmp(method, "PUT") == 0) handle_config_put(fd, body, (size_t)content_length);
        else respond_error(fd, 405, "Method Not Allowed", "use GET or PUT");
        return;
    }
    bool beep = strcmp(path, "/beep") == 0, led = strcmp(path, "/led") == 0, color = strcmp(path, "/color") == 0;
    if (!beep && !led && !color) {
        respond_error(fd, 404, "Not Found", "use /status, /beep, /led, /color, /config or /stop");
        return;
    }
    if (!post) {
        respond_error(fd, 405, "Method Not Allowed", "use POST");
        return;
    }
    if (!json_parse(&doc, body, (size_t)content_length) || doc.nodes[0].type != JSON_OBJECT) {
        respond_error(fd, 400, "Bad Request", "the body must be a JSON object");
        return;
    }
    if (beep) handle_beep(fd, &doc);
    else if (led) handle_led(fd, &doc);
    else handle_color(fd, &doc);
}

int http_listen(int port, bool any_address) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) return -1;
    int on = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in address = { 0 };
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(any_address ? INADDR_ANY : INADDR_LOOPBACK);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(server, 8) != 0) {
        close(server);
        return -1;
    }
    log_printf("listening on %s:%d\n", any_address ? "0.0.0.0" : "127.0.0.1", port);
    return server;
}

void http_serve(int server) {
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) continue;
        struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        handle(client);
        close(client);
    }
}
