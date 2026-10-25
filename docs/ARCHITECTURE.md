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
    "max_task_seconds": 300,
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
Limits: `max_tool_calls`, `max_task_seconds`, abort() honored between steps and by
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
