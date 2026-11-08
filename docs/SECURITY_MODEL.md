# Security model

ImForge runs in two places that care about security: the builder on your desk, and the live
client inside your app (often a game). Both read data that may come from somewhere else. This
note covers what we defend against, how, and what we don't.

## What we worry about

Three kinds of input are untrusted.

The live endpoint. The builder listens on a loopback TCP port, a named pipe, a unix socket or a
shared memory mapping. Another user on the same machine can try to connect to it or grab the
port or pipe name before the builder does, and if someone changes the bind host, so can other
machines. A hostile builder could push content into your game; a hostile app could drive the
builder's selection, spam its log or feed it fake block hashes that lead to source rewrites.

Project folders and `.imblib` archives. People download them. Their JSON names files, output
directories and templates, and a zip can contain whatever its author wants.

Exported code. It lands in your source tree and gets compiled.

Processes running as your own user are trusted. They can read the pairing file, but they could
also read and write your app's memory directly, so the live link gives them nothing new. The
same goes for an administrator.

## The live link

### Transports

TCP listens on `127.0.0.1` by default; an empty host also means loopback. Listening on anything
else needs an explicit bind host in the builder's Preferences, and then the builder prints a
warning. On Windows the listener uses `SO_EXCLUSIVEADDRUSE` and refuses to start without it, so
another process can't bind the same port with `SO_REUSEADDR` and take over connections.

The Windows named pipe has a DACL that only grants the current user, rejects remote clients,
and is created with `FIRST_PIPE_INSTANCE`. The client connects with identification-level
impersonation only and checks that the server process runs as the same user. On Linux the unix
socket lives in `$XDG_RUNTIME_DIR` when it's set (otherwise `/tmp`), is created 0600 under a
tight umask, and the server only removes an existing path if it's a socket owned by the caller.
The client checks the socket's owner, the server checks the peer's uid with `SO_PEERCRED`.
Shared memory objects must be owned by the current user, and the client refuses a segment that
is writable by group or others. The shm reader never trusts head, tail or record lengths
written by the peer beyond what its own validated header allows.

### Pairing

None of that stops another local user from occupying the endpoint first, so every link starts
with pairing. The builder generates a one-time code from the OS CSPRNG (`BCryptGenRandom` on
Windows, `getrandom` with a `/dev/urandom` fallback elsewhere). If the RNG fails there's no code
and no pairing; nothing falls back to `rand()` or the time.

The code is written to a per-user file, `%LOCALAPPDATA%\ImForge\live\<endpoint>.pair` on
Windows or `$XDG_RUNTIME_DIR/imforge/` (else `~/.cache/imforge/`) on Linux. On Windows the file
and its directories are owned by the current user with a protected DACL that grants only that
user; reparse points are refused. On Linux the directory is 0700 and the file 0600, created with
`O_EXCL|O_NOFOLLOW` and renamed into place. The reader opens without following links, checks
the owner and permissions on the open handle, refuses group or other bits and anything over
4 KB. Endpoint names are reduced to `[A-Za-z0-9_.-]` with a fixed `tcp-`/`pipe-`/`shm-` prefix,
so a URL can't point the path anywhere else. The app reads the file before each connect, so
you never type the code. If the app can't see the file (VM, sandbox, other account), the code
can be passed through `IMFORGE_PAIRING_CODE` or `SetPairingCode()`.

The code is 160 bits (32 Crockford base32 characters). It was going to be 40, but whoever sends
the `hello` gets a `mac_b` they can test guesses against offline, and 40 bits doesn't survive a
GPU for the code's 10 minute life. The file makes length free.

Both sides then prove they know the code without sending it: K = SHA-256 of the normalized
code with a domain prefix, and each side sends an HMAC-SHA256 over both nonces and a random
session id, with a different label per direction so a MAC can't be reflected back. The app
checks the builder's MAC first, in constant time, so a squatter on the port can't push panels.
Until `pair_ok` the builder only accepts `hello` and `pair_response`, the app only the
handshake replies, and the app sends nothing but a bare `hello` with its nonce. Its name, pid,
widget and texture lists go out later in `client_info`, after the builder is authenticated.

