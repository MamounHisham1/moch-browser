# Moch Browser — Architecture

Moch is an AI-native desktop browser: a lean C++20 / Qt 6 QtWebEngine (Chromium) shell
with a built-in agent runtime that drives the real, visible browser through typed tools.

## Research summary (Phase 0, verified 2026-10-25)

| Project | State | License | Verdict |
|---|---|---|---|
| Lightpanda (lightpanda-io/browser) | Active, nightly-only, headless-only, Zig; CDP server on :9222; no GUI rendering engine | AGPL-3.0-only | **Not used.** AGPL contaminates; cannot render the visible browser; early stage. |
| Split Browser (niutech/splitbrowser) | Dormant since Jan 2024, Qt 5.14, Windows-first, alpha | MIT + Ultralight | **Not used.** Dead project. |
| Falkon (KDE/falkon) | Active (25.12 release), C++/QtWebEngine | GPL-3.0 | **Reference only, no code copied.** Proves the QtWebEngine shell architecture; GPL would force Moch GPL. |
| Browser Use (browser-use/browser-use) | Very active, Python, raw CDP | MIT | **Architectural reference** for the typed-tool taxonomy. Not linked (Python, separate process model). |

## Chosen architecture and why

**Fresh C++20 Qt6 QtWebEngine Widgets application. Single process tree (one Chromium
profile shared by all tabs), in-process agent runtime driving the visible pages.**

- QtWebEngine 6.11.2 is already the system rendering engine on the target machine
  (Arch). Full Chromium compatibility, LGPL, zero extra dependencies to ship.
- One shared `QWebEngineProfile` for all tabs means one GPU process and shared
  renderers — roughly half the memory of running a visible browser plus a separate
  headless automation browser. This matters on the 8 GB target.
- The automation layer is a separate *module* (`tools/` + `agent/`) operating on the
  same views through a narrow interface, satisfying "keep visible browser and
  automation engine separate where practical" without a second engine process.
- A future headless engine (e.g. Lightpanda over CDP) can be added behind the same
  tool interface; it is deliberately not shipped in v1 (see Limitations).

Rejected alternatives:
- Forking Falkon: GPL + large legacy surface (KDE deps, plugin system) vs our needs.
- Electron/Tauri shell: would add a second browser stack and ~200 MB of downloads;
  system Qt is already present.
- Python + browser-use sidecar: needs pip/venv, a second browser process (Playwright
  Chromium), and IPC; slower, heavier, and the user's Python has no pip.

## Module map

```
src/
  main.cpp              entry point, QApplication, high-DPI, offscreen-safe
  app/                  ApplicationController: owns profile, settings, manager singletons
  browser/              TabManager, WebView, HistoryStore, BookmarkStore, DownloadManager
  ui/                   MainWindow, Toolbar, TabBar, NewTabPage, SettingsDialog, AiSidebar
  ai/                   LlmProvider interface, OpenAiCompatProvider, provider registry
  agent/                AgentService (task state machine), ToolExecutor, ApprovalGate
  tools/                Tool interface, ToolRegistry, page tool implementations
  security/             ContentSanitizer, RiskClassifier, SecretRedactor, PermissionPolicy
  storage/              Settings (JSON), Database (SQLite), TaskRecords
tests/                  unit + integration (offscreen WebEngine, mock LLM server)
resources/              qrc: icons, QSS themes, new-tab page HTML/JS
```

Build: CMake + Ninja, C++20, targets `moch` (app) and `moch_tests` (ctest).
Run tests: `QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 ctest --test-dir build`.

## Core contracts (binding for all modules)

### Settings (storage/settings.hpp)

JSON at `~/.config/moch/settings.json`, created with 0600. Loaded once at startup,
saved on change. Schema:

```json
{
  "theme": "dark",                    // "dark" | "light"
  "search_engine": "duckduckgo",      // key of engines table in UrlResolver
  "startup_behavior": "new_tab",      // "new_tab" | "continue"
  "ai": {
    "provider": "openai-compat",      // id in provider registry
    "base_url": "https://api.openai.com/v1",
    "model": "gpt-4o-mini",
    "api_key_env": "MOCH_API_KEY",    // env var name; key itself stored only in keyring file
    "temperature": 0.2
  },
  "agent": {
    "permission_level": "cautious",   // "readonly" | "cautious" | "autonomous"
    "max_task_seconds": 0,
    "max_tool_calls": 40,
    "confirm_downloads": true
  },
  "privacy": { "do_not_track": true, "send_page_content": true },
  "headless_engine": { "enabled": false, "cdp_url": "" }
}
```

