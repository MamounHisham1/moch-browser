# Moch Browser

Moch is an AI-native web browser: a lean C++20 / Qt 6 desktop shell around
Chromium (QtWebEngine) with a built-in agent runtime that can read pages,
navigate, fill forms, and execute multi-step tasks — through explicitly typed
tools, with the user's permission.

You browse normally. The assistant works alongside you in a collapsible
sidebar, acting on the same visible tabs through a fixed set of validated
tools — never with unrestricted access to your machine, cookies, or files.

## Status

Working build (verified 2026-10-27, Arch Linux, Qt 6.11.2): 7 test suites,
73 test cases, all passing via ctest; app selftest green; wizard and
new-tab flows verified live via CDP against the real window. Phase tracking
and the architectural record live in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Known limitations are listed at
the bottom of this file.

## First-run setup

On first launch Moch opens a setup wizard (`moch://setup`, reopenable from
the menu via "Personalize Moch…"): pick what you want Moch for (16
categories — job hunting, socials, email, shopping, research, news, travel,
learning, finance, freelance, real estate, food, entertainment, dev, sports,
custom), sign in to the platforms you use (the real login page opens in
Moch; passwords stay between you and the site), then pick an AI model — the
OpenRouter catalog loads live (400+ models, free ones badged) with one-key
OAuth or paste, or connect a provider directly (OpenAI, DeepSeek, Groq,
Mistral, Together, xAI, Ollama, LM Studio, custom). Your selections feed the
agent's context.

## Features

- Real Chromium rendering via QtWebEngine (one shared profile for all tabs).
- Tabs (favicons, close, reorder, restore), address bar with search and
  security indicator, back/forward/reload-stop, find-in-page, history,
  bookmarks, downloads with progress panel, settings dialog, dark/light
  themes, keyboard shortcuts.
- New-tab page with top sites, bookmarks, recent history, and a search field
  (WebChannel bridge into the browser).
- AI sidebar (`Ctrl+I`): chat about the current page, and autonomous
  multi-step tasks — action requests run the agent automatically (`/ask`
  forces it) — with a live tool timeline, approval
  prompts, and an always-visible Stop button.
- Agent runtime: 16 typed browser tools with JSON-Schema argument validation,
  per-tool timeouts, a tool-call cap, an opt-in wall-clock limit (off by
  default), cancellation, and
  approval gates driven by the permission policy (readonly / cautious /
  autonomous).
- Any OpenAI-compatible model provider (OpenAI, OpenRouter, Groq, Together,
  Ollama, LM Studio, vLLM) — configurable at runtime, no rebuild.
- Security: page content is sanitized and passed to the model as untrusted
  data inside an explicit envelope; API keys are read from the environment or
  a 0600 file and are redacted from logs; sensitive actions (submit/purchase/
  payment/login) require explicit user approval; `file://` and other
  non-http(s) navigation is blocked for the agent.

## Measured performance

On the development machine (Arch Linux, Wayland, 8 GB RAM, Qt 6.11.2):
startup to a usable window with one new-tab page takes about 1.0 s
(`--selftest` runs, which include a fixed 1.5 s settle, complete in
~2.5 s wall clock). Total RSS with one new-tab open is ~490 MB across 5
processes (249 MB UI process + 4 Chromium helper processes). These are
measured numbers from `/proc/<pid>/status` on a release build, not
projections.

## Test results (executed 2026-10-25)

```
tst_provider      8 passed   (OpenAI wire format, error mapping, SSE streaming, key handling)
tst_security     38 passed   (sanitizer envelope, risk classifier, secret redactor, permission matrix)
tst_browsercore   9 passed   (URL resolution, settings roundtrip, history, bookmarks, tab lifecycle)
tst_agent         7 passed   (tool validation, dispatch errors, full agent loop with mock model,
                              approval gate, readonly deny, malformed-model recovery)
tst_setup         7 passed   (model catalog parsing, use-case registry, bridge roundtrip,
                              API key storage, OAuth callback server, wizard completion)
tst_navtool       2 passed   (page tools against a live offscreen tab: find_elements matches,
                              navigation timeout survives timer fire)
tst_taskdetect    2 passed   (sidebar router: action phrasings reach the agent, questions stay chat)
ctest: 100% tests passed (7 suites, 73 test cases)
```

## Build

