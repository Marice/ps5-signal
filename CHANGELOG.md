# Changelog

All notable changes to this project are documented here. The format is based
on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [v0.1.0] - 2026-10-16

### Added
- Background payload that plays beep and LED sequences through an HTTP API on
  127.0.0.1:9150 (`/status`, `/beep`, `/led`, `/stop`).
- LED colour through the console's blue, white and amber channels, besides its
  brightness.
- A startup beep tune and LED sequence from `/data/ps5-signal/config.json`,
  written with the defaults when it doesn't exist: "Shave and a Haircut", a
  run through the LEDs and their mixes, and then a held purple (blue 255,
  white 80, amber 200).
- Limits against abuse: up to 24 beeps and 24 LED steps per sequence, each
  10 seconds at most in all, beeps 100-2000 ms apart, one beep and one LED
  sequence at a time, and per-minute limits.
- `client/ps5_signal.h` for homebrew in C: beeps, LED patterns, colours,
  stop, and whether the service runs.
- `docs/API.md`: how to use the API from your own homebrew.
- `POST /quit`, so a new build can be loaded without restarting the console.
- A development build that also listens on the network and shows the raw LED
  state, for testing.
- LED colours by rewriting each channel's program in the LED state as one step
  (the state holds a small program per channel, not one level).
- A colour to hold (`led_color` in the config, or `POST /color` to try one),
  set again when the system takes the LED back.
- `GET /config` and `PUT /config`, so an app can edit the config without
  access to `/data`.
- Host tests of the API and the startup config (`tests/run-host-tests.sh`).
