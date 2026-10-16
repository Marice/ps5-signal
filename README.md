# ps5-signal

A background payload for the PS5 that plays **beep sequences** and **LED sequences** (brightness and colour)
for other homebrew. It runs once, listens on `127.0.0.1:9150` and takes simple HTTP requests with a JSON body,
so a payload, a game port or a web page on the console can beep or light the console without calling its
internal functions itself. A homebrew app can't drive these itself: they need a system process's rights, which
only this payload takes.

Tested on a 13.60 PS5 pro.

## What the console allows

| | Values | Source |
| --- | --- | --- |
| Beep tone | 0 silent, 1 short, 2 error tone, 3 long | [StonedModder's research](https://github.com/StonedModder/ps5-beeper-LED-Controller) |
| LED brightness | 0 bright, 1 medium, 2 dim | same |
| LED colour | three channels, 0-255 each: blue, white, amber; mixed for other shades | [PS5-Suite](https://github.com/manos555555/PS5-Suite) (`/dev/icc_indicator`) |

There is no free RGB colour: the LED has blue, white and amber light only. The LED state holds a record per channel;
each channel is a small program of steps (the system's own light fades this way). A colour is set by rewriting each
channel as a single step that holds its level. Whether the LED has zones that can be lit separately is not known yet.
Firmware 9.00 or newer.

## The easy way: Signal Companion

[Signal Companion](https://github.com/Marice/ps5-signal-companion) is a homebrew app that carries ps5-signal and
installs it for you:

1. Install Signal Companion (homebrew store, or the `PPSA69426` folder from its
   [release](https://github.com/Marice/ps5-signal-companion/releases/latest) in `/data/homebrew/`).
2. Open it and choose **Start it** when it says ps5-signal is not running.
3. Optional: switch on **Start with ps5-autoloader** on its Status screen, so ps5-signal loads with every boot
   ([ps5-autoloader](https://github.com/itsPLK/ps5-y2jb-autoloader) by itsPLK).
4. Choose the colour and edit the startup tune and light show in the app.

To update, install the new app and choose **Restart ps5-signal**. The rest of this section is for loading
ps5-signal yourself.

## Running it

1. Send `ps5-signal.elf` to the ELF loader (etaHEN: port 9021), or add it to [ps5-autoloader](https://github.com/itsPLK/ps5-y2jb-autoloader) to load it at start-up.
2. It plays the startup sequence from `/data/ps5-signal/config.json`. When the file doesn't exist it writes this
   default: the tune *Shave and a Haircut* (ta, ta-ta ta ta ... ta TAA), a run through the blue, white and amber
   LEDs and their mixes, and then the light keeps a purple (blue 255, white 80, amber 200).

   ```json
   {
     "startup": {
       "beep": [
         { "tone": 1, "pause_ms": 300 }, { "tone": 1, "pause_ms": 150 }, { "tone": 1, "pause_ms": 150 },
         { "tone": 1, "pause_ms": 300 }, { "tone": 1, "pause_ms": 600 }, { "tone": 1, "pause_ms": 300 },
         { "tone": 3 }
       ],
       "led": [
         { "color": "blue", "ms": 250 }, { "color": "white", "ms": 250 }, { "color": "orange", "ms": 250 },
         { "color": "blue", "ms": 250 }, { "color": "white", "ms": 250 }, { "color": "orange", "ms": 250 },
         { "blue": 255, "white": 255, "ms": 300 }, { "white": 255, "amber": 255, "ms": 300 },
         { "blue": 255, "amber": 255, "ms": 300 }, { "color": "off", "ms": 250 },
         { "blue": 255, "white": 80, "amber": 200, "ms": 800 }
       ]
     },
     "led_color": { "blue": 255, "white": 80, "amber": 200 }
   }
   ```

   - `led_color`: a colour to hold, `{"blue": 0-255, "white": 0-255, "amber": 0-255}`, or `null` for the
     system's own. The service sets it at start, sets it again when the system takes the LED back (checked every
     2 seconds), and returns to it after every LED sequence.

   - The steps follow the API's rules below.
   - Leave `beep` or `led` out, or make it `[]`, to turn that part off.
   - A file that isn't valid is left as it is and nothing plays at start-up; the log says what is wrong.
3. A second copy stops at once: the port is in use. The log goes to the kernel log, prefixed `[ps5-signal]`.

## API

All requests go to `http://127.0.0.1:9150`. Bodies are JSON objects; answers are JSON.

| Request | Body | Answer |
| --- | --- | --- |
| `GET /status` | | Whether a beep and an LED sequence play, the beeper's mute and volume, the LED's brightness and colour channels, and what this console supports |
| `POST /beep` | `{"pattern": [{"tone": 1, "pause_ms": 200}, {"tone": 3}]}` | `202` playing |
| `POST /led` | `{"pattern": [{"color": "orange", "ms": 400}, {"level": 2, "ms": 400}], "repeat": 2}` | `202` playing |
| `POST /stop` | | Ends both sequences and puts the LED back |
| `POST /quit` | | Ends both sequences and stops the service, so a new build can be loaded without restarting the console |
| `POST /color` | `{"blue": 120, "white": 0, "amber": 255}` or `{"system": true}` | Holds a colour now without saving it (to try colours), or the system's again |
| `GET /config` | | `config.json` as it is (the default when there is none) |
| `PUT /config` | the whole config | Checked like the startup config, written, and its `led_color` held at once; `400` with the reason when it isn't valid (the file is then left as it is) |

**Beeps.** 1-24 steps, the pauses 10 seconds at most in all. `tone` 0-3; `pause_ms` 100-2000 (default 200), the time until the next beep.

**LED steps.** 1-24 steps, 10 seconds at most in all (times `repeat`), each with `ms` 250-10000 (how long it shows) and one of:

| | |
| --- | --- |
| `"level": 0-2` | brightness |
| `"color": "blue"`, `"white"`, `"orange"` or `"off"` | a colour by name |
| `"blue"`, `"white"`, `"amber"`: 0-255 | the channels mixed (each 0 when left out) |

`repeat` 1-5 (default 1) plays the LED steps again.

**One at a time.** One beep sequence and one LED sequence can play at once (side by side). A request while a
sequence of its kind plays is refused with `409`; `/stop` ends them.

**Per minute.** 30 beep requests and 120 LED changes.

Errors: `400` invalid body, `404` unknown path, `405` wrong method, `409` a sequence of that kind is playing,
`413` body over 8 KB, `429` over the per-minute limit, `503` not available on this console. The body explains:
`{"error": "..."}`.

After an LED sequence, also a stopped one, the service puts the LED back: the brightness the player chose, and
the held colour, or else the colour state the system had set. If the beeper is muted in the console's settings, beeps stay
silent: the service doesn't change that setting.

**For developers:** [docs/API.md](docs/API.md) explains how to use the API from your own homebrew, with examples
in C and JavaScript.

### From C

Copy [`client/ps5_signal.h`](client/ps5_signal.h) into your project:

```c
#define PS5_SIGNAL_IMPLEMENTATION
#include "ps5_signal.h"

ps5_signal_beep("[{\"tone\":1,\"pause_ms\":150},{\"tone\":1}]");
ps5_signal_led("[{\"color\":\"orange\",\"ms\":400},{\"color\":\"off\",\"ms\":400}]", 3);
```

Each call returns the HTTP status, or -1 when the service doesn't run. The header also has
`ps5_signal_running()`, `ps5_signal_color()`, `ps5_signal_color_system()` and `ps5_signal_stop()`.

### From a web page or JavaScript

```js
fetch("http://127.0.0.1:9150/beep", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ pattern: [{ tone: 1, pause_ms: 200 }, { tone: 3 }] }),
});
```

### With curl (development build)

The release build only answers on the console itself. `ps5-signal-dev.elf` also listens on the network, to try
the API from a PC, and adds `GET /indicator/raw` (the LED state as the console holds it, in hex). Don't leave it
running:

```sh
curl http://<console>:9150/status
curl -X POST http://<console>:9150/beep -d '{"pattern":[{"tone":1},{"tone":1}]}'
curl http://<console>:9150/indicator/raw
```

## Safety

- **Local only.** The service runs with a system process's rights. It listens on `127.0.0.1`, so only software
  on the console can use it.
- **Abuse limits.** At most 24 beeps and 10 seconds per request, 100-2000 ms apart, one sequence of each kind at a time, and the
  per-minute limits above.
- **Flash wear is unknown.** The beeper and brightness settings survive a restart. The research says the ICC holds
  them until the console loses power, which points to memory that stays powered rather than flash, and the ICC has
  a separate flush function (`sceKernelIccNvsFlush`). Neither is proven: LED steps last at least 250 ms and LED
  changes are limited per minute until it is known.
- **The system may take the LED back.** During a colour step the service checks twice a second and sets the
  colour again only when it changed.
- **Firmware.** Functions and the LED device are looked up at start. When one is missing, `/status` says so and
  its requests answer `503`; nothing crashes.

## Building

You need Linux or WSL with Docker and Python 3. `build.sh` downloads the PS5 payload SDK (v0.43, checked by
sha256) into `.deps/` and builds in a container.

```sh
docker build -t ps5-signal-build docker   # once
./build.sh                                # ps5-signal.elf
./build.sh dev                            # ps5-signal-dev.elf, also on the network
tests/run-host-tests.sh                   # the API and the startup config on the PC (needs cc and curl)
tools/deploy.sh ps5-signal-dev.elf        # to PS5_HOST's ELF loader (port 9021)
```

## Licence and credits

GPL-3.0-or-later ([LICENSE](LICENSE)).

- The beeper and LED brightness functions, their values and how to call them: [StonedModder's
  ps5-beeper-LED-Controller](https://github.com/StonedModder/ps5-beeper-LED-Controller) (GPL-3.0-or-later)
- The LED colour channels, the `/dev/icc_indicator` ioctls and the layout of the LED state:
  [PS5-Suite](https://github.com/manos555555/PS5-Suite) by manos555555. Its code has no licence, so none of it is
  copied here; ps5-signal implements those findings itself.
- [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) by John Törnblom
- [ps5-autoloader](https://github.com/itsPLK/ps5-y2jb-autoloader) by itsPLK, which loads ps5-signal with the rest of a payload chain at start-up
