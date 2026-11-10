# openeq

A system-wide equalizer for Windows headsets, with a Qt interface. All the audio code is our own; no Equalizer APO, VB-Cable or other third-party engine is involved.

openeq is three pieces:

| Piece | File | What it does |
|---|---|---|
| APO | `openeq_apo.dll` | An Audio Processing Object that Windows' audio engine (`audiodg.exe`) loads into the headset's effect chain. It applies the EQ to everything the headset plays, with no added latency. |
| Helper | `openeq-devicectl.exe` | Installs the APO on a device, and removes it again. Needs administrator rights; the app runs it through a UAC prompt. |
| App | `openeq.exe` | The editor: a parametric EQ with a live response graph, a 10-band graphic mode, presets and a tray icon. |

The APO runs in user mode. It is not a kernel driver, so it doesn't need driver signing, test-signing mode or the WDK.

## How it fits together

```
openeq.exe ──writes──▶ %ProgramData%\openeq\devices\{endpoint}.txt ◀──watches── openeq_apo.dll (inside audiodg.exe)
     │                                                                              ▲
     └── runas ──▶ openeq-devicectl.exe ──registers + writes FxProperties ──────────┘
```

- The app writes the curve as a small text file, atomically, up to about 30 times a second while you drag.
- A watcher thread inside the APO notices the change, designs new filters at the device's real sample rate, and hands them to the audio thread. The audio thread crossfades into them over 10 ms, so edits don't click.
- The audio thread never allocates, locks or touches the disk.
- If the device already had a vendor or Windows effect in the slot openeq takes, openeq loads that effect and runs it first, so you keep it.

## Building

Requirements: Visual Studio 2022 or later with the C++ desktop workload, Windows SDK 10.0.22621 or later, Qt 6.8 (msvc2022_64) and CMake 3.21 or later.

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
```

```bash
cmake --build build --config Release
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

`build/bin` then holds everything, with the Qt runtime copied alongside it. For a folder you can copy to another PC (Qt and the VC++ runtime included):

```bash
cmake --install build --config Release --prefix dist
```

## Using it

1. Run `openeq.exe`, pick your headset under **Output** and click **Install on this device**.
2. Accept the UAC prompt. Sound cuts out for about a second while the Windows Audio service restarts.
3. The status pill turns **Active**. From then on, every edit is live.

To shape the curve:

- Drag a node to move it. Hold Shift for fine control.
- Scroll over a node to change its Q.
- Double-click empty space to add a band; double-click a node to reset its gain.
- Right-click a node to change its filter type, disable it or delete it.
- With a band selected, the arrow keys nudge it, Tab moves to the next band and Delete removes it.
- Each band under the graph has FREQ, GAIN and Q knobs. Drag a knob up or down (Shift for fine control), scroll over it, or double-click to reset it. Click a readout to type an exact value, such as `2.4k`.

The **PREAMP** strip sets the overall gain. When your boosts could push the signal past 0 dBFS, the CLIP light turns red and shows by how much. **Auto** lowers the preamp just enough to prevent that, and **LIMIT** soft-clips peaks instead.

Closing the window keeps openeq in the tray. The EQ itself runs in Windows' audio engine and keeps working even if the app isn't running.

### Presets

Built-in presets ship with the app. Your own are saved in `%AppData%\openeq\presets`. **⋯ → Import** reads openeq files and AutoEQ / `ParametricEQ.txt` files, such as headphone correction profiles from the AutoEQ project.

The file format is plain text:

```
openeq 1
name My headset
mode parametric
bypass 0
preamp -4.5
softclip 0
filter LS 105 5.5 0.71 on
filter PK 2400 -3.1 1.8 on
```

Filter types are `PK` (peak), `LS`/`HS` (low/high shelf), `LP`/`HP` (low/high pass), `NO` (notch) and `BP` (band pass).

## Uninstalling

Click **Uninstall** in the app. The helper restores the device's original effect settings exactly, from the backup it made at install time.

From an administrator prompt you can also run:

```bash
openeq-devicectl uninstall-all
```

`openeq-devicectl list` shows every output device and whether openeq is installed on it; this command doesn't need admin rights.

## Troubleshooting

- **The pill says Active but nothing changes.** Check `%ProgramData%\openeq\apo.log`. If it has no `LockForProcess` lines for your device, Windows didn't load the APO. Some drivers ignore third-party effects. Make sure **Audio enhancements** are on in the device's Windows sound settings.
- **Install failed.** After installing, the helper test-plays the device. If playback fails, it undoes the install on its own and restarts audio, so the device works again. The app shows the helper's log, which is also saved in `%ProgramData%\openeq\devicectl-last.log`.
- **No sound after an install.** Run `openeq-devicectl uninstall-all` from an administrator prompt. It restores the backup and restarts the audio service.

## Layout

```
core/       DSP: RBJ biquads, filter chain, config format, response maths (no Qt, no Windows)
apo/        openeq_apo.dll: COM class, config watcher, real-time processing
devicectl/  openeq-devicectl.exe: endpoint registry install/uninstall with backup
app/        Qt Widgets UI
common/     IDs and helpers shared by the above
tests/      test_core (DSP unit tests) and apo_host (drives the APO DLL offline)
```

## Status

This is the first iteration, for Windows 10 and 11 on x64 only. The APO isn't code-signed yet. Windows has historically loaded unsigned system effects, but this hasn't been confirmed on every build. A public release should sign `openeq_apo.dll` with an ordinary code-signing certificate; it doesn't need a kernel driver signature.
