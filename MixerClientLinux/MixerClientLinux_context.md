---
name: project-linux-port
description: "Linux port of Windows MixerClient HID driver into MixerClientLinux — API substitutions, architecture, and build deps"
metadata: 
  node_type: memory
  type: project
  originSessionId: 93b12617-98bc-4457-a38f-4e3386a606ed
---

# MixerClientLinux — Linux port of MixerClient

Created `PCPanelClone/MixerClientLinux/` as a Linux equivalent of `PCPanelClone/MixerClient/`.
Only `.h` and `.cpp` files were ported (`.sln`/`.vcxproj` replaced with `CMakeLists.txt`).

## Files

| File | Notes |
|---|---|
| `HidReader.h/.cpp` | Linux HID layer |
| `VolumeMixerController.h/.cpp` | PulseAudio audio control |
| `MixerClient.cpp` | Entry point |
| `Util.h` | Error macros (int-based, not HRESULT) |
| `Holder.h` | Copied verbatim (generic C++) |
| `scope_guard.h` | Copied verbatim (pure C++) |
| `CMakeLists.txt` | C++20, links hidapi-hidraw + libpulse + x11 + nlohmann_json |

## Key API substitutions

| Windows | Linux |
|---|---|
| `SetupDiGetClassDevs` / `SetupDiEnumDeviceInterfaces` | `hid_enumerate()` (libhidapi) |
| `CreateFile` + `ReadFile`/`WriteFile` with `OVERLAPPED` | `hid_open_path` + `hid_read_timeout` / `hid_write` |
| `HidP_GetValueCaps` / `HidP_GetButtonCaps` / `HidP_GetData` | Custom HID descriptor parser using `HIDIOCGRDESC` ioctl + bit extraction |
| WASAPI (`IMMDevice`, `IAudioSessionManager2`, `ISimpleAudioVolume`) | PulseAudio threaded mainloop (`pa_context_get_sink_input_info_list`, `pa_context_set_sink_input_volume`) |
| `IAudioEndpointVolume` | `pa_context_get/set_sink_volume_by_name` |
| `GetForegroundWindow` + `QueryFullProcessImageNameW` | X11 `XGetInputFocus` + `_NET_WM_PID` + `/proc/<pid>/comm` |
| `SHGetFolderPath(CSIDL_PERSONAL)` | `getenv("HOME")` |
| `std::wstring` process names | `std::string` (UTF-8) |
| `Sleep(1000)` | `sleep(1)` |

## HID device target

Device filter: `usagePage == 0x01` (Generic Desktop), `usage == 0x37` (Dial).
The HID descriptor parser reads via `HIDIOCGRDESC` ioctl on the `/dev/hidraw*` node,
then parses Variable/Array input items, building `hidValues` (dials) and `hidButtons` maps
keyed by sequential DataIndex — matching Windows `HidP_GetData` semantics.

## Audio notes

- PulseAudio wrapper (`PulseSession` class) uses threaded mainloop with lock/wait/signal pattern.
- Per-app volume matches by substring of `PA_PROP_APPLICATION_PROCESS_BINARY`.
- Process names in `MixerSettings.json` need updating from `"firefox.exe"` → `"firefox"` etc.
- Settings file written to `$HOME/MixerSettings.json`.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Dependencies: `hidapi-hidraw`, `libpulse`, `x11`, `nlohmann_json`

udev rule needed to access `/dev/hidraw*` without root (Arduino Micro VID/PID, group `users` since `plugdev` doesn't exist on Arch):
```
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="2341", ATTRS{idProduct}=="8037", MODE="0660", GROUP="users"
```
Install: `sudo tee /etc/udev/rules.d/99-arduino-micro.rules`, then `sudo udevadm control --reload && sudo udevadm trigger`

## Running as a background service

Systemd user service at `~/.config/systemd/user/mixer-client.service`:
```ini
[Unit]
Description=PCPanel Mixer Client
After=graphical-session.target pulseaudio.service

[Service]
ExecStart=/home/ryanc/github/PCPanelClone/MixerClientLinux/MixerClientLinux
Restart=on-failure
RestartSec=3

[Install]
WantedBy=default.target
```

```bash
systemctl --user daemon-reload
systemctl --user enable --now mixer-client.service
journalctl --user -u mixer-client.service -f   # view logs
```

## IDE diagnostics note

`std::ranges::sort` on lines 261–262 of `HidReader.cpp` shows a false-positive error in the IDE
because the parser doesn't see C++20 from CMake. Compiles correctly with `-std=c++20`.
