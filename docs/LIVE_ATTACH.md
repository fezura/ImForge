# Live attach

Live attach connects the builder to your running application, so every change you make is
rendered by your app with your renderer, fonts and style. It's for development only: release
builds compile it away.

## What your app needs

```cpp
#define IMFORGE_LIVE                         // development builds only
#include "imforge_client.h"                  // or the amalgamated dist/imforge_client_single.h

IMFORGE_REFLECT(Player, health, ammo, pos, name);   // or #include "generated/imforge_reflect.h"
IMFORGE_REFLECT(Settings, esp, aimbot, fov);

void Init() {
    // after ImGui::CreateContext() and your backend init
    imforge::live::Connect("tcp://127.0.0.1:7777");
    #include "generated/imforge_dev.h"       // registers handlers and locked blocks (optional)
}

void Frame() {
    ImGui::NewFrame();
    imforge::live::NewFrame();               // polls the transport, applies updates
    imforge::live::Draw("MainMenu", { imforge::live::Bind("player", g_player),
                                      imforge::live::Bind("cfg", g_settings) });
    ImGui::Render();
}
```

`Connect` never blocks, and while the builder isn't running it keeps retrying with a 1 s
backoff. Without `IMFORGE_LIVE` every call above is an empty inline function, so you can leave
the lines in and only flip the define.

The builder is the server and your app connects to it. Start the builder first, or let the app
reconnect on its own.

## Pairing

Every link is authenticated with a one-time code before the app accepts a single panel. You
never type the code; it's handled through a file only your user account can read.

When the builder starts listening it generates a random code and writes it to a pairing file
named after the endpoint:

- Windows: `%LOCALAPPDATA%\ImForge\live\tcp-7777.pair` (or `pipe-<name>.pair`, `shm-<name>.pair`)
- Linux: `$XDG_RUNTIME_DIR/imforge/tcp-7777.pair`, or `~/.cache/imforge/...` when
  `XDG_RUNTIME_DIR` isn't set

`imforge::live::Connect(url)` works out the same path from the URL and reads the code before
every (re)connect. If there's no file yet, the app waits quietly (`Status()` says "waiting for
ImForge builder") and doesn't even open a connection.

Both sides then prove they know the code with an HMAC challenge/response; the code itself never
goes over the wire. The app ignores the builder until the builder has proven it, and the other
way round. Until then the app only sends a random nonce. Its name, pid, widgets and textures go
out in a `client_info` message once the builder is authenticated.

After pairing, every message in both directions carries an HMAC and a sequence number. If a
message was modified, injected, replayed, reordered or lost, the receiver notices before parsing
it, drops the connection, wipes the keys and reports a security event (in the builder: a Log
entry with source `security` and a red counter in the Live panel; in the app: `Status()` and
`LastError()` start with "security:"). The app then pairs again with the next code. Nothing is
encrypted, since the link is local and carries UI definitions, not secrets.

The builder replaces the code the moment pairing succeeds, so a copied or replayed code is
useless. Unused codes are replaced after 10 minutes. Five wrong answers burn the code and the
builder refuses connections for 3 seconds; a peer that gets the challenge and disconnects isn't
counted, because it guessed nothing. Stopping the server or closing the builder deletes the
file. [SECURITY_MODEL.md](SECURITY_MODEL.md) explains the reasoning.

The Live panel shows the state ("paired automatically with MyGame pid 1234"), the pairing file,
failed attempts, a **Rotate now** button and the **Require pairing** switch. That switch is a
builder preference, not a project setting, so a downloaded project can't turn authentication
off. With it off, apps built with an older `imforge_client.h` that don't pair are accepted too,
and the panel shows a red UNAUTHENTICATED badge.

### When the app can't read the pairing file

That happens when the app runs in a VM, on another machine, in a sandbox or under a different
user account. Click **Copy code** in the Live panel and hand the code to the app, either as an
environment variable `IMFORGE_PAIRING_CODE=XXXX-XXXX-...` or with
`imforge::live::SetPairingCode("...")` before or after `Connect`. The code works once; for the
next session copy the new one, or point both sides at a shared directory with
`imforge::pairing::SetPairingDir()`.

## Transports

Pick one in the wizard or project settings. The URL you pass to `Connect` has to match.

