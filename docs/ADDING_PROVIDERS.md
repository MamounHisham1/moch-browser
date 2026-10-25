# Adding a model provider

Moch talks to language models through the `LlmProvider` interface (`src/ai/provider.hpp`).
The shipped implementation, `OpenAiCompatProvider`, works with any
OpenAI-compatible `/chat/completions` endpoint: OpenAI, OpenRouter, Groq, Together,
Ollama (`/v1`), LM Studio, vLLM, and most gateways.

## Configure an existing OpenAI-compatible provider

No code needed. In Settings (`Ctrl+,`) set the base URL and model, and store a key
either in the `MOCH_API_KEY` environment variable or in
`~/.local/share/moch/secret` (chmod 600). Example for a local Ollama:

```json
"ai": {
  "provider": "openai-compat",
  "base_url": "http://localhost:11434/v1",
  "model": "llama3.1",
  "api_key_env": "MOCH_API_KEY"
}
```

## Adding a new provider class

1. Create `src/ai/myprovider.hpp/.cpp` implementing `LlmProvider`:

```cpp
class MyProvider : public LlmProvider {
    Q_OBJECT
public:
    QString id() const override { return "myprovider"; }
    void chat(const ChatRequest &req, std::function<void(ChatResponse)> done) override;
    void abort() override;
};
```

2. Map `ChatRequest` to your wire format and the response back into
   `ChatResponse`. Tool calls use the OpenAI JSON shape
   (`{id, function:{name, arguments}}` where `arguments` is a JSON *string*).
   Keep everything asynchronous on the main thread; call `done` exactly once,
   including for errors (`ChatResponse::error` non-empty).
3. Register it in `src/ai/providerregistry.cpp`: map the `ai.provider` settings
   value to a constructor.
4. Add tests with a mock server (see `tests/unit` for the pattern used by the
   OpenAI client) covering: plain reply, tool-call reply, auth failure, and
   malformed response.

Rules that keep providers swappable: never read settings outside the
constructor; never log or echo the API key; always honor `abort()`; surface
provider errors verbatim (truncated) in `ChatResponse::error` so the UI can
show the user what failed.
