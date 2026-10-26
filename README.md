# Moch Browser

Moch is an AI-native web browser: a lean C++20 / Qt 6 desktop shell around
Chromium (QtWebEngine) with a built-in agent runtime that can read pages,
navigate, fill forms, and execute multi-step tasks — through explicitly typed
tools, with the user's permission.

You browse normally. The assistant works alongside you in a collapsible
sidebar, acting on the same visible tabs through a fixed set of validated
tools — never with unrestricted access to your machine, cookies, or files.

## Status

Working build (verified 2026-10-26, Arch Linux, Qt 6.11.2): 5 test suites,
74 test functions, all passing via ctest; app selftest green; wizard and
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
  multi-step tasks (`/ask <task>`) with a live tool timeline, approval
  prompts, and an always-visible Stop button.
- Agent runtime: 16 typed browser tools with JSON-Schema argument validation,
  per-tool timeouts, tool-call and wall-clock limits, cancellation, and
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
tst_provider     10 passed   (OpenAI wire format, error mapping, SSE streaming, key handling)
tst_security     38 passed   (sanitizer envelope, risk classifier, secret redactor, permission matrix)
tst_browsercore  10 passed   (URL resolution, settings roundtrip, history, bookmarks, tab lifecycle)
tst_agent         8 passed   (tool validation, dispatch errors, full agent loop with mock model,
                              approval gate, readonly deny, malformed-model recovery)
tst_setup         8 passed   (model catalog parsing, use-case registry, bridge roundtrip,
                              API key storage, OAuth callback server)
ctest: 100% tests passed (5 suites, 74 test functions)
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

