# Dota 2 Linux Free cheat (menu on END)

A native helper library for **Dota 2 on Linux**. It is injected into the running game and adds a small Panorama menu with:

| Feature | What it does |
|---|---|
| **Auto-accept** | Accepts a found match as soon as the popup appears |
| **Camera distance** | Keeps the camera at the distance you choose (800–3000) |
| **Enemy last-seen** | Marks where every enemy hero was last visible, on the minimap and in the world, with an age timer |
| **Teleport tracker** | Shows where an enemy hero is teleporting to — even into the fog of war — with a countdown until they land; the spot becomes their last-seen position when they arrive |

Everything is done in memory: no game files are modified and no ConVars are changed.

> **Warning.** Injecting third-party code into Dota 2 breaks the Steam Subscriber Agreement and can get the account VAC-banned. This project exists for research into the game's client/network internals. Use it on an account you are prepared to lose — at your own risk.

---

## Contents

- [Requirements](#requirements)
- [Build](#build)
- [Install](#install)
- [Usage](#usage)
- [Configuration](#configuration)
- [How it works](#how-it-works)
- [After a game update](#after-a-game-update)
- [Project layout](#project-layout)
- [Credits and license](#credits-and-license)

---

## Requirements

| Dependency | Why | Tested with |
|---|---|---|
| Linux, x86-64 | The native Dota 2 Linux client is the target | Arch Linux (Omarchy) |
| Dota 2 (Steam, native Linux build) | Not the Proton/Windows build | Oct 2026 client |
| GCC ≥ 7.1 (C++17) | Compiler (`CMakeLists.txt` requires GCC) | GCC 16.2 |
| CMake ≥ 3.5, GNU make | Build system | CMake 4.4 |
| gdb | Used by `inject.sh` / `uload` to `dlopen` the library inside the game process | gdb 17.2 |
| `pidof` (procps), `killall` (psmisc) | Used by the scripts | — |
| systemd (user session), optional | Auto-inject service | — |

Install the toolchain:

```bash
# Arch / Manjaro / Omarchy
sudo pacman -S --needed base-devel cmake gdb procps-ng psmisc

# Debian / Ubuntu
sudo apt install build-essential cmake gdb procps psmisc

# Fedora
sudo dnf install gcc-c++ cmake make gdb procps-ng psmisc
```

### ptrace

Injection attaches `gdb` to the game without root, which needs the Yama ptrace scope set to `0`:

```bash
# once, until reboot
sudo sysctl kernel.yama.ptrace_scope=0

# persistent
echo 'kernel.yama.ptrace_scope = 0' | sudo tee /etc/sysctl.d/10-ptrace.conf
```

---

## Build

The scripts expect `libDota2Helper.so` next to them, so build in-source:

```bash
git clone https://github.com/nikitashutnik/LinuxDota2Cheat.git Dota2Helper
cd Dota2Helper
cmake .
make -j"$(nproc)"
```

The result is `libDota2Helper.so` in the project root (Release, stripped, `-O3`). Build artifacts are git-ignored.

---

## Install

### Where is Dota?

`inject.sh` copies the library into the game's binary directory (the Steam runtime sandbox cannot see paths outside the game) and `dlopen`s it from there. The default location is

```
~/.local/share/Steam/steamapps/common/dota 2 beta/game/bin/linuxsteamrt64
```

If your Steam library lives elsewhere, point `DOTA_DIR` at that directory:

```bash
export DOTA_DIR="/mnt/games/SteamLibrary/steamapps/common/dota 2 beta/game/bin/linuxsteamrt64"
```

### Manual injection

Start Dota 2, then:

```bash
./load          # same as ./inject.sh
```

The script waits until the game has loaded its client and UI libraries, pauses Steam's crash reporter for a moment, injects, and prints `injected into <pid>`. Injecting twice is detected and skipped.

### Auto-inject on every game start

`watch.sh` polls for a running `dota2` process and injects once per process. Run it as a user service:

```bash
cp contrib/d2h-watch.service ~/.config/systemd/user/
# edit ExecStart (path to your clone) and DOTA_DIR if needed
systemctl --user daemon-reload
systemctl --user enable --now d2h-watch
```

Its output goes to `/tmp/d2h_watch.log`. `D2H_DELAY` (seconds, default 5) is the extra wait after the game's libraries are mapped.

### Unload

```bash
./uload         # needs sudo: sends SIGXCPU so the library can restore the link map, then dlclose()
```

---

## Usage

| Key | Action |
|---|---|
| `End` | Toggle the in-game menu (works in the dashboard and in a match) |
| `Delete` | Run `/tmp/d2h_run.js` in the current HUD context (development helper) |

The menu has toggles for auto-accept and the last-seen markers, a stepper for the camera distance and one for how long a last-seen marker stays (1–60 s). Changes are saved immediately.

Markers:

- **Red ring** — enemy hero last seen here, with `12s` = seconds since.
- **Blue ring** — enemy hero teleporting here, with `TP 2s` = seconds until they land. A cancelled teleport removes the marker; a finished one turns it into a red last-seen marker at the destination.

Logs go to `/tmp/d2h_debug.log` (recreated on every injection). The client schema (every networked class with field offsets) is dumped to `~/.config/dota2helper_schema.txt` on start — handy when the game updates.

---

## Configuration

`~/.config/dota2helper.cfg`, one `key=value` per line, written by the menu:

| Key | Default | Meaning |
|---|---|---|
| `autoaccept` | `1` | Accept found matches |
| `camera_distance` | `0` | Camera distance; `0` leaves the game's camera alone |
| `lastseen` | `1` | Last-seen and teleport markers |
| `lastseen_secs` | `10` | Lifetime of a last-seen marker, seconds |

---

## How it works

The library is a plain `.so` whose constructor runs when `dlopen`ed inside the game. It resolves the game's interfaces through `CreateInterface`, finds a few globals by byte pattern, and hooks virtual methods by swapping the vtable pointer of the target object (`Utils/Vmt.h`) — no inline patching.

| Interface | Slot | Hook | Used for |
|---|---|---|---|
| `Source2Client002` (libclient) | 36 | `FrameStageNotify` | Per-frame tick: input, menu bridge, camera, markers |
| `PanoramaUIEngine001` → `CUIEngine` | 78 | `RunScript` | Captures the HUD panel; auto-accept |
| `ParticleSystemMgr003` (libparticles) | 18 | `CreateParticleCollection` | Names of particle systems the client builds |
| `NetworkMessagesVersion001` (libnetworksystem) | 3 | `UnserializeMessage` | Every incoming net message, before the game dispatches it |

Field offsets (team, scene node, dormancy, …) come from the game's own schema system (`SchemaSystem_001`), so they survive layout changes.

**Last-seen.** Enemy heroes stay in the client's entity list while in the fog; their scene node goes dormant. The tracker records the position at the last non-dormant tick and pushes markers to a Panorama overlay (a `DOTAHeroImage` per hero under `#minimap` and under `HUDElements`).

**Teleports.** When a hero channels a teleport, the server sends `CUserMsg_ParticleManager` messages for three effects: `teleport_start` attached to the hero, `teleport_origin` at the hero, and `teleport_end` at the destination. The client receives and builds all three even when the destination is in the fog — it only hides the effect through its fog-of-war check. The hook decodes the protobuf payload with a 40-line wire reader: a world-origin `*teleport_end*` system whose modifiers come from an enemy hero, plus its `UPDATE_TRANSFORM` for control point 0, gives the destination; control point 7 of the start effect carries the channel time (so stacked teleports show 5 s, 7 s, …); the effect's `DESTROY` before that time means the teleport was cancelled. Particle names arrive as a 64-bit resource id, resolved by pairing each `CREATE` with the collection the client builds for it.

**Menu bridge.** The menu is Panorama JavaScript created at runtime. JS cannot call into the library, so it publishes its state in the id of a throw-away panel (`D2H:<timestamp>:acc=1,dist=1200,…`), which the native side reads from the UI engine's panel table.

---

## After a game update

Dota updates can move things. Check `/tmp/d2h_debug.log` right after injection:

| Log line | Where to fix |
|---|---|
| `Interface <name> is NULL` | `src/Interfaces.cpp` — interface version string |
| `sig <name>: FAIL` | `src/Scanner.cpp` — byte pattern |
| `schema system: FAILED` | `src/SDK/Schema.cpp` — class-info layout probe |
| Markers or hooks silently stop working | vtable slots in `src/Hooks/Hooks.h` (each one documents how it was verified) |

---

## Project layout

```
src/
  McDota.cpp           entry point: constructor/destructor, hook setup, link-map hiding
  Interfaces.*         game interfaces (CreateInterface)
  Scanner.*            byte-pattern signatures
  Settings.*           ~/.config/dota2helper.cfg
  Hooks/               FrameStageNotify, RunScript, ParticleCollection, NetMessage
  Hacks/               Zoom (camera), LastSeen (markers + teleport tracker)
  GUI/                 Panorama JS menu and overlay, JS -> native bridge
  SDK/                 game class layouts: vtables, schema system, bit buffers
  Utils/               memory, pattern finder, VMT hooking, logger
inject.sh, load        inject into a running game
uload                  unload
watch.sh               auto-inject loop (see contrib/d2h-watch.service)
```

---

## Credits and license

Started from [McDota](https://github.com/LWSS/McDota) by LWSS — the SDK headers, VMT hooking and injection scripts come from there. Licensed under the **GNU GPL v3**, see [LICENSE](LICENSE).
