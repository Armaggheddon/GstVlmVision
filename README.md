<div id="top"></div>
<br/>
<br/>
<br/>

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
    Transform your media pipelines with AI-powered insights! 🎥🤖
    <br/>
    <br/>
    <a href="https://github.com/Armaggheddon/GstVlmVision/issues">Report Bug</a>
    &bull;
    <a href="https://github.com/Armaggheddon/GstVlmVision/issues">Request Feature</a>
</p>

---

Ever wondered what a GStreamer pipeline would say if it could talk? Now it can (almost)! With **GstVlmVision** (formerly GstGeminiVision), you can inject the power of any OpenAI-compatible vision-language model directly into your GStreamer media pipelines. Turn your video streams into insightful descriptions, automate content analysis, or just have some fun making your videos self-aware! 🤖🎬

https://github.com/user-attachments/assets/91c23d18-2409-410c-a8c1-2cc841419003

---

## 🤨 What is GstVlmVision?

GstVlmVision is a GStreamer plugin that acts as a bridge between your live video or image streams and any OpenAI-compatible vision-language model API. It periodically captures frames, sends them to a VLM endpoint via `/v1/chat/completions`, and then makes the generated description available through GstMeta, GObject signals, or GstBus messages.

Supported providers include OpenAI, Google Gemini (via OpenAI compatibility), vLLM, Ollama, and any `/v1/chat/completions`-compatible server.

