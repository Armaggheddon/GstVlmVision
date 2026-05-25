#!/usr/bin/env python3
"""
OpenAI Chat Completions API Emulator

FastAPI server that emulates /v1/chat/completions multimodal endpoint.
Useful for testing GStreamer VLM Vision plugin without a real API key.

Usage:
    source .venv/bin/activate
    python tests/mock_server.py [--port 8765] [--host 0.0.0.0]
"""

import argparse
import base64
import json
import logging
import os
import re
import sys
import time
from datetime import datetime
from typing import Any, Dict, List, Optional

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response, StreamingResponse
from pydantic import BaseModel, Field

# ── Logging ──────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("mock-openai")

# ── Pydantic models matching OpenAI spec ────────────────────────────

class ImageUrl(BaseModel):
    url: str
    detail: Optional[str] = "auto"

class ContentPart(BaseModel):
    type: str
    text: Optional[str] = None
    image_url: Optional[ImageUrl] = None

class Message(BaseModel):
    role: str
    content: Any  # string or list[ContentPart]

class ChatCompletionRequest(BaseModel):
    model: str = "gpt-4o"
    messages: List[Message]
    temperature: Optional[float] = 1.0
    max_tokens: Optional[int] = 800
    top_p: Optional[float] = 1.0
    stop: Optional[List[str]] = None
    response_format: Optional[Dict[str, Any]] = None

# ── Stats ────────────────────────────────────────────────────────────

class ServerStats:
    def __init__(self):
        self.total_requests = 0
        self.total_images = 0
        self.total_bytes = 0
        self.start_time = time.time()
        self.last_request: Optional[Dict[str, Any]] = None

    def record(self, images: int, size: int):
        self.total_requests += 1
        self.total_images += images
        self.total_bytes += size

    def summary(self):
        uptime = time.time() - self.start_time
        return {
            "uptime_sec": round(uptime, 1),
            "total_requests": self.total_requests,
            "total_images": self.total_images,
            "total_body_bytes": self.total_bytes,
            "last_request": self.last_request,
        }

stats = ServerStats()

# ── FastAPI app ──────────────────────────────────────────────────────

app = FastAPI(
    title="OpenAI API Emulator",
    version="1.0.0",
    description="Emulates /v1/chat/completions for local GStreamer VLM Vision testing",
)

# Deterministic description responses for common test patterns
TOY_DESCRIPTIONS = [
    "A colorful SMPTE test pattern with vertical bars of white, yellow, cyan, green, magenta, red, and blue.",
    "A standard color bar test signal used for video calibration, showing seven vertical stripes.",
    "Video test pattern displaying the full SMPTE color bar sequence with a pluge pattern at the bottom.",
    "Broadcast test chart with primary and secondary color bars against a dark background.",
    "Engineering test signal: SMPTE RP 219 color bars with brightness and chroma reference levels.",
]

resp_index = 0


def extract_images_from_request(req: ChatCompletionRequest) -> List[str]:
    """Extract base64 image data from a multimodal chat request."""
    images = []
    for msg in req.messages:
        content = msg.content
        if isinstance(content, str):
            continue
        if isinstance(content, list):
            for part in content:
                if isinstance(part, dict):
                    if part.get("type") == "image_url":
                        url = part.get("image_url", {}).get("url", "")
                        images.append(url)
                elif hasattr(part, "image_url") and part.image_url:
                    images.append(part.image_url.url)
    return images


def decode_image_info(data_url: str) -> Dict[str, Any]:
    """Parse a data:image/... URL and return info about the image."""
    info = {"valid": False, "mime": None, "size_bytes": 0, "width": None, "height": None}

    if not data_url.startswith("data:"):
        return info

    match = re.match(r"data:(image/\w+);base64,(.+)", data_url, re.DOTALL)
    if not match:
        return info

    info["mime"] = match.group(1)

    try:
        raw = base64.b64decode(match.group(2))
        info["size_bytes"] = len(raw)

        # Try to detect JPEG dimensions from header
        if info["mime"] == "image/jpeg" and len(raw) > 100:
            # Simple JPEG SOF0 marker scan for dimensions
            i = 2
            while i < len(raw) - 8:
                if raw[i] == 0xFF:
                    marker = raw[i + 1]
                    if 0xC0 <= marker <= 0xC2:  # SOF0, SOF1, SOF2
                        info["height"] = (raw[i + 5] << 8) | raw[i + 6]
                        info["width"] = (raw[i + 7] << 8) | raw[i + 8]
                        break
                    length = (raw[i + 2] << 8) | raw[i + 3]
                    i += 2 + length
                else:
                    i += 1

        info["valid"] = True
    except Exception as e:
        info["valid"] = False
        info["error"] = str(e)

    return info


# ── Routes ───────────────────────────────────────────────────────────

@app.get("/health")
async def health():
    return {"status": "ok", "service": "OpenAI API Emulator"}


@app.get("/stats")
async def get_stats():
    return stats.summary()