A code works once. The builder rotates it right after a successful pairing, after 10 minutes
unused, after 5 failed attempts (and then refuses connections for 3 seconds) or when you click
Rotate now. The failure counter lives in the server, not the connection, so reconnecting
doesn't reset it. Only a wrong or malformed `pair_response` counts as a failure. A peer that
disconnects after the challenge has guessed nothing, so it's logged and ignored; otherwise a
squatter relaying the app could burn the builder's attempts. Likewise the app only throws a
code away once it has paired with it. A code the peer couldn't prove is kept, ranked behind any
other code, and retried every 2 seconds with the file re-read each time. Handshake keys and
nonces are wiped after pairing and the file is deleted when the server stops.

The "Require pairing" switch is a per-user builder preference, not part of the project, so a
downloaded project can't turn authentication off. With it off the Live panel shows a red
UNAUTHENTICATED badge.

### After pairing

Every message in both directions is sealed with a MAC and a sequence number:
`MAC (32 bytes) || seq (8 bytes) || JSON`, where the MAC is HMAC-SHA256 over the sequence number
and the exact JSON bytes, keyed per direction with HKDF from K, both nonces and the session id.
The sequence number has to be exactly the next one. MAC and sequence are checked before any
JSON is parsed, so a tampered, injected, unsealed, replayed, reordered or dropped frame is
caught before it does anything. The receiver drops the connection, wipes the keys and reports
a security event, and the app re-pairs with the next code. The exact construction is in
[SPEC.md](SPEC.md#session-channel).

There's no encryption, on purpose. The transports are local, the payload is UI definitions,
theme data and app metadata, and the only parties who can watch loopback or pipe traffic (the
same user, an admin) can read that data anyway. What an attacker on the path must not be able
to do is change or inject messages, and the MACs cover that. A cipher would add a lot of code to
a header-only library without stopping anyone new. If you switch the builder to listen on a
non-loopback address, the traffic is readable on the network; keep that to networks you trust.

### Size limits

The builder and the app parse JSON from each other, so everything is bounded. A transport frame
over 32 MB marks the stream as corrupt before any of it is buffered. Protocol payloads are
capped at 16 MB, and the JSON parser stops at 256 levels of nesting and 500k values. Before
pairing the limit is 4 KB per frame on both sides. Peer-supplied lists are capped (4096
entries, 20000 blocks), names are clipped to 256 characters, log text to 4096, and everything
the builder displays from the app is stripped of control characters and escape sequences.
`select` and `hover` are ignored unless the node exists.

On the app side, a builder can push at most 256 panels, 1024 components and 128 themes, with
20000 nodes and 64 levels per document; `NewFrame` handles at most 256 messages. The
interpreter stops recursing at 64 levels with an error instead of overflowing the game's stack.

### Fonts over the link

The builder sends the project's font files so the app can draw with them. It only reads files
that, after resolving symlinks, are inside the project folder or the builder's own fonts folder;
an absolute path or a link pointing anywhere else is skipped with a warning, so a downloaded
project can't make the builder read and ship an arbitrary file. Each file goes out in 192 KB
`font_data` chunks named by its SHA-256.

The app treats the bytes as hostile. A file may be at most 8 MB, the app keeps at most 16 files
and 32 MB, a `fonts` list holds at most 32 entries with sizes between 4 and 256 px, and chunks
must arrive in order and add up to the declared size. The joined file must match its hash and
then pass a structural check before Dear ImGui's stb_truetype sees it, because stb_truetype does
no bounds checking of its own. The check accepts plain TrueType only (no `.ttc` collections, no
CFF outlines, which stb parses against a fixed 512 MB window). It verifies the table directory
and every table stb reads against the file size, runs stb's own cmap lookup for every BMP code
point with bounded reads and requires each result to be a real glyph, walks every glyph outline
and composite record, refuses composites that point at themselves or nest deeper than 8, and
rejects inverted or absurd glyph boxes and line heights that would turn into negative or huge
rectangles in the atlas packer. stb expands a composite into one vertex array, so the check also
adds up what every glyph expands to: at most 32768 vertices for one glyph and 32M for the whole
font, which stops a handful of nested composites from becoming billions of points. A format 6
cmap has to stay inside the BMP, because the lookup loop only visits code points up to 0xFFFF
while stb reads whatever a 32-bit `ImWchar` asks for. One quirk is repaired instead of refused: a last contour made of a
single off-curve point makes stb read one point past its buffer, and real fonts like Consolas and
Calibri have such glyphs, so the app marks that point on-curve (a one point contour has no area
either way). A font with no glyph in its ranges is skipped too, since Dear
ImGui before 1.92 asserts on that. On top of the check, each font is first built in a throwaway
atlas, and only added to the app's atlas if that worked. Before 1.92 the atlas rasterizes every
glyph in a font's ranges up front, so a 256 px font with big glyph boxes could ask for a
multi-gigabyte texture. The app adds up the pixels each new font would need from the real glyph
boxes and refuses fonts once all live fonts together pass 32M pixels (about an 8192 by 4096
texture). From 1.92 on glyphs are built on demand and this budget doesn't apply. Fonts are added
outside the frame in `PreNewFrame()` and never removed, so `ImFont*` pointers the app holds stay
valid; at most 64 get added over the life of the app. Font data handed to the atlas is a copy it
owns, and the glyph range arrays it keeps pointers to live as long as the client. Call
`PreNewFrame()` right before the backend's `NewFrame()`, never between `ImGui::Render()` and the
renderer drawing that frame: a rebuild recreates the font texture, and draw data from before it
still points at the old one. The `icons` message that maps icon names to code points holds at
most 8192 entries; names are lowercase letters, digits and `-` up to 64 characters, and code
points must be printable (no control characters, 0x7F to 0x9F included) and outside the
surrogate range, otherwise the whole message is dropped. Font names are stripped of control
characters before they can reach the app's log callback.

On the builder side the project's `fonts` list is untrusted too. At most 32 entries are looked
at per push, reading stops once the files add up to more than 32 MB, and files the current plan
doesn't use are dropped from the cache. A client's `client_info` is only taken once per
connection, because each one makes the builder send the project and every font again; a client
that repeats it is ignored.

### What builder data can and can't do

Text from the builder is never used as a printf format: text widgets use `TextUnformatted` or
`"%s"`, and slider, drag and input `format` props go through a filter that allows a single
numeric conversion and nothing else. Handlers and locked blocks are looked up by exact name in
tables your app fills with `RegisterHandler`/`RegisterBlock`, so builder data can't name an
arbitrary function. `set` and `toggle` actions only reach fields listed in `IMFORGE_REFLECT`,
with bounds-checked indices.

Without `IMFORGE_LIVE` none of this is compiled into your app at all.

## .imblib import

An `.imblib` is a zip, and zips are a classic way to write files where they don't belong. The
importer rejects the whole archive if any entry name is absolute, has a drive letter, contains
`..`, a backslash, `//` or control characters, if an entry is a symlink or encrypted, if names
repeat, or if there are more than 4096 entries. Only `.json`, `.h`, `.hpp`, `.inl`, `.md`,
`.txt` and `.png` are extracted; anything else is skipped. Each entry may be at most 16 MB and
the archive 64 MB in total, checked against the declared sizes before extraction and again
against the bytes actually extracted.

The files written into the project don't take their names from the archive paths. Component
and widget names from the manifests are reduced to safe file names, widget files must end in
`.json` and headers in `.h`, `.hpp` or `.inl`. Every write goes through a check that the target
is inside the project directory, and refuses symlinks and symlinked parent directories.

## Animations, designs and widget manifests

Version 0.2 added keyframe animations, designs and widget packs that insert text into generated
code, so these got their own checks.

The live `animations` message is capped at 2048 entries, 32 tracks per animation, 64 keyframes
per track, 256 characters for names and tags, 4096 for descriptions and credits and 32 tags. The
same limits apply to animations inside a `project` message. Numbers are checked when an
animation is parsed: durations must be finite and within (0, 60] seconds, iterations an integer up
to 1000, `steps()` at most 100, spring and cubic-bezier values inside fixed ranges. Panels get
the same treatment for motion specs (`durationScale`, `delay`, `stagger`, `iterations`), and the
playback engine still clamps whatever reaches it, so a 1e-30 second duration or a NaN delay ends
up as "instant" instead of a NaN pose. Replacing a registered animation keeps the old one alive for
a while because playing scopes may still point at it; the pile of replaced animations is capped
at 64, and since a playing scope only compares the pointer, never dereferences it, dropping
one is safe.

Project files and designs are untrusted. A design's font file has to be a relative path with no
`..`, drive letter or backslash, otherwise the default font file is used and a warning is added.
When the builder resolves a project font it refuses network (UNC) paths and `..`; an absolute
local path still works, but the file only loads if it starts like a TrueType or OpenType font.
Design style class names are limited to letters, digits, `-` and `_`, and motion numbers go
through the same ranges as above. The `.imblib` format doesn't carry animations or designs;
only components, widget manifests and headers.

Anything that ends up in generated C++ is checked. Style class names that map to the same C++
identifier (`accent-button` and `accent_button`) are an error in the validator and in codegen
instead of producing two functions with one name. Animation names must be PascalCase to load,
their descriptions are never written out, and a motion naming a preset the builder doesn't ship
is skipped with a warning. The `codegen.namespace` and `codegen.runtimeHeader` settings from a
project fall back to `ui` and `imb_runtime.h` when they aren't a namespace name or a plain
header name. Panel and theme names in the `@imb:file` comment lose newlines, backslashes and
`?` so they can't end the comment.

Widget manifests (bundled packs, `.imblib` archives, the live `widgets` message) must pass the
same check when they load. `include` is a plain header name: letters, digits, `_`, `.`, `/`
and `-`, ending in `.h` or `.hpp`, with no `..` and no leading slash. `call` and `callEnd` are a
single call, `ns::Function(...)`, whose arguments hold `{placeholders}` and literals; `;`,
braces outside placeholders, `#`, comments, backslashes, backticks and newlines are refused.
Export only copies pack headers whose names pass that check and whose resolved path stays inside
the pack folder.

Bundled packs come from the folders next to the executable, the install `share` folder and the
source tree (or `IMFORGE_PACKS_DIR` if you set it). A project folder never adds packs on its
own; custom libraries go through the `.imblib` import above. A broken pack manifest is skipped
with a warning.

Custom animations are saved by `SaveProject` under `animations/`, with the file name taken from
the animation name run through the same sanitizer as panels and themes, and the same
inside-the-project checks. A name like `../x` can't leave the folder.

## Writing files

Everything the builder writes is confined to the project, unless you say otherwise.

Paths in `imforge.json` (panels, components, themes) must be relative and stay inside the
project; absolute paths, drive letters, `..`, backslashes, `:` and control characters make the
load fail. Default file names derived from panel, component and theme names are sanitized, and
`SaveProject` checks the final path both lexically and after resolving it, so a symlinked
subdirectory that points outside the project is refused. The builder won't write through a
symlink planted in a downloaded project, and temporary files are removed before being recreated,
so a link at the temp name doesn't redirect the write. Deleting a panel only removes files
inside the project.

New projects from templates use a sanitized project name in file paths, and symlinks inside the
template are skipped.

Export writes into `codegen.outputDir`. Since a downloaded project chooses that value, a
directory outside the project is only used if you changed it in this session, approved it with
the "Allow this folder" button (remembered for the session, never saved into the project) or set
`IMFORGE_ALLOW_EXTERNAL_OUTPUT=1`. Otherwise export fails with a message saying why. Generated
file names that would escape the output directory are skipped with an error.

## Other inputs

`compile_commands.json` is treated as untrusted: `-load`, `-plugin`, `-add-plugin`,
`-fplugin=`, `-fpass-plugin=`, `-plugin-arg-*` (also behind `-Xclang`) and `@response` files are
dropped before anything reaches libclang. The binding tool never starts processes.

"Open folder" and link buttons only open `http(s)://` URLs and existing directories, never
files. The builder only starts external programs for that and for the native file dialog on
Linux (`xdg-open` or `open`, `zenity`), always with quoted arguments.

## Known limitations

- Same-user processes and administrators are trusted. Malware running as you can read the
  pairing file. The `IMFORGE_PAIRING_CODE` and Copy code fallbacks put the code in the
  environment (inherited by child processes) or the clipboard (clipboard history may keep it);
  the code is single use, but that's still wider exposure than the file.
- One peer at a time. A local attacker can hold the endpoint for 5 seconds at a time (the hello
  timeout), or burn 5 attempts to force a 3 second refusal and a new code. The app recovers
  after each window but can be kept waiting by a persistent attacker. The refusal deliberately
  doesn't grow, so the real app is never locked out for long.
- Anyone on the path can still drop frames or the connection. That shows up as a sequence gap
  or a disconnect, not as accepted data.
- If a peer stops reading and more than 64 MB queue up, the transport drops the oldest frames.
  With strict sequence numbers that becomes a sequence gap, reported as a security event and
  re-paired; the log message names the expected and received numbers so you can tell an
  overflow from tampering.
- A builder that crashes leaves its pairing file behind. The app tries that code, fails to
  verify the peer, and keeps waiting for a real builder.
- A same-user process can `ftruncate` the shared memory object under the mapping, which crashes
  the other side with SIGBUS.
- Wiping secrets can't reach copies left behind by string reallocation or JSON temporaries.
- `copyClipboard`, `set` and `toggle` actions from builder-supplied panels run when the user
  interacts with them. That's what they're for.
- Windows reserved device names (`CON`, `NUL`, ...) aren't special-cased when sanitizing file
  names.
- Generated code embeds `Code` node bodies verbatim, by design. A widget manifest can still
  name any function in its `call`, and an imported library ships headers that get compiled into
  your project, so the call grammar limits what a manifest can write but not what its header
  does. Treat code exported from someone else's project, and libraries from people you don't
  know, as untrusted source and read them before compiling.
- Absolute local font paths in a project file are still honored for the builder's own canvas.
  Only network paths and `..` are refused there. The live link never sends such a font to the
  app.
- stb_truetype is not safe against hostile fonts and we don't claim to make it so. The font check
  covers the tables and structures Dear ImGui makes stb read: the table directory, cmap (formats
  0, 4, 6, 12, 13), head, maxp, hhea, hmtx, loca, glyf and composites. It does not cover `kern`
  and `GPOS` (Dear ImGui never asks stb for kerning), `GDEF`, hinting instructions (stb skips
  them) or the rasterizer's own clipping. A font that passes can still render badly; it can't make
  stb read outside the buffer in the cases we know of, and the bytes only come from the paired
  builder anyway. If that matters to you, don't attach to a builder you don't trust, or leave
  `PreNewFrame()` out and the app never loads builder fonts.
- Font files are never evicted on the app side. After 16 files or 32 MB have been received, new
  fonts are refused until the app restarts. A hostile paired builder can use that up on purpose,
  and so can a long session of swapping font files.
- A paired builder can still make the app hash and check up to 256 chunks of font data per frame,
  which is a hitch, not a memory problem.
- The automation `export <dir>` command and the export self-check count as explicit user intent
  and skip the external output directory confirmation.
