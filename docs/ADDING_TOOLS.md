# Adding a browser tool

Agent tools are the only way the language model can act on the browser. Each
tool is a small, typed, validated operation — the model never gets raw
JavaScript, shell access, or filesystem access.

## The contract

A tool subclasses `Tool` (`src/tools/tool.hpp`):

```cpp
class MyTool : public Tool {
    Q_OBJECT
public:
    ToolSpec spec() const override;   // name, description, JSON-Schema parameters
    Risk risk() const override;       // Safe or Sensitive (approval gate)
    void execute(const QJsonObject &args, std::function<void(ToolResult)> done) override;
};
```

- `spec().parameters` is a JSON Schema object; arguments are validated against
  it before `execute` runs. Invalid arguments return
  `{"ok": false, "errorCode": "invalid_arguments"}` without touching the page.
- `execute` must call `done` exactly once, always — including on failure — and
  never block longer than the documented timeout (default 15s). Use the
  page/tab handles given to you by `ToolRegistry`; do not reach into widgets.
- Return data as JSON (`ToolResult::data`). Keep payloads bounded (a page's
  text is capped; element lists are capped) so the model's context is not
  flooded.
- Mark the tool `Sensitive` if it can spend money, submit forms, authenticate,
  or send messages. The `RiskClassifier` in `src/security/` and the permission
  level decide whether the user is asked first.

## Registering

Add the class in `src/tools/pagetools.cpp` (or a new file there) and register
an instance in `ToolRegistry::registerBuiltinTools()`. The registry exposes
specs to the provider (function calling) and dispatches validated calls.

## Testing

Add cases to the tool tests: valid args produce the expected structured
result against a `data:` URL page (offscreen WebEngine), invalid args produce
`invalid_arguments`, and a timeout/missing-element case produces a clean
error, never a hang. Run:

```sh
cmake --build build -j$(nproc)
QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 ctest --test-dir build --output-on-failure
```