Imagine:
*   Generating visual descriptions for accessibility purposes.
*   Creating a security camera that describes what it sees.
*   Building interactive art installations that react to visual input.
*   Automating video content moderation and tagging.
*   ...the possibilities are as vast as your imagination (and the VLM's capabilities)!

---

## 🌟 Features

- **OpenAI Chat Completions Multimodal** -- standard `/v1/chat/completions` with text + image_url content blocks
- **Multiple Output Modes** -- bitmask-selectable: GstMeta, GObject signal, GstBus message ([details](docs/output-modes.md))
- **Template Override Mode** -- `{{PLACEHOLDER}}` templates for nonstandard API providers ([details](docs/template-mode.md))
- **Async HTTP with Concurrency Control** -- threaded dispatch with configurable `max-inflight` and `timeout`
- **JPEG Frame Encoding** -- raw video frames encoded to JPEG before sending
- **Docker Support** -- pre-configured build environment ([details](#-docker))
- **Python Bindings** -- GObject Introspection via `gi.repository.GstVlmVision`

---

## 🎬 See it in Action!

*   Show a `videotestsrc` pipeline running with the plugin.
*   Display the console output from `vlm_vision_example.py` or `vlm_vision_example.c` showing the descriptions.
*   *Bonus:* If you have a more complex demo (e.g., overlaying text on video), showcase that!

![GstVlmVision in Action](docs/images/examplec_screen.png)
![GstVlmVision in Action](docs/images/examplepy_screen.png)

```bash
gst-launch-1.0 -m videotestsrc ! videoconvert ! \
  vlmvision base-url="https://api.openai.com" api-key="sk-..." \
    model="gpt-4o" profile="openai" \
    user-prompt="What do you see?" output-mode=4 \
  ! videoconvert ! autovideosink
```
```console
Setting pipeline to PLAYING state...
Pipeline running...
Press Ctrl+C to quit
Pipeline state changed from NULL to READY
Pipeline state changed from READY to PAUSED
Pipeline state changed from PAUSED to PLAYING

=================================
Frame time: 0.000000000 (PTS: 0)
Description: That's a color bars test pattern, used to adjust color settings on television screens.

=================================

^CInterrupt received, stopping...
Cleaning up...
Pipeline stopped.
```

---

## 🚀 Quick Start

```bash
gst-launch-1.0 -m videotestsrc ! videoconvert ! \
  vlmvision base-url="http://localhost:8765" api-key="dummy" \
    model="gpt-4o" profile="openai" \
    user-prompt="What do you see?" output-mode=4 \
  ! videoconvert ! autovideosink
```

---

## 🛠️ Building from Source

Ready to give your GStreamer pipelines a voice? Let's go!

### Prerequisites

- **GStreamer:** Core GStreamer libraries and development files (version 1.16+ recommended).
- **Build Tools:** `meson`, `ninja`, `gcc` (or your C compiler), `pkg-config`.
- **Dependencies for the Plugin:**
    - `libglib2.0-dev`
    - `libgstreamer-plugins-base1.0-dev`
    - `libcurl4-openssl-dev` (or your system's cURL dev package)
    - `libjson-c-dev`
    - `libjpeg-dev`
    - `libgirepository1.0-dev` & `gobject-introspection` (for GObject Introspection, used by Python bindings)
- **Python 3** (for the Python example):
    - `python3-gi`
    - `python3-gst-1.0`

### Building the Plugin

1.  **Clone the repository (if you haven't already):**
    ```bash
    git clone https://github.com/Armaggheddon/GstVlmVision.git
    cd GstVlmVision
    ```

2.  **Navigate to the plugin directory:**
    ```bash
    cd gst-vlm-plugin
    ```

3.  **Configure and build with Meson & Ninja:**
    First, set up the build directory using Meson. The `--prefix=/usr` ensures that a subsequent install places files in standard system locations.
    ```bash
    meson setup build --prefix=/usr --buildtype=release
    ```
    Then, compile the plugin:
    ```bash
    ninja -C build
    ```
    Your compiled plugin shared object (e.g., `libgstvlmvision.so`) will be located in the `gst-vlm-plugin/build/src/` directory.

4.  **Install the Plugin (Optional, but Recommended for System-Wide Access):**
    To make the plugin and its development files available system-wide, run the install command (this usually requires root privileges):
    ```bash
    sudo ninja -C build install
    ```
    This command will copy the necessary files to standard system locations. Based on a typical installation with `--prefix=/usr`, the files will be placed as follows:
    *   The plugin library: `libgstvlmvision.so` to `/usr/lib/x86_64-linux-gnu/gstreamer-1.0/`
    *   GObject Introspection data:
        *   `GstVlmVision-1.0.gir` to `/usr/share/gir-1.0/`
        *   `GstVlmVision-1.0.typelib` to `/usr/lib/x86_64-linux-gnu/girepository-1.0/`
    *   Pkg-config file: `gstvlmvision.pc` to `/usr/lib/x86_64-linux-gnu/pkgconfig/`

    *(Note: The exact paths like `x86_64-linux-gnu` might vary slightly based on your Linux distribution's multiarch setup.)*

    After installation, GStreamer should be able to automatically discover the plugin. Verify with:
    ```bash
    gst-inspect-1.0 vlmvision
    ```

For development without system install, you can set environment variables instead:
```bash
export GST_PLUGIN_PATH=$(pwd)/build:$GST_PLUGIN_PATH
export GI_TYPELIB_PATH=$(pwd)/build:$GI_TYPELIB_PATH
```

---

## ▶️ Running Examples

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

### C Example
Navigate to the examples directory and compile/run:
```bash
cd examples && make all
./bin/vlm_vision_example
./bin/vlm_vision_example2 [video-uri]
```

### Python Example
```bash
cd examples
python3 vlm_vision_example.py
```

You should see descriptions from the VLM printed to the console! 🚀

---

## ⚙️ Property Reference

For a **full, detailed list** of all properties, their types, default values, ranges, and descriptions, please refer to the output of `gst-inspect-1.0`:

➡️ **[View Full Plugin Details (gst-inspect-1.0 output)](docs/gst_inspect_vlmvision.txt)** ⬅️

You can also generate this information yourself by running:
```bash
gst-inspect-1.0 vlmvision
```

Here's a summary of the key properties:

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

## 📤 Output Modes

`output-mode` is a bitmask combining:

| Value | Flag | Effect |
|---|---|---|
| 1 | META | Attach `GstVlmVisionMeta` to downstream buffers |
| 2 | SIGNAL | Emit `description-received` GObject signal |
| 4 | BUS | Post `vlmvision-result` GstBus message |
| 8 | JSON | Include raw JSON in bus message |

Default: 3 (meta + signal). See [docs/output-modes.md](docs/output-modes.md) for signal signatures, bus message structures, and code examples.

---

## ⚠️ Error Handling

| Policy | Value | Behaviour |
|---|---|---|
| `skip` | default | Silently skip API errors |
| `bus` | -- | Post `vlmvision-error` GstBus message with `{ message, http-status }` |
| `signal` | -- | Emit `analysis-error(error_message, http_status)` GObject signal |

---

## 🧩 Template Mode

For APIs that don't follow the standard OpenAI format, use template files to override request serialization and response parsing.

| Property | Purpose |
|---|---|
| `template-body` | File path -- JSON body with `{{PLACEHOLDER}}` substitutions |
| `template-headers` | File path -- per-line header templates |
| `template-response` | JSON path (e.g. `$.choices[0].message.content`) |

Supported placeholders: `{{BASE_URL}}`, `{{MODEL}}`, `{{SYSTEM_PROMPT}}`, `{{USER_PROMPT}}`, `{{IMAGE_0_BASE64}}`, `{{IMAGE_0_DATA_URL}}`, `{{IMAGE_ARRAY_JSON}}`, `{{TEMPERATURE}}`, `{{MAX_TOKENS}}`, `{{TOP_P}}`, `{{SCHEMA_JSON}}`, `{{REQUEST_ID}}`, `{{STOP_SEQUENCES_JSON}}`.

Full guide with examples: [docs/template-mode.md](docs/template-mode.md)

---

## 🐳 Docker: Your AI-Powered Media Lab in a Box!

Want to dive straight into the action without wrestling with dependencies? Our Docker setup is your golden ticket! 🎟️ It's like having a pre-configured media lab, ready to build, test, and run GstVlmVision with just a few commands. No more "it works on my machine" – it'll work in *this* machine!

**Step 1: Build the All-Powerful Docker Image**

First, conjure up your Docker image. This image contains all the tools and magic needed. From your `GstVlmVision` project root:
```bash
docker build -t gst-vlm-vision .
```
*(Psst! If you've already built it, you can skip this step unless you've made changes to the Dockerfile or the plugin build process itself.)*

**Step 2: Unleash the Entrypoint Script!**

The Docker image comes with a super-handy `entrypoint.sh` script that acts as your mission control. You tell it what to do, and it handles the nitty-gritty. Here are your commands, Captain:

- **`build` (Default Action): Compile the Mighty Plugin!**
    Just want to build the main `vlmvision` plugin? This is your command. It compiles the plugin but doesn't install it system-wide in the container. Perfect for a quick compilation check.
    ```bash
    # Run from your GstVlmVision project root
    docker run --rm \
        --volume $(pwd)/gst-vlm-plugin:/builder \
        gst-vlm-vision build
    ```

- **`build-examples`: Build the Plugin & The Examples!**
    This action first ensures the main `vlmvision` plugin is built and installed *inside the container*. Then, it gallops over to your examples directory (`/examples` in the container) and builds them (either using the Makefile or compiling C files directly).
    ```bash
    # Run from your GstVlmVision project root
    docker run --rm \
        --volume $(pwd)/gst-vlm-plugin:/builder \
        --volume $(pwd)/examples:/examples \
        gst-vlm-vision build-examples
    ```

- **`test-examples <example_script_name>`: The Grand Showcase!**
    This is where the real fun begins! This action:
    1.  If you specify a C example (e.g., `vlm_vision_example.c`), it compiles it on the fly if not already built.
    2.  Runs your chosen example script (C or Python)!
    ✨ **Requires `VLM_API_KEY`!** ✨
    ```bash
    # Example for the C script (vlm_vision_example.c):
    # Run from your GstVlmVision project root
    docker run --rm \
        -e VLM_API_KEY="YOUR_ACTUAL_API_KEY" \
        -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
        --volume $(pwd)/gst-vlm-plugin:/builder \
        --volume $(pwd)/examples:/examples \
        gst-vlm-vision test-examples vlm_vision_example.c

    # Example for the Python script (vlm_vision_example.py):
    # Run from your GstVlmVision project root
    docker run --rm \
        -e VLM_API_KEY="YOUR_ACTUAL_API_KEY" \
        -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
        --volume $(pwd)/gst-vlm-plugin:/builder \
        --volume $(pwd)/examples:/examples \
        gst-vlm-vision test-examples vlm_vision_example.py
    ```
    Don't forget to replace `"YOUR_ACTUAL_API_KEY"`! The X11 forwarding lines are for examples that pop up a video window.

- **`shell`: Your Personal Command Deck!**
    Want to poke around inside the container? Need to run some custom commands or debug something? The `shell` action drops you right into an interactive command line.
    ```bash
    # Run from your GstVlmVision project root
    docker run -it --rm \
        -e VLM_API_KEY="YOUR_ACTUAL_API_KEY" `# Optional, but good to have if you plan to test` \
        -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
        --volume $(pwd)/gst-vlm-plugin:/builder \
        --volume $(pwd)/examples:/examples \
        gst-vlm-vision shell
    ```
    Inside the shell, your plugin source will be at `/builder` and examples at `/examples`. The main plugin won't be installed by default with this action alone, but the `entrypoint.sh` script itself is available at `/entrypoint.sh` if you want to manually trigger parts of its logic, or use this shell after running `test-examples` to inspect a fully set-up environment.

> [!NOTE]
> Using `-e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix` allows GUI elements to display on your host machine. This requires running the command `xhost +` on your host Linux machine to allow the Docker container to access your display. If you're using a different display server or setup, you might need to adjust these flags accordingly.

**Important Notes for Docker Adventures:**
- **Volume Mounts are Key:** The `--volume $(pwd)/...:/...` flags map directories from your computer into the Docker container.
    - `/builder`: Points to your `gst-vlm-plugin` directory. This is where the main plugin source code lives.
    - `/examples`: Points to your `examples` directory.
- **API Key:** For `test-examples`, the `VLM_API_KEY` environment variable (`-e`) is crucial. The plugin won't talk to the VLM without it!
- **GUI Display:** If your examples use `autovideosink` or any other element that creates a window, you'll need the `-e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix` lines (and sometimes `xhost +local:docker` on your host Linux machine) to see the output.

With these commands, you're all set to explore the wonders of GstVlmVision without breaking a sweat over setup!

---

## 🔄 Migration from GstGeminiVision

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

## 🏗️ Architecture

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

## 🙌 Contributing

Contributions welcome -- bug fixes, features, documentation. Open an issue or submit a PR.

## 📜 License

MIT License -- see the LICENSE file.

---

Happy Hacking and may your pipelines be ever insightful! 💡
