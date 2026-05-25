<div id="top"></div>

<p align="center">
  <img width="400" src="docs/images/gstvlmvision_logo.png">
</p>
<h1 align="center">
    <a href="https://github.com/Armaggheddon/GstVlmVision">GstVlmVision</a>
</h1>
<p align="center">
    <a href="https://github.com/Armaggheddon/GstVlmVision/commits/master">
    <img src="https://img.shields.io/github/last-commit/Armaggheddon/GstVlmVision">
    </a>
    <a href="https://github.com/Armaggheddon/GstVlmVision">
    <img src="https://img.shields.io/badge/Maintained-yes-green.svg">
    </a>
    <a href="https://github.com/Armaggheddon/GstVlmVision/issues">
    <img src="https://img.shields.io/github/issues/Armaggheddon/GstVlmVision">
    </a>
    <a href="https://github.com/Armaggheddon/GstVlmVision/blob/master/LICENSE">
    <img src="https://img.shields.io/github/license/Armaggheddon/GstVlmVision">
    </a>
</p>
<p align="center">
    OpenAI-compatible vision-language model integration for GStreamer pipelines
    <br/>
    <br/>
    <a href="https://github.com/Armaggheddon/GstVlmVision/issues">Report Bug</a>
    &bull;
    <a href="https://github.com/Armaggheddon/GstVlmVision/issues">Request Feature</a>
</p>

---

## What is GstVlmVision?

GstVlmVision (formerly GstGeminiVision) is a GStreamer plugin that bridges live video/image streams with any OpenAI-compatible vision-language model API. It periodically captures video frames, sends them to a VLM endpoint via `/v1/chat/completions`, and makes the description available through GstMeta, GObject signals, or GstBus messages.

Supported providers: OpenAI, Google Gemini (via OpenAI compatibility), vLLM, Ollama, and any `/v1/chat/completions`-compatible server.

![Video demo](./docs/vlm_vision_demo.mp4)

---

## Features

- **OpenAI Chat Completions Multimodal** -- standard `/v1/chat/completions` with text + image_url content blocks
- **Multiple Output Modes** -- bitmask-selectable: GstMeta, GObject signal, GstBus message ([details](docs/output-modes.md))
- **Template Override Mode** -- `{{PLACEHOLDER}}` templates for nonstandard API providers ([details](docs/template-mode.md))
- **Async HTTP with Concurrency Control** -- threaded dispatch with configurable `max-inflight` and `timeout`
- **JPEG Frame Encoding** -- raw video frames encoded to JPEG before sending
- **Docker Support** -- pre-configured build environment
- **Python Bindings** -- GObject Introspection via `gi.repository.GstVlmVision`

---

## Quick Start

```bash
gst-launch-1.0 -m videotestsrc ! videoconvert ! \
  vlmvision base-url="http://localhost:8765" api-key="dummy" \
    model="gpt-4o" profile="openai" \
    user-prompt="What do you see?" output-mode=4 \
  ! videoconvert ! autovideosink
```

---

## Building from Source

**Prerequisites:** GStreamer 1.16+, meson, ninja, gcc, pkg-config, libglib2.0-dev, libgstreamer-plugins-base1.0-dev, libcurl4-openssl-dev, libjson-c-dev, libjpeg-dev. Optional: libgirepository1.0-dev + gobject-introspection (for Python bindings).

```bash
git clone https://github.com/Armaggheddon/GstVlmVision.git
cd GstVlmVision/gst-vlm-plugin
meson setup build --prefix=/usr --buildtype=release
ninja -C build
sudo ninja -C build install
gst-inspect-1.0 vlmvision
```

For development without system install:
```bash
export GST_PLUGIN_PATH=$(pwd)/build:$GST_PLUGIN_PATH
export GI_TYPELIB_PATH=$(pwd)/build:$GI_TYPELIB_PATH
```

---

## Running Examples

```bash
export VLM_API_KEY=sk-...    # or empty for local servers

# C examples
cd examples && make all
./bin/vlm_vision_example
./bin/vlm_vision_example2 [video-uri]

# Python
python3 vlm_vision_example.py
```

---

## Property Reference

| Property | Type | Default | Description |
|---|---|---|---|
| `base-url` | string | `https://api.openai.com` | Base URL for the API endpoint |
| `api-key` | string | NULL | API key; NULL = no auth header sent |
| `model` | string | `""` (must set) | Model name (e.g. `gpt-4o`, `gemini-2.0-flash`) |
| `system-prompt` | string | NULL | System prompt |
| `user-prompt` | string | `"Describe what you see..."` | User prompt about the image(s) |
| `profile` | string | `openai` | API compatibility profile |
| `analysis-interval` | double | 5.0 | Seconds between analyses (0.1--3600) |
| `frames-per-request` | int | 1 | Images per request (1--10) |
| `request-mode` | enum | `chat-completions` | API request type |
| `stop-sequences` | GStrv | NULL | Stop generation strings |
| `temperature` | double | 1.0 | Sampling temperature (0.0--2.0) |
| `max-output-tokens` | int | 800 | Max tokens to generate |
| `top-p` | double | 0.8 | Nucleus sampling (0.0--1.0) |
| `output-mode` | flags | 3 | Bitmask: 1=meta, 2=signal, 4=bus, 8=json |
| `timeout` | int | 30 | HTTP timeout in seconds (1--600) |
| `max-inflight` | int | 1 | Max concurrent requests (1--16) |
| `queue-policy` | enum | `drop` | Backpressure: drop or block |
| `error-policy` | enum | `skip` | Error handling: skip, bus, or signal |
| `template-body` | string | NULL | File path for body template override |
| `template-headers` | string | NULL | File path for headers template override |
| `template-response` | string | NULL | JSON path for response extraction |

