# Security policy

## What's in scope

ImForge is a desktop design tool plus a few headers that you compile into your own
application. The parts where security matters:

- The live attach link (`imforge_transport.h`): the builder's TCP server, the named pipe /
  Unix socket and the shared-memory ring. The builder listens on `127.0.0.1` unless you
  change the bind host in its preferences. Every link is paired with a one-time code and
  each message is authenticated, see [docs/LIVE_ATTACH.md](docs/LIVE_ATTACH.md).
- The client header's JSON and framing parsers, which run inside your application in
  development builds. Release builds compile the client to nothing.
- Project loading in the builder: JSON project files, `.imblib` archives (zip) and font files.
- Generated code, which should never contain anything that wasn't in your project.

## Reporting a vulnerability

Please don't open a public issue for security problems. Use GitHub's private vulnerability
reporting instead (the "Security" tab of the repository, then "Report a vulnerability").
Include the version or commit, the part that's affected (builder, client header, transport,
codegen), how to reproduce it and, if you have one, a suggested fix.

You'll get a reply within 7 days. We aim to publish a fix and an advisory within 30 days of
confirming the report, and we'll credit you unless you'd rather we didn't.

## Supported versions

Only the latest release and the `main` branch get security fixes.
