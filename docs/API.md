# Using ps5-signal from your homebrew

ps5-signal is a background payload that plays beeps and light patterns on the PS5. Your homebrew sends it a
short HTTP request; ps5-signal does the work with the rights of a system process. You do not need those rights in
your own code.

This guide is for developers of payloads, game ports and homebrew apps.

## Before you start

- **ps5-signal must run.** The player starts it with their ELF loader, ps5-autoloader or the Signal Companion app.
  Always check that it answers (see [Is it running?](#is-it-running)). If it does not answer, carry on without
  signals. Do not fail.
- **Only on the console.** The service listens on `127.0.0.1:9150`. Code on the console can reach it; a PC cannot
  (the development build `ps5-signal-dev.elf` is the exception, for testing).
- **Homebrew apps need loopback sockets.** A payload can always connect. An app connects from its sandbox; this
  worked on the one console it was tested on (Signal Companion does it).

## The quickest way: C

Copy [`client/ps5_signal.h`](../client/ps5_signal.h) into your project. In exactly one source file:

```c
#define PS5_SIGNAL_IMPLEMENTATION
#include "ps5_signal.h"
```

Then:

```c
if (ps5_signal_running()) {
    /* Two short beeps. */
    ps5_signal_beep("[{\"tone\":1,\"pause_ms\":200},{\"tone\":1}]");
    /* Orange, then off, three times. */
    ps5_signal_led("[{\"color\":\"orange\",\"ms\":400},{\"color\":\"off\",\"ms\":400}]", 3);
}
```

| Function | What it does | Returns |
| --- | --- | --- |
| `ps5_signal_running()` | Asks whether the service answers | 1 or 0 |
| `ps5_signal_beep(pattern)` | Plays beeps | HTTP status |
| `ps5_signal_led(pattern, repeat)` | Plays a light pattern, 1-5 times | HTTP status |
| `ps5_signal_color(blue, white, amber)` | Holds a colour, 0-255 per channel; it is not saved | HTTP status |
| `ps5_signal_color_system()` | Gives the light back to the system | HTTP status |
| `ps5_signal_stop()` | Stops the beeps and the light that play now | HTTP status |

The HTTP status is `202` when the service plays it, or one of the [errors](#errors). `-1` means that the service
does not answer. Each call blocks for at most 2 seconds; call it from a worker thread when a frame must not wait.

## The API

Send requests to `http://127.0.0.1:9150`. Bodies are JSON objects; answers are JSON objects.

| Request | Body | Answer |
| --- | --- | --- |
| `GET /status` | | What plays now, the beeper and the light, and what this console supports |
| `POST /beep` | `{"pattern": [beep steps]}` | `202` |
| `POST /led` | `{"pattern": [light steps], "repeat": 1-5}` | `202` |
| `POST /color` | `{"blue": 0-255, "white": 0-255, "amber": 0-255}` or `{"system": true}` | `202` |
| `POST /stop` | | `200` |

`GET /config`, `PUT /config` and `POST /quit` are for Signal Companion and the player. Do not change the player's
config or stop the service from your homebrew.

### Beep steps

```json
{ "tone": 1, "pause_ms": 200 }
```

| Field | Values | |
| --- | --- | --- |
| `tone` | `0` silent, `1` short, `2` error tone, `3` long | Required |
| `pause_ms` | 100-2000 (default 200) | Time from this beep to the next one |

The beeper has no pitch. A tune is its rhythm: the tones and the pauses.

- A long tone (`3`) lasts longer than a short one. Two long tones 300 ms apart sound as one long beep; leave more
  room between them (the length of a long tone is not measured yet).
- Short tones 150 ms apart are heard as separate beeps.
- If the player muted the beeper in the console's settings, the beeps are silent. The service does not change that
  setting.

### Light steps

Each step has `ms` (250-10000, how long it shows) and one of:

| Field | Values | |
| --- | --- | --- |
| `"color"` | `"blue"`, `"white"`, `"orange"`, `"off"` | A colour by name |
| `"blue"`, `"white"`, `"amber"` | 0-255 each (0 when left out) | The three LEDs mixed |
| `"level"` | `0` bright, `1` medium, `2` dim | The brightness; the colour stays |

```json
{ "color": "orange", "ms": 400 }
{ "blue": 255, "amber": 60, "ms": 400 }
{ "level": 2, "ms": 400 }
```

The light has blue, white and amber LEDs only. There is no free RGB colour; mixes give other shades. Blue 255, white 80
and amber 200, for example, is the purple of ps5-signal's default.

After a light pattern, also one that you stopped, the service puts the light back: the colour the player keeps, or
the system's own.

### Limits

| | Limit |
| --- | --- |
| Beeps per request | 1-24, the pauses 10 seconds at most in all |
| Light steps per request | 1-24, 10 seconds at most in all, times `repeat` |
| At the same time | One beep pattern and one light pattern (they can play together) |
| Per minute | 30 beep requests, 120 light changes |

A request while a pattern of its kind plays is refused with `409`. There is no queue: wait, or call `/stop` first.

### Errors

Every error has a body like `{"error": "..."}` that says what is wrong.

| Status | Meaning | What to do |
| --- | --- | --- |
| `400` | The body is not valid | Read `error`; check the steps against the tables above |
| `404` / `405` | Unknown path / wrong method | Check the request |
| `409` | A pattern of that kind plays | Try again when it ends, or `POST /stop` |
| `413` | The body is over 8 KB | Send fewer steps |
| `429` | Over the per-minute limit | Wait; play fewer signals |
| `503` | This console does not offer it | Leave it out |

## Is it running?

```c
if (!ps5_signal_running()) {
    /* No signals: carry on. */
}
```

Or ask `GET /status` and check for `200`. The answer also says what this console supports:

```json
{
  "service": "ps5-signal", "version": "0.1.0",
  "beep": { "playing": false, "muted": false, "volume": 2 },
  "led": {
    "playing": false, "available": true, "level": null,
    "color": { "available": true, "blue": 255, "white": 80, "amber": 200,
               "held": { "blue": 255, "white": 80, "amber": 200 } }
  }
}
```

`null` means that the console did not report the value. Check `led.color.available` before you send colours, and
`led.available` before you send `level` steps.

## Examples

### A notification: two quick blinks and a beep

```c
ps5_signal_beep("[{\"tone\":1}]");
ps5_signal_led("[{\"color\":\"white\",\"ms\":250},{\"color\":\"off\",\"ms\":250}]", 2);
```

### Success and failure

```c
/* Success: a rising "ta-ta-TAA". */
ps5_signal_beep("[{\"tone\":1,\"pause_ms\":150},{\"tone\":1,\"pause_ms\":150},{\"tone\":3}]");
ps5_signal_led("[{\"blue\":0,\"white\":0,\"amber\":0,\"ms\":250},{\"color\":\"blue\",\"ms\":600}]", 1);

/* Failure: the error tone and an orange flash. */
ps5_signal_beep("[{\"tone\":2}]");
ps5_signal_led("[{\"color\":\"orange\",\"ms\":300},{\"color\":\"off\",\"ms\":300}]", 3);
```

### A slow breathing light

```c
ps5_signal_led("[{\"blue\":60,\"ms\":500},{\"blue\":150,\"ms\":500},{\"blue\":255,\"ms\":500},"
               "{\"blue\":150,\"ms\":500},{\"blue\":60,\"ms\":500}]", 3);
```

That is 2.5 seconds, 3 times: 7.5 seconds, under the 10-second limit.

### A colour while your homebrew runs

```c
ps5_signal_color(0, 0, 255);   /* amber while a download runs */
/* ... */
ps5_signal_color_system();     /* back to the system's light */
```

This colour is not saved. It stays until another colour, `{"system": true}`, or a restart of the service. If the
player keeps a colour in their config, `ps5_signal_color_system()` gives the light back to the system, not to their
colour. Prefer short light patterns: after a pattern the service always puts back the player's own colour.

### From JavaScript (a web page on the console)

```js
async function signal(path, body) {
  try {
    const answer = await fetch("http://127.0.0.1:9150" + path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    return answer.status;
  } catch {
    return -1; // not running
  }
}

signal("/beep", { pattern: [{ tone: 1, pause_ms: 200 }, { tone: 1 }] });
signal("/led", { pattern: [{ color: "blue", ms: 400 }, { color: "off", ms: 400 }], repeat: 2 });
```

### With curl, from a PC (development build only)

Load `ps5-signal-dev.elf` instead of `ps5-signal.elf`. It also listens on the network. Do not leave it running.

```sh
curl http://<console>:9150/status
curl -X POST http://<console>:9150/beep -d '{"pattern":[{"tone":1},{"tone":3}]}'
curl -X POST http://<console>:9150/led  -d '{"pattern":[{"color":"orange","ms":400}],"repeat":2}'
```

## Be a good neighbour

- **Signal what matters.** One signal per event; no beeps in a loop.
- **Keep it short.** Most players hear the console in a quiet room.
- **Do not fight over the light.** After your pattern the service puts the player's colour back. Use
  `ps5_signal_color` only while your homebrew runs, and call `ps5_signal_color_system` when you stop.
- **Handle `-1`, `409` and `429` quietly.** The signal is extra; your homebrew works without it.

## Licence

ps5-signal and `client/ps5_signal.h` are GPL-3.0-or-later. If you copy the header into your project, your project
must be compatible with the GPL.
