#!/usr/bin/env python3
"""
VLM Vision Plugin -- Python example
Pipeline: videotestsrc ! videoconvert ! vlmvision ! videoconvert ! ximagesink

Demonstrates all major vlmvision plugin capabilities:
  - Environment-variable configuration (API key, base URL, model, profile)
  - Property configuration (profile, prompts, interval, output-mode, timeout, max-inflight)
  - Signal-based result delivery
  - Bus-message-based result delivery
  - Startup configuration summary
  - Graceful error handling and shutdown

Requires: libgstvlmvision.so installed in GStreamer plugin path.
"""

import sys
import os
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib


class VlmVisionApp:
    """Demonstrates the vlmvision GStreamer element with signal and bus output."""

    def __init__(self):
        Gst.init(None)

        # ------------------------------------------------------------------
        # 1. Environment-variable configuration
        # ------------------------------------------------------------------
        api_key = os.environ.get("VLM_API_KEY") or os.environ.get("OPENAI_API_KEY")
        self.base_url = os.environ.get("VLM_BASE_URL")
        self.model = os.environ.get("VLM_MODEL")
        self.profile = os.environ.get("VLM_PROFILE") or "openai"

        if not api_key:
            api_key = ""
        if not self.base_url:
            self.base_url = "https://api.openai.com"
        if not self.model:
            print("WARNING: VLM_MODEL not set. API calls will fail without a valid model name.")

        self.api_key = api_key

        # ------------------------------------------------------------------
        # 2. Prompt & analysis settings
        # ------------------------------------------------------------------
        self.system_prompt = "You are a helpful vision assistant."
        self.user_prompt = "Describe what you see in this image."
        self.analysis_interval = 5.0  # seconds
        self.frames_per_request = 1
        self.temperature = 0.7
        self.max_output_tokens = 200
        self.top_p = 0.9

        # bitmask: 1=meta  2=signal  4=bus  8=raw-json
        # 7 = meta + signal + bus
        self.output_mode = 7
        self.timeout = 30
        self.max_inflight = 2
        self.error_policy = 1   # bus

        # ------------------------------------------------------------------
        # 3. Build pipeline
        # ------------------------------------------------------------------
        self.pipeline = Gst.Pipeline.new("vlm-vision-pipeline")

        source = Gst.ElementFactory.make("videotestsrc", "source")
        conv1 = Gst.ElementFactory.make("videoconvert", "converter1")
        vlm = Gst.ElementFactory.make("vlmvision", "vlm-vision")
        conv2 = Gst.ElementFactory.make("videoconvert", "converter2")
        sink = Gst.ElementFactory.make("ximagesink", "sink")

        for name, elem in [
            ("videotestsrc", source),
            ("videoconvert", conv1),
            ("vlmvision", vlm),
            ("videoconvert", conv2),
            ("ximagesink", sink),
        ]:
            if not elem:
                print(f"ERROR: Could not create element '{name}'.")
                if name == "vlmvision":
                    print(
                        "       Ensure the plugin is installed and "
                        "GST_PLUGIN_PATH is set."
                    )
                sys.exit(1)

        self.source = source
        self.vlm = vlm

        # -- Configure source --
        source.set_property("pattern", 0)  # smpte test pattern

        # -- Configure vlmvision properties --
        vlm.set_property("api-key", self.api_key)
        vlm.set_property("base-url", self.base_url)
        vlm.set_property("model", self.model)
        vlm.set_property("profile", self.profile)
        vlm.set_property("system-prompt", self.system_prompt)
        vlm.set_property("user-prompt", self.user_prompt)
        vlm.set_property("analysis-interval", self.analysis_interval)
        vlm.set_property("frames-per-request", self.frames_per_request)
        vlm.set_property("temperature", self.temperature)
        vlm.set_property("max-output-tokens", self.max_output_tokens)
        vlm.set_property("top-p", self.top_p)
        vlm.set_property("output-mode", self.output_mode)
        vlm.set_property("timeout", self.timeout)
        vlm.set_property("max-inflight", self.max_inflight)
        vlm.set_property("error-policy", self.error_policy)

        # ------------------------------------------------------------------
        # 4. Connect signals
        # ------------------------------------------------------------------
        vlm.connect("description-received", self.on_description_received)
        vlm.connect("analysis-error", self.on_analysis_error)

        # ------------------------------------------------------------------
        # 5. Link elements
        #     videotestsrc ! videoconvert ! vlmvision ! videoconvert ! ximagesink
        # ------------------------------------------------------------------
        for elem in [source, conv1, vlm, conv2, sink]:
            self.pipeline.add(elem)

        if not (
            source.link(conv1)
            and conv1.link(vlm)
            and vlm.link(conv2)
            and conv2.link(sink)
        ):
            print("ERROR: Failed to link pipeline elements.")
            sys.exit(1)

        # ------------------------------------------------------------------
        # 6. Bus (message bus for EOS, errors, and vlmvision-result messages)
        # ------------------------------------------------------------------
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self.on_bus_message)

        self.loop = GLib.MainLoop()

    # ------------------------------------------------------------------
    # Signal callback: description-received
    # ------------------------------------------------------------------
    def on_analysis_error(self, element, message, http_status):
        print(f"\n*** [SIGNAL] ANALYSIS ERROR (HTTP {http_status}): {message} ***\n", flush=True)

    def on_description_received(self, element, description, buffer):
        """Called when vlmvision emits the 'description-received' signal."""
        pts = buffer.pts
        if pts != Gst.CLOCK_TIME_NONE:
            pts_str = f"{pts // Gst.SECOND}.{pts % Gst.SECOND:09d}"
        else:
            pts_str = "?"
        print("=" * 50)
        print(f"  [SIGNAL] Frame PTS : {pts_str}s")
        print(f"  [SIGNAL] Description: {description}")
        print("=" * 50, flush=True)

    # ------------------------------------------------------------------
    # Bus message callback
    # ------------------------------------------------------------------
    def on_bus_message(self, bus, message):
        """Process messages from the GStreamer bus."""
        t = message.type

        if t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"\n[ERROR] from {message.src.get_name()}: {err.message}")
            if debug:
                print(f"  Debug: {debug}")
            self.loop.quit()

        elif t == Gst.MessageType.EOS:
            print("\n[EOS] End of stream reached.")
            self.loop.quit()

        elif t == Gst.MessageType.ELEMENT:
            self._handle_element_message(message)

    def _handle_element_message(self, message):
        """Process element-specific bus messages (e.g. vlmvision-result)."""
        structure = message.get_structure()
        if structure is None:
            return

        name = structure.get_name()
        if name == "vlmvision-error":
            msg = structure.get_string("message") or ""
            http_status = 0
            try:
                ok, http_status = structure.get_int("http-status")
                if not ok:
                    http_status = 0
            except Exception:
                pass
            print(f"*** [BUS] VLM VISION ERROR (HTTP {http_status}): {msg} ***", flush=True)
        elif name == "vlmvision-result":
            # Parse fields: description (string), pts (uint64), model (string),
            #               raw-json (string), error (string)
            description = structure.get_string("description")
            model = structure.get_string("model") or ""
            raw_json = structure.get_string("raw-json") or ""
            error = structure.get_string("error") or ""

            # get_uint64 returns (success: bool, value: int) in GI bindings
            pts_val = None
            try:
                ok, pts_val = structure.get_uint64("pts")
                if not ok:
                    pts_val = None
            except Exception:
                # Fallback for binding variants that return a single value
                pass

            print("--- [BUS] vlmvision-result ---")
            if pts_val is not None:
                pts_str = f"{pts_val // Gst.SECOND}.{pts_val % Gst.SECOND:09d}"
                print(f"  PTS        : {pts_str}s")
            if model:
                print(f"  Model      : {model}")
            print(f"  Description: {description}")
            if error:
                print(f"  Error      : {error}")
            if raw_json:
                print(f"  Raw JSON   : ({len(raw_json)} chars)")
            print("--- End bus message ---\n", flush=True)

    # ------------------------------------------------------------------
    # Startup summary
    # ------------------------------------------------------------------
    def _print_config(self):
        """Print a summary of the active configuration."""
        print("=" * 50)
        print(" VLM Vision Pipeline Configuration")
        print("=" * 50)
        print(f"  Base URL         : {self.base_url}")
        print(f"  Model            : {self.model}")
        print(f"  Profile          : {self.profile}")
        print(f"  System prompt    : {self.system_prompt!r}")
        print(f"  User prompt      : {self.user_prompt!r}")
        print(f"  Analysis interval: {self.analysis_interval} s")
        print(f"  Frames/request   : {self.frames_per_request}")
        print(f"  Temperature      : {self.temperature}")
        print(f"  Max output tokens: {self.max_output_tokens}")
        print(f"  Top-p            : {self.top_p}")
        print(f"  Output mode      : {self.output_mode} "
              f"(meta={'1' if self.output_mode & 1 else '0'} "
              f"signal={'1' if self.output_mode & 2 else '0'} "
              f"bus={'1' if self.output_mode & 4 else '0'})")
        print(f"  Timeout          : {self.timeout} s")
        print(f"  Max inflight     : {self.max_inflight}")
        print("-" * 50)
        print("  Pipeline:")
        print("    videotestsrc ! videoconvert ! vlmvision ! videoconvert")
        print("    ! ximagesink")
        print("=" * 50, flush=True)

    # ------------------------------------------------------------------
    # Run
    # ------------------------------------------------------------------
    def run(self):
        """Start the pipeline and run the GLib main loop."""
        self._print_config()
        print("Press Ctrl+C to stop.\n", flush=True)

        ret = self.pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            print("ERROR: Unable to set pipeline to PLAYING state.")
            sys.exit(1)

        try:
            self.loop.run()
        except KeyboardInterrupt:
            print("\n[Interrupt] Received Ctrl+C, shutting down...")
        finally:
            self.pipeline.set_state(Gst.State.NULL)
            print("[Done] Pipeline stopped.")


if __name__ == "__main__":
    VlmVisionApp().run()
