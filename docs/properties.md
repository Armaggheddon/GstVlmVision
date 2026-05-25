# Property Reference

## Full Property Table

| Property | Type | Default | Range | Description |
|---|---|---|---|---|
| `base-url` | string | `https://api.openai.com` | any URL | Base URL for the API endpoint |
| `api-key` | string | NULL | any string | API key; NULL = no auth header sent |
| `model` | string | `""` (must set) | any string | Model name (e.g. `gpt-4o`, `gemini-2.0-flash`) |
| `system-prompt` | string | NULL | any string | System prompt for the model |
| `user-prompt` | string | `"Describe what you see in this image."` | any string | User prompt about the image(s) |
| `profile` | string | `openai` | preset name | API compatibility profile |
| `analysis-interval` | double | 5.0 | 0.1--3600.0 | Seconds between analyses |
| `frames-per-request` | int | 1 | 1--10 | Images per API request |
| `request-mode` | enum | `chat-completions` | -- | API request type |
| `stop-sequences` | GStrv | NULL | string array | Strings that stop generation |
| `temperature` | double | 1.0 | 0.0--2.0 | Sampling temperature |
| `max-output-tokens` | int | 800 | 1--MAXINT | Max tokens to generate |
| `top-p` | double | 0.8 | 0.0--1.0 | Nucleus sampling cutoff |
| `output-mode` | flags | 3 (meta+signal) | 0--15 | Bitmask: 1=meta, 2=signal, 4=bus, 8=json |
| `timeout` | int | 30 | 1--600 | HTTP timeout in seconds |
| `max-inflight` | int | 1 | 1--16 | Max concurrent API requests |
| `queue-policy` | enum | `drop` | `drop` / `block` | Backpressure behavior |
| `error-policy` | enum | `skip` | `skip` / `bus` / `signal` | How API errors are reported |
| `template-body` | string | NULL | file path | Request body template file |
| `template-headers` | string | NULL | file path | Headers template file |
| `template-response` | string | NULL | JSON path | Response extraction JSON path |

---

## Property Details

### Endpoint Configuration

**`base-url`**
Base URL for the OpenAI-compatible API endpoint. Defaults to `https://api.openai.com`. Change to any `/v1/chat/completions`-compatible server (e.g., `http://localhost:8000/v1` for vLLM/Ollama, or `https://generativelanguage.googleapis.com/v1beta/openai` for Gemini).

**`api-key`**
API key sent as a Bearer token in the `Authorization` header. Set to an empty string `""` for local servers that don't require auth. When NULL (default), no `Authorization` header is sent at all.

**`model`**
Model name string. Must be set by the user. Examples: `gpt-4o`, `gemini-2.0-flash`, `llava-v1.6-34b`, `qwen2-vl-7b`.

**`profile`**
Selects the API compatibility profile. Currently only `openai` is built in. Additional profiles can be added in the capability preset system.

### Prompts

**`system-prompt`**
System-level instruction sent before the user message. NULL means no system message is included.

**`user-prompt`**
The main prompt describing what to ask about the captured image(s). Default: `"Describe what you see in this image."`.

### Sampling / Generation

**`temperature`**
Controls randomness in output. Higher values (e.g., 1.5) make output more random; lower values (e.g., 0.2) make it more deterministic. Range: 0.0--2.0.

**`max-output-tokens`**
Maximum number of tokens the model may generate. Default: 800.

**`top-p`**
Nucleus sampling: only tokens with cumulative probability up to this value are considered. Range: 0.0--1.0.

**`stop-sequences`**
Array of strings. When any of these strings appear in the model output, generation stops. Example: `stop-sequences='["END","STOP"]'`.

### Timing / Concurrency

**`analysis-interval`**
Minimum time in seconds between consecutive analysis requests. The element will buffer frames and send at most one request per interval. Range: 0.1--3600.0 seconds.

**`frames-per-request`**
Number of frames to accumulate before sending a single API request. Useful for multi-frame analysis (e.g., detecting changes over time). Range: 1--10.

**`timeout`**
HTTP request timeout in seconds. The underlying libcurl call will abort if no response is received within this window. Range: 1--600.

**`max-inflight`**
Maximum number of concurrent API requests. Higher values increase throughput but may overload the API server. Range: 1--16.

**`queue-policy`**
Controls what happens when the number of in-flight requests reaches `max-inflight`:
- `drop` (default): the oldest pending request is dropped to make room.
- `block`: the current frame is skipped (no new request queued) until a slot opens.

### Output

**`output-mode`**
Bitmask flag controlling how analysis results are delivered. Default: 3 (meta + signal).

| Value | Flag | Effect |
|---|---|---|
| 1 | META | Attach `GstVlmVisionMeta` to downstream buffers |
| 2 | SIGNAL | Emit `description-received` GObject signal |
| 4 | BUS | Post `vlmvision-result` GstBus message |
| 8 | JSON | Include raw JSON in bus message payload |

Example: `output-mode=6` enables both signal and bus output.

### Error Handling

**`error-policy`**
Controls how API errors (network failures, HTTP error codes, parse errors) are surfaced:

- `skip` (default): silently skip errors and continue.
- `bus`: post a `vlmvision-error` GstBus message with the error details.
- `signal`: emit the `analysis-error` GObject signal with `(error_message, http_status)`.

### Template Override

See [Template Mode](template-mode.md) for full details.

**`template-body`**
Path to a JSON file with `{{PLACEHOLDER}}` substitutions for overriding the request body.

**`template-headers`**
Path to a text file with per-line `{{PLACEHOLDER}}` header templates.

**`template-response`**
JSON path expression (e.g., `$.choices[0].message.content`) for extracting description text from nonstandard API responses. Default: `$.choices[0].message.content`.
