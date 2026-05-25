#!/usr/bin/env python3
"""
Multi-frame VLM Vision example.

Demonstrates frames-per-request: accumulates N video frames (each spaced
by analysis-interval seconds) and sends them as a single Chat Completions
request containing multiple image_url blocks.

Usage:
    # Against the mock server (no API key needed):
    python3 multi_frame_example.py

    # With environment overrides:
    VLM_API_KEY=sk-... VLM_BASE_URL=https://api.openai.com \
        VLM_MODEL=gpt-4o python3 multi_frame_example.py
"""

import os
import sys
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib


class MultiFrameApp:
    def __init__(self):
        Gst.init(None)

        # ── config ─────────────────────────────────────────────────
        api_key = os.environ.get("VLM_API_KEY") or os.environ.get("OPENAI_API_KEY")
        self.base_url = os.environ.get("VLM_BASE_URL")
        self.model = os.environ.get("VLM_MODEL")
        self.profile = os.environ.get("VLM_PROFILE") or "openai"

        if not api_key:
            api_key = ""
        if not self.base_url:
            self.base_url = "https://api.openai.com"
        if not self.model:
            print("WARNING: VLM_MODEL not set.")

        self.api_key = api_key

        # Multi-frame settings
        self.frames_per_request = 3   # send 3 frames at once
        self.analysis_interval = 0.5  # capture every 0.5 s → batch every 1.5 s
        self.user_prompt = (
            "You are receiving {} consecutive frames from a video. "
            "Describe what you see and note any changes between frames."
        ).format(self.frames_per_request)

        self.output_mode = 6          # signal + bus
        self.timeout = 15

        # ── pipeline ───────────────────────────────────────────────
        self.pipeline = Gst.Pipeline.new("multi-frame-pipeline")

        src = Gst.ElementFactory.make("videotestsrc", "src")
        conv1 = Gst.ElementFactory.make("videoconvert", "conv1")
        vlm = Gst.ElementFactory.make("vlmvision", "vlm")
        conv2 = Gst.ElementFactory.make("videoconvert", "conv2")
        sink = Gst.ElementFactory.make("ximagesink", "sink")

        if not all([src, conv1, vlm, conv2, sink]):
            print("ERROR: Could not create all elements.", file=sys.stderr)
            if not vlm:
                print("  'vlmvision' not found. Is the plugin installed?",
                      file=sys.stderr)
            sys.exit(1)

        src.set_property("pattern", 18)  # SMPTE color bars

        vlm.set_property("api-key", self.api_key)
        vlm.set_property("base-url", self.base_url)
        vlm.set_property("model", self.model)
        vlm.set_property("profile", self.profile)
        vlm.set_property("user-prompt", self.user_prompt)
        vlm.set_property("analysis-interval", self.analysis_interval)
        vlm.set_property("frames-per-request", self.frames_per_request)
        vlm.set_property("output-mode", self.output_mode)
        vlm.set_property("timeout", self.timeout)
        vlm.set_property("error-policy", 1)  # bus

        vlm.connect("description-received", self._on_signal)
        vlm.connect("analysis-error", self._on_error)

        for e in [src, conv1, vlm, conv2, sink]:
            self.pipeline.add(e)
        src.link(conv1)
        conv1.link(vlm)
        vlm.link(conv2)
        conv2.link(sink)

        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self._on_bus)

        self.loop = GLib.MainLoop()
        self.batch_count = 0

    # ── callbacks ──────────────────────────────────────────────────

    def _on_error(self, element, message, http_status):
        print(f"\n*** [SIGNAL] ANALYSIS ERROR (HTTP {http_status}): {message} ***\n", flush=True)

    def _on_signal(self, element, description, buffer):
        pts = buffer.pts
        pts_s = f"{pts // Gst.SECOND}.{pts % Gst.SECOND:09d}"
        print(f"\n[SIGNAL] batch={self.batch_count}  PTS={pts_s}s")
        print(f"  {description}")

    def _on_bus(self, bus, message):
        t = message.type
        if t == Gst.MessageType.ERROR:
            err, dbg = message.parse_error()
            print(f"\nERROR: {err.message}", file=sys.stderr)
            if dbg:
                print(f"  {dbg}", file=sys.stderr)
            self.loop.quit()
        elif t == Gst.MessageType.EOS:
            print("\nEOS — done.")
            self.loop.quit()
        elif t == Gst.MessageType.ELEMENT:
            s = message.get_structure()
            if s and s.get_name() == "vlmvision-error":
                msg = s.get_string("message") or ""
                hs = 0
                try:
                    ok, hs = s.get_int("http-status")
                    if not ok: hs = 0
                except Exception: pass
                print(f"*** [BUS] VLM VISION ERROR (HTTP {hs}): {msg} ***", flush=True)
            elif s and s.get_name() == "vlmvision-result":
                self.batch_count += 1
                desc = s.get_string("description") or "(none)"
                pts = s.get_uint64("pts")[1]
                pts_s = f"{pts // Gst.SECOND}.{pts % Gst.SECOND:09d}"
                print(f"[BUS]   batch={self.batch_count}  PTS={pts_s}s")
                print(f"  {desc}")

    def run(self):
        print("Multi-Frame VLM Vision Example")
        print(f"  base-url:          {self.base_url}")
        print(f"  model:             {self.model}")
        print(f"  profile:           {self.profile}")
        print(f"  frames-per-request: {self.frames_per_request}")
        print(f"  analysis-interval:  {self.analysis_interval}s")
        print(f"  (batch every {self.analysis_interval * self.frames_per_request}s)")
        print(f"  user-prompt:       \"{self.user_prompt}\"")
        print()

        self.pipeline.set_state(Gst.State.PLAYING)
        try:
            self.loop.run()
        except KeyboardInterrupt:
            print("\nInterrupted.")
        self.pipeline.set_state(Gst.State.NULL)


if __name__ == "__main__":
    MultiFrameApp().run()
