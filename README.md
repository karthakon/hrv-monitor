# HRV Monitor for Pebble Time 2

Watchapp for testing HRV (RR/PPI interval) support on the Pebble Time 2
(emery/obelix). Requires custom firmware from
[karthakon/PebbleOS branch `hrv-gh3x2x`](https://github.com/karthakon/PebbleOS/tree/hrv-gh3x2x),
which adds HRV support for the GH3X2X optical sensor
(see coredevices/PebbleOS#1630).

**This will not run on stock firmware.** The app calls
`health_service_set_hrv_sample_period()` and
`health_service_peek_hrv_ppi_ms()`, which only exist in the fork.

## Screens (Up/Down to switch)

1. **LIVE** — last PPI (ms), current HR, HR/HRV event counters,
   running RMSSD over the last ~400 accepted beats.
2. **SESSION** — recording duration, accepted/rejected beat counts,
   RMSSD, SDNN, mean PPI for the whole session.
3. **BASELINE** — 7-night average RMSSD vs a 21-night baseline band
   (mean +/- 1 SD). Status: Collecting / Balanced / LOW / HIGH.
   Needs 21 recorded nights before it reports a status.
4. **NIGHT** — minutes per sleep stage (Awake/Light/Deep/REM) and a
   hypnogram strip for the last session.

## How sleep staging works (experimental)

Deep and Awake come directly from Pebble Health
(`HealthActivityRestfulSleep` / `HealthActivitySleep`). While Pebble
Health reports *light* sleep, a minute is relabeled **REM** if its
PPI variance is elevated (>1.5x) relative to the night's baseline
variance (computed from the first ~60 accepted beats after one hour).
REM shows erratic beat-to-beat intervals; deep sleep shows smooth,
high HRV. This is a crude first pass — expect only rough agreement
with a Garmin hypnogram.

## Artifact filtering

PPIs are rejected if quality is flagged bad, outside 300-2000 ms, or
>20% different from the previous accepted interval. Rejected counts
are shown on the SESSION screen.

## Storage

No phone companion is needed; everything is on-watch. Each minute of
recording writes one 8-byte epoch (mean PPI, RMSSD, beat count,
quality, stage) to persistent storage (~8 hours max per session).
Stopping a recording of 30+ minutes saves a nightly summary; the last
30 nights feed the BASELINE screen.

### Controls

Long-press Select (1.5s) to start or stop a recording. A single click
of Select only starts a recording when no stored epochs exist; it can
never stop a recording or overwrite stored data, so a stray button
press cannot wipe last night's session.

## Building and installing

### 1. Flash the fork firmware

Clone https://github.com/karthakon/PebbleOS, branch `hrv-gh3x2x`,
with submodules, then build in the official Docker image:

    git clone -b hrv-gh3x2x --recurse-submodules https://github.com/karthakon/PebbleOS.git
    cd PebbleOS
    docker run --rm -it -v "$PWD":/pebble -w /pebble ghcr.io/coredevices/pebbleos-docker:v5 bash
    pip install -r requirements.txt
    git config --global --add safe.directory '*'
    ./waf configure --board obelix@pvt --slot 1
    ./waf build && ./waf bundle && exit

You must manually keep track of which slot you are flashing. If this
is your first custom firmware, build for slot 1 (assuming original
firmware is on slot 0). For all future flashes, build for the
alternate slot. Install with the pebble tool: enable Developer
Connection in the Pebble app, then

    pebble fw --phone <PHONE_IP> install build/normal_obelix_pvt_*.pbz

Do NOT accept any "Update PebbleOS" prompt afterward — that reverts
to stock.

### 2. Build the SDK from the same tree

    pebble sdk install --tintin /path/to/PebbleOS

### 3. Build and install this app

    git clone https://github.com/karthakon/hrv-monitor.git
    cd hrv-monitor
    pebble build
    pebble install --phone <PHONE_IP>

## Comparing with another device

Wear the Pebble on one wrist and any device with overnight optical HRV
(Garmin, Oura, Whoop, Apple Watch, etc.) on the other. Start a recording
before sleep, stop it on waking, then compare the NIGHT screen against
the other device's sleep stages and the SESSION RMSSD against its
overnight HRV. Both are optical PRV, so expect some divergence between
devices. Many devices only compute HRV inside a scheduled sleep window
and need weeks to build a baseline; this app does its 7-day vs 21-day
comparison once 21 nights are recorded.