Requirements: Linux, C++20 compiler, CMake ≥ 3.22, Ninja, and Qt 6 with
modules Core, Gui, Widgets, WebEngineWidgets, Network, Sql, WebChannel, Test
(on Arch: `pacman -S qt6-base qt6-webengine qt6-webchannel cmake ninja`).

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/src/moch
```

Run the test suite (offscreen, no display needed):

```sh
QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 ctest --test-dir build --output-on-failure
```

`--selftest` starts the app and exits after successful startup (used in CI).
Qt 6.11 on some systems logs to the journal by default; the
`QT_FORCE_STDERR_LOGGING=1` environment variable restores stderr logs.

## Packaging (AppImage)

```sh
./packaging/make-appimage.sh            # version comes from CMakeLists.txt
./packaging/make-appimage.sh 0.1.0      # or pass one explicitly
```

Output lands in `dist/`: `Moch-<version>-x86_64.AppImage`, its `.sha256`, and —
when `UPDATE_URL` is set — a `.zsync` delta file.

The script needs `appimagetool`; it is not packaged for Arch, so unpack the
official AppImage once:

```sh
curl -LO https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x appimagetool-x86_64.AppImage
./appimagetool-x86_64.AppImage --appimage-extract
mkdir -p ~/.local/opt && mv squashfs-root ~/.local/opt/appimagetool
```

What the script does, and why it is cheap to re-run:

- **Incremental build.** Only changed translation units recompile. Job count is
  sized from `MemAvailable` because a single QtWebEngine-heavy unit peaks near
  1.2 GB; `-j$(nproc)` on an 8 GB machine gets `cc1plus` OOM-killed.
- **Cached staging.** The ~430 MB of Qt/Chromium payload is copied, never
  compiled. Staging is keyed on the Arch package versions it copies from, the
  built binary, and the script's own hash, so a code-only release skips it and
  costs one link plus a squashfs pack (about a minute).
- **Bundled:** Qt itself, QtWebEngine + `QtWebEngineProcess`, Chromium's `.pak`
  resources and locales, SQLite/TLS/image/QPA plugins, i18n data, and every
  shared library they need.
- **Not bundled, by design:** glibc and the loader, the graphics driver stack
  (GL/EGL/DRM/GBM/Vulkan/NVIDIA), the X11 and Wayland client libraries, and
  audio/session buses. Those belong to the host session.
- **Verified.** The script unpacks `usr/bin/moch` back out of the finished
  image and compares it byte-for-byte with the binary it just built, so an
  empty or half-packed image fails the release instead of shipping.

Deliberate caveats:

- The bundled Chromium is frozen at packaging time. Every `qt6-webengine`
  upgrade on the build host is a Chromium security update that must be
  repackaged to reach users; `moch --version` reports the Chromium version
  inside a given build.
- Chromium's SUID sandbox cannot be installed inside a read-only squashfs, so
  QtWebEngine uses the user-namespace sandbox. On kernels with unprivileged
  user namespaces disabled, run with `QTWEBENGINE_DISABLE_SANDBOX=1` — that
  trades the renderer sandbox for a working browser. Moch's own tool
  permissions, approval gates and secret redaction are unaffected either way.
- The AppImage is built against the host's glibc, so it targets Arch-like
  distributions rather than "any Linux". Building in an older container is the
  fix if that matters.

## Configuration

Settings live in `~/.config/moch/settings.json` and in the Settings dialog
(`Ctrl+,`). The model API key is read from the environment variable named by
`ai.api_key_env` (default `MOCH_API_KEY`) or from
`~/.local/share/moch/secret` (chmod 600). See `.env.example`. The key is never
written to settings.json, logs, or model payloads.

## Extending

- [Adding or switching model providers](docs/ADDING_PROVIDERS.md)
- [Adding browser tools](docs/ADDING_TOOLS.md)
- [Architecture and decisions](docs/ARCHITECTURE.md)

## License

MIT (see LICENSE). Qt is LGPL-3.0; QtWebEngine embeds Chromium (BSD-style
licenses, see Qt's attribution). Moch's own code reuses nothing from Falkon
(GPL), Lightpanda (AGPL), Split Browser, or Browser Use.

## Known limitations

- Linux only for now (Wayland and X11). Packaging beyond Arch is not done.
- The optional headless engine (e.g. Lightpanda over CDP) is a settings
  placeholder; v1 automation drives the visible browser only.
- No extensions, no multi-profile manager, no sync.
- Very long pages are truncated before being sent to the model (default cap
  24 000 chars for agent tools, 12 000 for sidebar chat).
- The agent drives pages via dispatched DOM events, not trusted input
  pipelines; a small number of sites that require isTrusted keyboard events
  will not accept agent-typed text.
- Streaming chat and the agent cannot run at the same time (one active task);
  concurrent tasks are queued out by design.
- Downloads are accepted into ~/Downloads without a per-download prompt
  (setting exists, per-file dialog not yet wired).
- The AI sidebar streams tokens but does not yet render markdown; tool-call
  streaming progress is shown at step granularity.

