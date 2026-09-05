# PonChan USB Speaker

The optional USB speaker profile exposes a standard USB Audio Class playback
device named **PonChan USB Speaker**. It needs no Mac audio driver and carries
playback over USB, independently of the Wi-Fi voice bridge. The existing
assistant and dashboard still use their existing network connections.

The format is 24 kHz, 16-bit stereo over USB; both channels are averaged for the
single built-in speaker. macOS converts normal application audio to that format.
The existing native voice output keeps priority, with USB audio yielding during
native playback and for 250 ms afterwards. USB volume and mute affect only USB
PCM and do not overwrite saved voice volume. This is not a studio monitor path.

There is no USB microphone endpoint and no audio-triggered assistant activation.
Calls still require the existing explicit touch/button action.

## Build

Use ESP-IDF 5.5.4, fetch the normal dependencies, then build with an isolated
sdkconfig and the USB defaults last. Add your own deployment overlay before
`sdkconfig.defaults.usb-speaker` when preserving network and device settings.

```sh
STACKCHAN_SDKCONFIG="$PWD/build-usb/sdkconfig" \
STACKCHAN_SDKCONFIG_DEFAULTS="$PWD/sdkconfig.defaults;$PWD/sdkconfig.defaults.local;$PWD/sdkconfig.defaults.usb-speaker" \
  idf.py -B build-usb build
```

If no local overlay exists, omit it from the defaults list. The USB feature is
off in the default firmware. The library versions are pinned in the manifest
and lockfile. The CMake integration prevents the upstream library's PUBLIC USB
descriptor source from being compiled twice with StackChan's WHOLE_ARCHIVE main.

Before installation, back up flash, verify its size/hash and partition layout,
and retain the currently valid OTA slot. Install the new application into the
inactive slot and activate it with `ESP_OTA_IMG_NEW` so the existing bootloader
can roll back if startup fails. Do not erase NVS, the old app or assets. USB UAC
uses the same internal PHY as USB Serial/JTAG: the serial port disappears while
this profile runs. No eFuses are changed.

## Mac output selection

```sh
swiftc -framework CoreAudio -framework Foundation tools/mac_audio_output.swift -o /tmp/mac_audio_output
/tmp/mac_audio_output list
/tmp/mac_audio_output volume '<PonChan device UID from list>' 0.3
/tmp/mac_audio_output select '<PonChan device UID from list>'
```

Save the list and selection result before changing output. Selection changes both
normal and system-sound outputs and verifies readback. It does not change input
or application-specific/DAW audio-device preferences. Select the original UID to
restore the prior output.

## Verify the actual device output path

`usb_speaker_status.py` uses the existing libusb library to read counters via
USB endpoint zero; it does not claim the audio interface, record sound, or read
PCM. It requires libusb (on a Homebrew Mac: `brew install libusb`).

```sh
python3 tools/usb_speaker_status.py
```

Compare before/after a low-level test sound. `frames_written` and
`nonzero_writes` must increase and `write_errors` must remain zero. These counters
prove successful writes to the speaker codec. Acoustic audibility and sound
quality still require listening. Snapshot fields are sampled separately; do not
assume that a changing snapshot is an atomic accounting total.

The optional recovery command requests restoration of the previous valid OTA
firmware and a reboot. Stop playback and restore the Mac's previous output first.

```sh
python3 tools/usb_speaker_status.py --restore-previous-firmware
```

This reports only request acceptance; verify the old firmware and serial port
return. It needs a previously valid OTA image. Physical download mode remains
the fallback if the firmware is unresponsive. Follow the board manufacturer's
reset/download procedure; do not modify USB-related eFuses.

## Tests

```sh
cmake -S tests -B build-host-tests
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```

The USB tests cover clipping boundaries, antiphase stereo, gain/mute, partial
frames, buffer capacity and priority timeout across clock wraparound.