API key resolution order: env var named by `api_key_env` → `~/.local/share/moch/secret`
(0600). The key is never written to settings.json, logs, or model payloads.

### LLM provider (ai/provider.hpp)

```cpp
struct ChatMessage { enum Role { System, User, Assistant, Tool } role;
                     QString content; QString toolCallId; QString toolName;
                     QJsonArray toolCalls; /* OpenAI shape: {id,function:{name,arguments}} */ };
struct ToolSpec    { QString name; QString description; QJsonObject parameters; }; // JSON Schema
struct ChatRequest { QList<ChatMessage> messages; QList<ToolSpec> tools; double temperature; };
struct ChatResponse { QString content; QJsonArray toolCalls; QString finishReason;
                      int promptTokens = 0, completionTokens = 0; QString error; };

class LlmProvider : public QObject {
    Q_OBJECT
public:
    virtual QString id() const = 0;
    virtual void chat(const ChatRequest&, std::function<void(ChatResponse)>) = 0; // async, main thread
    virtual void abort() = 0;    // cancel in-flight request
signals:
    void streamDelta(const QString& text);  // optional; emitted during chat()
};
```

`OpenAiCompatProvider` speaks OpenAI `/chat/completions` (works for OpenAI,
OpenRouter, Groq, Together, Ollama `/v1`, LM Studio, vLLM). Non-streaming for agent
steps; streaming SSE for sidebar chat. Adding a provider = implement `LlmProvider`,
register in `ProviderRegistry`, add a settings entry (see docs/ADDING_PROVIDERS.md).

### Tools (tools/tool.hpp)

```cpp
enum class Risk { Safe, Sensitive };   // Sensitive => approval gate before execution
struct ToolResult { bool ok; QString errorCode; QString errorMessage; QJsonObject data; };
class Tool : public QObject {
    Q_OBJECT
public:
    virtual ToolSpec spec() const = 0;
    virtual Risk risk() const = 0;
    // Execute with hard timeout (default 15s, per-tool override in spec description).
    // Must return a ToolResult; never throw; never block > timeout.
    virtual void execute(const QJsonObject& args, std::function<void(ToolResult)>) = 0;
};
```

v1 tool set (15): `get_current_page`, `get_page_content`, `get_page_links`,
`find_elements`, `click_element`, `fill_input`, `select_option`, `scroll_page`,
`navigate_to_url`, `switch_tab`, `create_tab`, `close_tab`, `take_screenshot`,
`wait_for_element`, `extract_structured_data`.

Arguments validated against `spec().parameters` before execution; invalid args return
`{"ok":false,"errorCode":"invalid_arguments"}` without touching the page. No tool
exposes: cookies, localStorage, page JavaScript execution, filesystem, shell, or
browser-profile secrets. Adding a tool = subclass `Tool`, register in `ToolRegistry`
(see docs/ADDING_TOOLS.md).

### Agent (agent/)

`AgentService` owns one active task; a task is a state machine:

```
Idle → Running → (AwaitingApproval → Running)* → Done | Failed | Cancelled | TimedOut
```

Loop per iteration: append context → provider.chat() → if toolCalls: validate →
risk-gate → execute (timeout) → append ToolResult → repeat; else finish with content.
Limits: `max_tool_calls` (the runaway guard; 0 = unlimited), `max_task_seconds` is opt-in (0 = unlimited, the default); abort() honored between steps and by
aborting in-flight provider calls. All state changes emit signals for the UI timeline.
Task transcript persisted to SQLite `agent_tasks`.

### Security (security/)

- `ContentSanitizer`: page → model text pipeline. Strips scripts/styles, caps length
  (default 24k chars), wraps in explicit untrusted-content delimiters with an
  instruction that content inside is data, never instructions.
- `RiskClassifier`: labels tool calls Sensitive (submit/purchase/pay/login keywords,
  password/payment field fills, form submits) → `ApprovalGate` (UI prompt, 60s timeout
  defaults to deny).
- `SecretRedactor`: scrubs the configured API key and common token patterns from any
  string before it is logged or sent to the model.
