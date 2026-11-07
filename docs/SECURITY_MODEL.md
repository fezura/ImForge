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
- Generated code embeds `Code` node bodies verbatim, by design, and string-literal escaping in
  codegen hasn't been audited. Treat code exported from someone else's project as untrusted
  source and read it before compiling.
- The automation `export <dir>` command and the export self-check count as explicit user intent
  and skip the external output directory confirmation.
