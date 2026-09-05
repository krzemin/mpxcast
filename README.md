# mpxcast

![mpxcast logo](assets/mpxcast.png)

`mpxcast` is a small FM radio streamer for RTL-SDR USB receivers. It tunes to an
FM station, decodes the signal locally, serves the audio over HTTP, and exposes
RDS radiotext as ICY metadata tags.

## Features

- live RTL-SDR input or looping captured IQ-file input
- mono and stereo FM decoding
- WAV and MPEG-TS audio streaming
- basic RDS decoding
- ICY metadata from RDS radiotext

The project is intended to be small enough for low-power computers. It has been
tested on a Raspberry Pi 3. Mono streaming is much cheaper than stereo plus RDS;
stereo and RDS decoding exercise considerably more DSP code and need more CPU.

The HTTP stream works well with music servers and players such as
[MPD](https://www.musicpd.org/).

## Example

Start the streamer:

```sh
mpxcast --host 0.0.0.0 --port 2347
```

Then open a stream URL:

```text
http://radio.local:2347/93600000.ts
http://radio.local:2347/93.6.wav
```

The path is `/<frequency>[.ts|.wav]`, where frequency can be written in Hz or
MHz. Query parameters can override stream defaults, for example:

```text
http://radio.local:2347/93.6.ts?stereo=0&rds=1&name=Local%20FM
```

## Captured IQ files

Use `--input-file` (or `-f`) to replay a captured RTL-SDR recording instead of
opening a device:

```sh
mpxcast -f station-94.9MHz.bin
```

The file must contain raw unsigned 8-bit interleaved I/Q samples at 1,920,000
samples per second, such as a recording made with:

```sh
rtl_sdr -f 94900000 -s 1920000 station-94.9MHz.bin
```

Playback loops indefinitely. The frequency in the HTTP path is accepted for
stream metadata and session selection, but is ignored for file input because a
recording cannot be retuned. `--device` and `--input-file` cannot be combined.

## Logging

The default log level is `info`. Use `-v` for `debug`, `-vv` for `trace`, or
set an explicit level with `--log-level=trace|debug|info|warn|error`. An explicit
log level takes precedence over `-v`.

## Version

Use `mpxcast --version` or `mpxcast -V` to print the build version.

## Limitations

- only RTL-SDR devices are supported directly
- clients with identical stream settings share one live DSP pipeline; a request with different
  settings replaces the active stream
- a client more than about 5 seconds behind the live stream is disconnected
- only uncompressed 48 kHz / 16-bit PCM audio is streamed
- RDS support is basic and does not yet parse richer services such as RT+

## Inspiration

This project takes its basic shape from:

- [`AlbrechtL/rtl_fm_streamer`](https://github.com/AlbrechtL/rtl_fm_streamer)
- [`windytan/redsea`](https://github.com/windytan/redsea)

## License

`mpxcast` is distributed under the GNU GPL v2 or later.