Full details with usage examples: [docs/properties.md](docs/properties.md)

---

## Output Modes

`output-mode` is a bitmask combining:

| Value | Flag | Effect |
|---|---|---|
| 1 | META | Attach `GstVlmVisionMeta` to downstream buffers |
| 2 | SIGNAL | Emit `description-received` GObject signal |
| 4 | BUS | Post `vlmvision-result` GstBus message |
| 8 | JSON | Include raw JSON in bus message |

Default: 3 (meta + signal). See [docs/output-modes.md](docs/output-modes.md) for signal signatures, bus message structures, and code examples.

---

## Error Handling

| Policy | Value | Behaviour |
|---|---|---|
| `skip` | default | Silently skip API errors |
| `bus` | -- | Post `vlmvision-error` GstBus message with `{ message, http-status }` |
| `signal` | -- | Emit `analysis-error(error_message, http_status)` GObject signal |

---

## Template Mode

For APIs that don't follow the standard OpenAI format, use template files to override request serialization and response parsing.

| Property | Purpose |
|---|---|
| `template-body` | File path -- JSON body with `{{PLACEHOLDER}}` substitutions |
| `template-headers` | File path -- per-line header templates |
| `template-response` | JSON path (e.g. `$.choices[0].message.content`) |

Supported placeholders: `{{BASE_URL}}`, `{{MODEL}}`, `{{SYSTEM_PROMPT}}`, `{{USER_PROMPT}}`, `{{IMAGE_0_BASE64}}`, `{{IMAGE_0_DATA_URL}}`, `{{IMAGE_ARRAY_JSON}}`, `{{TEMPERATURE}}`, `{{MAX_TOKENS}}`, `{{TOP_P}}`, `{{SCHEMA_JSON}}`, `{{REQUEST_ID}}`, `{{STOP_SEQUENCES_JSON}}`.

Full guide with examples: [docs/template-mode.md](docs/template-mode.md)

---

## Docker

```bash
docker build -t gst-vlm-vision .

# Build plugin
docker run --rm -v $(pwd)/gst-vlm-plugin:/builder gst-vlm-vision build

# Build examples
docker run --rm -v $(pwd)/gst-vlm-plugin:/builder -v $(pwd)/examples:/examples \
  gst-vlm-vision build-examples

# Run Python example (needs X11 for video window)
docker run --rm -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e VLM_API_KEY=$VLM_API_KEY \
  -v $(pwd)/gst-vlm-plugin:/builder -v $(pwd)/examples:/examples \
  gst-vlm-vision test-examples vlm_vision_example.py

# Interactive shell
docker run -it --rm -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v $(pwd)/gst-vlm-plugin:/builder -v $(pwd)/examples:/examples \
  gst-vlm-vision shell
```

---

## Migration from GstGeminiVision

| Old | New |
|---|---|
| Element: `geminivision` | Element: `vlmvision` |
| `model-name` | `model` |
| `prompt` | `user-prompt` |
| `output-metadata` (bool) | `output-mode` (bitmask) |
| `top-k` | Removed (not in OpenAI API) |
| URL hardcoded to Google | Configurable `base-url` |

**Before:**
```bash
geminivision api-key="YOUR_KEY" prompt="Describe this" model-name="gemini-2.0-flash"
```

**After:**
```bash
vlmvision base-url=https://generativelanguage.googleapis.com/v1beta/openai \
  api-key="YOUR_KEY" user-prompt="Describe this" model="gemini-2.0-flash" \
  profile=gemini-openai
```

---

## Architecture

```
video frame -> jpeg-encoder -> VlmRequest -> serializer -> http-client (libcurl POST)
VlmResult <- response-parser <- HTTP response
    |
    +-> GstVlmVisionMeta (buffer metadata)
    +-> description-received (GObject signal)
    +-> vlmvision-result (GstBus message)
```

Key source modules in `gst-vlm-plugin/src/`: `gstvlmvision.c` (element), `gstvlmvisionmeta.c` (metadata), `serializer-openai-chat.c`, `serializer-template.c`, `response-parser-openai-chat.c`, `response-parser-template.c`, `template-engine.c`, `http-client.c`, `jpeg-encoder.c`, `queue-policy.c`.

---

## Contributing

Contributions welcome -- bug fixes, features, documentation. Open an issue or submit a PR.

## License

MIT License -- see the LICENSE file.
