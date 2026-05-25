# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build commands

```bash
# From gst-vlm-plugin/
meson setup build --prefix=/usr --buildtype=release
ninja -C build
# Install system-wide (needed for examples to find the plugin)
sudo ninja -C build install
```

Build examples:

```bash
# From examples/
make all    # compiles .c files into bin/
```

Verify the plugin is registered:

```bash
gst-inspect-1.0 vlmvision
```

## Run examples

Set at minimum an API key:

```bash
# OpenAI
export VLM_API_KEY=sk-...
# or Gemini via OpenAI endpoint
export VLM_API_KEY=<google-api-key>
export VLM_BASE_URL=https://generativelanguage.googleapis.com/v1beta/openai
export VLM_MODEL=gemini-2.0-flash
# or local vLLM (no key needed)
export VLM_BASE_URL=http://localhost:8000
export VLM_MODEL=llava-v1.6-34b
```

```bash
# C example (videotestsrc source)
./examples/bin/vlm_vision_example
# C example (uridecodebin source)
./examples/bin/vlm_vision_example2 [video-uri]
# Python example
python3 examples/vlm_vision_example.py
```

## Docker

```bash
docker build -t gst-vlm-vision .
docker run --rm --network host -e VLM_API_KEY=$VLM_API_KEY \
  -v $(pwd)/gst-vlm-plugin:/src:ro \
  -v $(pwd)/examples:/examples \
  gst-vlm-vision test-examples vlm_vision_example.c
```

The container mounts the source at `/src:ro` and copies it to `/builder` for building. `--network host` is used for WSL2 display access.

## Architecture

### Element: `vlmvision`

A `GstBaseTransform` subclass (`transform_ip` — in-place). Sits between a video source and a sink, intercepting frames and periodically sending them to an OpenAI-compatible `/v1/chat/completions` endpoint.

Data flow: `video frame → JPEG encode (jpeg-encoder) → VlmRequest (neutral model) → VlmSerializer (Chat Completions JSON) → http-client (libcurl) → VlmResponseParser → VlmResult → emit signal / attach GstMeta / post GstBus message`

### Source modules

| File | Role |
|---|---|
| `plugin.c` | Registers the `vlmvision` element |
| `gstvlmvision.c` | Element lifecycle, properties, worker thread, transform_ip |
| `gstvlmvision.h` | Element struct, class definition, output/request mode enums |
| `gstvlmvisionmeta.c/h` | `GstVlmVisionMeta` — custom GstMeta for buffer-attached descriptions |
| `jpeg-encoder.c/h` | Raw video frame → JPEG encoding (RGB/BGR/RGBA/BGRA) |
| `http-client.c/h` | libcurl wrapper: synchronous HTTP POST with timeout |
| `vlm-request.h` | `VlmRequest` — provider-neutral multimodal request struct |
| `vlm-result.h` | `VlmResult` — normalized response struct |
| `vlm-serializer.h` | `VlmSerializer` vtable: `build_url`, `build_headers`, `build_body` |
| `vlm-backend.h` | `VlmResponseParser` vtable: `parse()` |
| `serializer-openai-chat.c/h` | Default serializer: builds `/v1/chat/completions` JSON |
| `response-parser-openai-chat.c/h` | Default parser: extracts `choices[0].message.content` |
| `template-engine.c/h` | `{{PLACEHOLDER}}` string substitution engine |
| `serializer-template.c/h` | Template-based request builder (escape hatch for nonstandard APIs) |
| `response-parser-template.c/h` | Template-based response extraction via JSON path expressions |
| `queue-policy.h` | `VlmQueuePolicy` enum (DROP, BLOCK) |

### Async architecture

A dedicated worker thread communicates through two `GAsyncQueue` objects:
- **`request_queue`** — main thread pushes `VlmWorkerRequest`, worker pops it
- **`result_queue`** — worker pushes `VlmWorkerResult`, main thread's idle GSource callback pops it

The worker constructs a `VlmRequest`, passes it through the `VlmSerializer` vtable to get URL/headers/body, calls `http_client_send()`, parses the response through `VlmResponseParser`, and pushes the result.

### Output modes (bitmask property `output-mode`)

| Flag | Value | Effect |
|---|---|---|
| `VLM_OUTPUT_META` | 1 | Attach `GstVlmVisionMeta` to downstream buffers |
| `VLM_OUTPUT_SIGNAL` | 2 | Emit `description-received` GObject signal |
| `VLM_OUTPUT_BUS` | 4 | Post `vlmvision-result` GstBus message |
| `VLM_OUTPUT_JSON` | 8 | Include raw JSON in bus message |

Default: `3` (meta + signal).

### Concurrency

- `max-inflight` property limits concurrent HTTP calls (default: 1)
- `queue-policy`: `drop` (default) drops oldest pending request when full; `block` skips new frames
- `timeout`: HTTP timeout in seconds (default: 30)
- All guarded by `GMutex inflight_mutex`
- `pending_description` protected by `GMutex pending_mutex`
- `analysis_in_progress` uses `g_atomic_int` for lock-free flag access

### Customization layers (in order of increasing complexity)

1. **Property overrides** — set `base-url`, `api-key`, `model` directly
2. **Profiles** — `profile=gemini-openai|vllm-openai|ollama|custom` presets endpoint defaults
3. **Template mode** — set `template-body`, `template-headers`, `template-response` to file paths for full wire-format control

### Dependencies

`glib-2.0`, `gobject-2.0`, `gstreamer-1.0`, `gstreamer-video-1.0`, `gstreamer-base-1.0`, `libcurl`, `json-c`, `libjpeg` (optional, for raw video → JPEG).

### Key properties

| Property | Default | Notes |
|---|---|---|
| `base-url` | `https://api.openai.com` | Configurable for any provider |
| `api-key` | NULL | NULL = no auth header |
| `model` | `""` (required) | Any model name (e.g. `gpt-4o`) |
| `profile` | `openai` | Preset endpoint configuration |
| `user-prompt` | `"Describe..."` | Text prompt |
| `system-prompt` | NULL | System message |
| `analysis-interval` | 5.0 sec | Time between samples |
| `output-mode` | 3 | Bitmask (1=meta, 2=signal) |
| `timeout` | 30 sec | HTTP timeout |
| `max-inflight` | 1 | Max concurrent requests |
| `queue-policy` | `drop` | Backpressure behavior |
| `error-policy` | `skip` | Error handling: skip, bus, or signal |

### Compatibility

The old `geminivision` element has been replaced by `vlmvision`. Migration:
```
geminivision api-key=... model-name=gemini-2.0-flash
→ vlmvision base-url=https://generativelanguage.googleapis.com/v1beta/openai api-key=... model=gemini-2.0-flash profile=gemini-openai
```
