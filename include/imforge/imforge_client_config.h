#pragma once

// imforge_client.h settings. Define before including it, or on the command line.

// shown in the builder, e.g. "dx11_win32", "gl3_sdl3"
#ifndef IMFORGE_BACKEND_NAME
#define IMFORGE_BACKEND_NAME "unknown"
#endif

// can be changed at runtime with SetAppName()
#ifndef IMFORGE_LIVE_APP_NAME
#define IMFORGE_LIVE_APP_NAME "app"
#endif

// distinct diagnostics remembered for de-dup, then the table is cleared and messages may repeat
#ifndef IMFORGE_LIVE_MAX_LOG
#define IMFORGE_LIVE_MAX_LOG 256
#endif

// ms between `stats` messages
#ifndef IMFORGE_LIVE_STATS_INTERVAL_MS
#define IMFORGE_LIVE_STATS_INTERVAL_MS 1000
#endif

// picker overlay colors (IM_COL32 layout)
#ifndef IMFORGE_LIVE_HOVER_COLOR
#define IMFORGE_LIVE_HOVER_COLOR 0xFF00C8FFu
#endif
#ifndef IMFORGE_LIVE_HOVER_FILL_COLOR
#define IMFORGE_LIVE_HOVER_FILL_COLOR 0x3000C8FFu
#endif
#ifndef IMFORGE_LIVE_OVERLAY_TEXT_COLOR
#define IMFORGE_LIVE_OVERLAY_TEXT_COLOR 0xFF5050FFu
#endif

// IMFORGE_LIVE_ASSERT(expr): used by Root<T>() / State<S>() on misuse.
// Defaults to IM_ASSERT with IMFORGE_LIVE, a no-op without.
