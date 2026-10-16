# Changelog

All notable changes to this project are documented here. The format is based
on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added
- Background payload that plays beep and LED sequences through an HTTP API on
  127.0.0.1:9150 (`/status`, `/beep`, `/led`, `/stop`).
- LED colour through the console's blue, white and amber channels, besides its
  brightness.
- A startup beep tune and LED sequence from `/data/ps5-signal/config.json`,
  written with the defaults when it doesn't exist.
- Limits against abuse: 6 beeps per request, 100-2000 ms apart, one beep and
  one LED sequence at a time, and per-minute limits.
- `client/ps5_signal.h` for homebrew in C.
- A development build that also listens on the network and shows the raw LED
  state, for testing.
- A colour to hold (`led_color` in the config, or `POST /color` to try one),
  set again when the system takes the LED back.
- `GET /config` and `PUT /config`, so an app can edit the config without
  access to `/data`.
- Host tests of the API and the startup config (`tests/run-host-tests.sh`).
