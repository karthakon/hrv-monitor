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
2. **SESSION** — start/stop clock times, recording duration,
   accepted/rejected beat counts, RMSSD, SDNN, mean PPI for the
   session. Persists across app restarts (reads the last saved night
   when not recording).
3. **BASELINE** — 7-night average RMSSD vs a 21-night baseline band
   (mean +/- 1 SD). Status: Collecting / Balanced / LOW / HIGH.
   Needs 21 recorded nights before it reports a status.
4. **NIGHT** — total sleep duration plus minutes per sleep stage
   (Awake/Light/Deep/REM) and a hypnogram strip. Persists across app
   restarts.

## How sleep staging works (experimental)

Each minute is classified from this app's own HRV analysis plus
Pebble Health's live activity mask. Deep sleep is handled
separately (see below):

- **Awake** when the sleep bit is absent — but only after 3
  consecutive such minutes (a debounce that suppresses flicker from
  the live mask). Fewer than 3 holds the previous stage.
- **REM** when a minute otherwise reads *light* but its PPI variance
  is elevated (>1.5x) relative to the night's baseline variance
  (computed from the first ~60 accepted beats after one hour). REM
  shows erratic beat-to-beat intervals; deep sleep shows smooth, high
  HRV.
- **Light** otherwise. Minutes where Pebble Health reports
  `HealthActivityRestfulSleep` also fall here during live recording;
  they are reassigned to Deep at session stop.
- **Deep** is NOT classified live. Pebble Health's live "in deep
  right now" bit structurally undercounts (deep runs need ~20 min
  before they register, and a once-per-minute peek misses ended and
  retrospectively-registered runs). Instead, at session stop the app
  queries the finalized total via
  `health_service_sum(HealthMetricSleepRestfulSeconds, start, end)`
  and moves that many minutes from Light to Deep. This matches
  Pebble Health's own Deep figure exactly.

REM detection needs continuous beat-to-beat data, so it requires the
HRM to be sampling every minute. Aggressive HRM duty-cycling and REM
accuracy are in direct conflict: sparse sampling misses REM. This is
an experimental first pass — expect only rough agreement with a
consumer sleep tracker, which is itself an imperfect estimate, not
ground truth.

## Effect on Pebble Health's own numbers

The heart-rate sensor is a shared, arbitrated resource. When this app
runs the HRM continuously, Pebble Health's own tracking receives
those samples too. Normally Pebble Health samples HR in short bursts
spaced by your measurement-interval setting (e.g. 10 min); running
this app fills those gaps with extra samples. This can shift Pebble
Health's *heart-rate* metrics (median/resting HR, its phone HR log).

Pebble Health's *sleep staging*, however, is computed purely from
accelerometer motion — it does not read heart rate at all — so
running this app does not change Pebble Health's sleep-stage numbers.
Each app processes its data independently; what's shared is the raw
sensor input, not the results.

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
overnight HRV. This app and every device listed above are all
optical PRV estimates, so expect some divergence against whichever
one you compare with — none of them is ground truth. Many devices
only compute HRV inside a scheduled sleep window and need weeks to
build a baseline; this app does its 7-day vs 21-day
comparison once 21 nights are recorded.
