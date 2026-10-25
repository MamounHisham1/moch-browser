# Moch Browser

Moch is an AI-native web browser: a lean C++20 / Qt 6 desktop shell around
Chromium (QtWebEngine) with a built-in agent runtime that can read pages,
navigate, fill forms, and execute multi-step tasks — through explicitly typed
tools, with the user's permission.

You browse normally. The assistant works alongside you in a collapsible
sidebar, acting on the same visible tabs through a fixed set of validated
tools — never with unrestricted access to your machine, cookies, or files.

## Status

Under active development. Phase tracking and the architectural record live in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Known limitations are listed at
the bottom of this file.

## Features

- Real Chromium rendering via QtWebEngine (one shared profile for all tabs).
- Tabs, address bar with search, back/forward/reload, history, bookmarks,
  downloads with progress, keyboard shortcuts, dark/light themes.
- New-tab page with top sites, bookmarks, and recent history.
- AI sidebar: chat about the current page, summarize, question answering, and
  autonomous multi-step tasks with a live tool timeline.
- Agent runtime: typed browser tools, planning, cancellation, timeouts,
  retry/step limits, and approval gates for sensitive actions.
- Any OpenAI-compatible model provider (OpenAI, OpenRouter, Groq, Together,
  Ollama, LM Studio, vLLM) — configurable at runtime, no rebuild.
- Security: page content is passed to the model as untrusted data inside
  delimiters; secrets never leave the machine; sensitive actions require
  approval; the agent can be stopped instantly.

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
- No extensions, no profiles manager, no sync.
- Very long pages are truncated before being sent to the model (cap is
  configurable in code).