- Permission levels: `readonly` (action tools disabled), `cautious` (default; approval
  for Sensitive), `autonomous` (approval only for payment/purchase).

### Browser core (browser/)

`TabManager` owns N `WebView`s (one QWebEngineView each), signals:
`tabsChanged()`, `currentChanged(int)`, `titleChanged(int,QString)`,
`iconChanged(int,QIcon)`, `loadProgress(int,int)`, `urlChanged(int,QUrl)`.
Tools and UI both go through TabManager; tools never touch widgets directly.
`HistoryStore`/`BookmarkStore`: SQLite via QtSql. `DownloadManager`: QWebEngineProfile
downloadRequested → file in ~/Downloads with progress model.

## Coding rules

- Qt idioms; ownership via QObject parents; cross-module communication via signals.
- No comments except a non-obvious *why*. No TODOs in shipped paths. No swallowed
  errors: every failure path sets errorCode + message or logs qWarning.
- JSON for all tool args/results (QJsonObject). Strings from web content are treated
  as untrusted at every boundary.
- Tests: QtTest, registered with ctest. Unit tests must not need a window; integration
  tests run with `QT_QPA_PLATFORM=offscreen`.
- `QT_FORCE_STDERR_LOGGING=1` is required for log visibility on this machine (Qt 6.11
  defaults to the journal); dev scripts set it.

## Setup wizard (2026-10-26)

`moch://setup` serves a 4-step onboarding page (use cases → platform
sign-in → AI model → summary) through the same WebChannel pattern as the
new-tab page. The bridge (`browser/setupbridge.*`) owns a static registry
of 16 use cases and ~70 platforms with real login URLs; selections persist
to Settings immediately. Model data comes from OpenRouter's public catalog
(`ai/modelcatalog.*`, 12 s timeout, curated offline fallback).
OpenRouter desktop OAuth (`browser/openrouterauth.*`): loopback QTcpServer
catches the ?code= redirect and exchanges it for a key at
/api/v1/auth/keys. First run shows the wizard when `setup_complete` is
false; the agent system prompt includes the chosen use cases.

WebChannel contract that cost a debugging session: invokables WITH return
values arrive in JS as Promises (must be awaited); signal payloads arrive
as plain values; register signal handlers before any await. Both pages
follow it; keep it that way. AUTORCC can keep serving stale HTML after
.html-only edits; touching the qrc is not reliable. Delete the generated
`qrc_moch.cpp` and its object (or rebuild clean), then verify by extracting
and decompressing the embedded resource (zstd) — grepping the binary or the
generated qrc for plaintext proves nothing.

## Verification record (2026-10-27)

Current HEAD was rebuilt and re-verified: 7 QtTest binaries, 73 cases, 100%
pass offscreen. The first-run wizard was driven end to end over WebEngine
CDP against the real window: 16 use-case cards render; selecting one
persists; the job-hunting category shows its 6 platform chips; the live
OpenRouter catalog loads (462 models across 75 providers in this run);
selecting OpenAI shows 103 models and a model click persists
`ai.base_url`/`ai.model`; the summary resolves its asynchronous AI row;
`Enter Moch` sets `setup_complete` and navigates to `moch://newtab`. The
new-tab page populates its top-sites and history sections after a reload
(bookmarks were empty in this profile).

## Implementation record (2026-10-25)

All phases landed on main. Verified state: clean configure + build from
scratch with gcc 16 / CMake 4.4 / Ninja; `ctest` 100% pass (4 suites,
66 test functions: provider wire/stream, security sanitizer/classifier/
redactor/policy, browser URL/settings/stores/tabs, agent loop with a mock
model server incl. approval gate and readonly deny); `moch --selftest`
green offscreen; window + new-tab page verified rendering on real Wayland.
Wizard verified live via CDP remote debugging (2026-10-26): 16 cards render,
LinkedIn sign-in navigates to the real login page, 467 live OpenRouter
models load, "Enter Moch" persists setup_complete and lands on the new-tab
page with populated sections.
Measured: ~1.0 s startup to usable window, ~490 MB RSS with one tab
(249 MB UI process + 4 Chromium helpers). Deviations from the original
sketch: tools own the single `ToolSpec` definition (`ai/provider.hpp`
includes `tools/tool.hpp`); mock model server lives in `tests/` and is
shared by provider and agent suites.