| URL | Mechanism | Notes |
|---|---|---|
| `tcp://127.0.0.1:7777` | TCP socket | works everywhere; listens on loopback only unless you change the bind host in Preferences (for an app on another machine or VM) |
| `pipe://ImForge` | Windows named pipe `\\.\pipe\ImForge`; on Linux a unix socket in `$XDG_RUNTIME_DIR` (else `/tmp`) | lowest overhead per message, local only |
| `shm://ImForge` | shared memory ring buffer, polled each frame | no sockets at all, for places where networking APIs are unwelcome |

Messages are JSON (listed in [SPEC.md](SPEC.md#messages)). The payload is the panel definition,
not code, so an update is a few kilobytes.

## What updates live

Widgets, properties, sizes and layout, themes, per-widget styles and style classes, fonts (if
your app loads them), bindings, `visibleIf`/`enabledIf`, label interpolation, panel state, the
no-code actions, animations, DrawList effects and blur parameters all update live.

Event handler bodies, locked `Code` blocks and custom widget implementations are compiled into
your app, so they need a rebuild. Until the hashes match again the builder shows a "stale"
badge on them.

## Registering your own code

```cpp
imforge::live::RegisterHandler("MainMenu.btn_play.onClick", &MainMenu_btn_play_OnClick_Live);
imforge::live::RegisterBlock("MainMenu.custom_1", &MainMenu_custom_1_Block_Live);
imforge::live::RegisterWidget("Toggle", &imb::ess::DrawToggleInterp);   // custom widgets
imforge::live::RegisterTexture("logo", my_texture_id);                  // for Image widgets
```

The generated `imforge_dev.h` has these calls for every handler and locked block in your
project, so normally you include it instead of writing them yourself.

## Picker mode

Press the picker button in the Live panel (or `Ctrl+Shift+P`) and click a widget in your app:
it becomes the selection in the builder. Hovering highlights the matching hierarchy row.

## Several panels and instances

Call `Draw` once per panel per frame. Each call site gets its own interpreter state (window open
flag, tab selection, animation state), keyed by panel name plus the ImGui ID stack, so drawing
the same panel twice in different windows works.

## Troubleshooting

**Nothing appears.** Check the builder's Log panel for the client connecting. If there's
nothing, the app isn't connected: wrong URL, a Windows firewall prompt, or the builder listens
on a different transport.

**The app says "waiting for ImForge builder (no pairing file ...)".** The builder isn't
listening on that endpoint, or the app runs as another user or in a VM and can't see the file.
Start the builder's live server, or use `IMFORGE_PAIRING_CODE` as described above.

**"pairing failed: the builder could not prove it knows the pairing code".** The pairing file is
stale (a builder crashed and a different one is listening now) or something else owns the port.
The app keeps the code, prefers any other code it has, and retries every 2 seconds, re-reading
the file each time. It pairs as soon as the real builder answers. It deliberately doesn't throw
the code away, so a squatting process can't make it drop the right one.

**Status starts with "security:", or the Log shows a `security` entry.** A message on the paired
link failed its HMAC or sequence check, so something modified or injected traffic, or a
transport lost a frame. The link was dropped before anything from that message was applied, and
it re-pairs on its own. If it keeps happening, another process is interfering with the
endpoint; switch to `pipe://`.

**The builder log keeps showing "pairing attempt failed" from an app you don't know.** Some other
local process is probing the port. Its attempts are rate limited and nothing it sends is
applied.

**A panel shows a placeholder box.** A locked block or custom widget has no registered function
in the app. Include `imforge_dev.h` or call `RegisterWidget`.

**"stale" badge.** The code in the builder differs from what your app was compiled with. Rebuild
the app.

**Bindings show mock values.** The path couldn't be resolved against the registered reflection
table. Check the `IMFORGE_REFLECT` field list and the root names passed to `Draw`.

**Fonts look different.** The app renders with its own atlas. Load the project's fonts in the
app (`generated/imforge_fonts.h`) to match.

**Anti-cheat.** In a development build ImForge opens one local socket, pipe or mapping and reads
one pairing file, nothing else. Don't ship builds with `IMFORGE_LIVE` defined.

**Access violation in `std::mutex` as soon as the transport starts (Windows, MSVC).** The
`msvcp140.dll` in `System32` is older (say 14.15) than the toolset you compiled with (14.44) and
doesn't support the newer STL's `constexpr` mutex constructor. Install the current VC++
2015-2022 x64 redistributable (reboot if the installer claims a newer version is already
registered), or compile with `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`. ImForge's own CMake adds
that define to every MSVC target; add it to your app too if it uses `imforge_client.h` and
crashes in `Connect`.