@app.get("/v1/models")
async def list_models():
    return {
        "object": "list",
        "data": [
            {"id": "toy-emulator", "object": "model", "created": 1700000000, "owned_by": "mock"},
            {"id": "gpt-4o", "object": "model", "created": 1700000000, "owned_by": "mock"},
            {"id": "gemini-2.0-flash", "object": "model", "created": 1700000000, "owned_by": "mock"},
        ],
    }


@app.post("/v1/chat/completions")
async def chat_completions(request: Request):
    global resp_index

    # Parse query parameters for behaviour toggles
    params = dict(request.query_params)
    delay = float(params.get("slow", 0))
    should_error = "error" in params
    empty_content = "empty" in params
    malformed = "malformed" in params
    model_name = params.get("model", None)

    # Read and parse body
    body_bytes = await request.body()
    stats.record(images=0, size=len(body_bytes))

    try:
        body = json.loads(body_bytes)
    except json.JSONDecodeError:
        return JSONResponse(
            status_code=400,
            content={"error": {"message": "Invalid JSON in request body", "type": "invalid_request_error"}},
        )

    # Store last request info (strip base64 to keep logs readable)
    brief = json.loads(json.dumps(body))
    stats.last_request = {
        "time": datetime.now().isoformat(),
        "model": brief.get("model", "?"),
        "num_messages": len(brief.get("messages", [])),
        "body_size": len(body_bytes),
    }

    # Log incoming
    msgs = brief.get("messages", [])
    log.info("Request #%d  model=%s  messages=%d  size=%d bytes  params=%s",
             stats.total_requests + 1,
             brief.get("model", "?"), len(msgs), len(body_bytes),
             dict(params) if params else "-")

    # Parse with Pydantic
    try:
        req = ChatCompletionRequest(**body)
    except Exception as e:
        log.warning("Validation error: %s", e)
        return JSONResponse(
            status_code=400,
            content={"error": {"message": f"Invalid request: {e}", "type": "invalid_request_error"}},
        )

    # Extract and validate images
    images = extract_images_from_request(req)
    image_infos = []
    for img_url in images:
        info = decode_image_info(img_url)
        image_infos.append(info)
        if info["valid"]:
            log.info("  image: %s  %dx%d  %d bytes",
                     info["mime"], info.get("width"), info.get("height"), info["size_bytes"])
        else:
            log.warning("  image: INVALID — %s", info.get("error", "unknown"))

    stats.record(images=len(image_infos), size=0)  # size already recorded

    # Behaviours
    if delay > 0:
        log.info("  sleeping %.1fs (slow mode)", delay)
        time.sleep(delay)

    if malformed:
        log.info("  returning malformed response")
        return Response(content=b"not json {{{", media_type="application/json", status_code=200)

    if should_error:
        log.info("  returning error response")
        return JSONResponse(
            status_code=429,
            content={
                "error": {
                    "message": "Rate limit exceeded. You have 0 tokens remaining.",
                    "type": "rate_limit_error",
                    "code": "rate_limit_exceeded",
                }
            },
        )

    # Build response
    description = TOY_DESCRIPTIONS[resp_index % len(TOY_DESCRIPTIONS)]
    resp_index += 1

    if empty_content:
        content = None
        log.info("  returning empty content")
    else:
        content = description

    response = {
        "id": f"chatcmpl-mock-{stats.total_requests:06d}",
        "object": "chat.completion",
        "created": int(time.time()),
        "model": model_name or req.model,
        "choices": [
            {
                "index": 0,
                "message": {
                    "role": "assistant",
                    "content": content,
                },
                "finish_reason": "stop",
            }
        ],
        "usage": {
            "prompt_tokens": len(json.dumps(body)) // 4 + 50,
            "completion_tokens": len(description) // 4 + 1,
            "total_tokens": len(json.dumps(body)) // 4 + len(description) // 4 + 51,
        },
    }

    log.info("  response: %s", description[:80])
    return JSONResponse(content=response)


# ── CLI entry point ───────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="OpenAI API Emulator for VLM Vision testing")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address")
    parser.add_argument("--port", type=int, default=8765, help="Bind port")
    args = parser.parse_args()

    log.info("Starting OpenAI API Emulator on http://%s:%d", args.host, args.port)
    log.info("Endpoints:")
    log.info("  GET  /health                   — health check")
    log.info("  GET  /stats                    — request statistics")
    log.info("  GET  /v1/models                — model list")
    log.info("  POST /v1/chat/completions       — normal response")
    log.info("  POST /v1/chat/completions?error=true  — rate limit error")
    log.info("  POST /v1/chat/completions?slow=5      — 5s delay")
    log.info("  POST /v1/chat/completions?empty=true  — null content")
    log.info("  POST /v1/chat/completions?malformed=1 — non-JSON response")
    log.info("")

    import uvicorn
    uvicorn.run(app, host=args.host, port=args.port, log_level="warning")


if __name__ == "__main__":
    main()
