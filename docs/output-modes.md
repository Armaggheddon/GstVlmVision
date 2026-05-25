# Output Modes

The `output-mode` property is a bitmask supporting four delivery channels for analysis results.

## Mode Flags

| Value | Name | Description |
|---|---|---|
| 1 | META | Attach `GstVlmVisionMeta` to downstream buffers |
| 2 | SIGNAL | Emit `description-received` GObject signal |
| 4 | BUS | Post `vlmvision-result` GstBus message |
| 8 | JSON | Include raw JSON in bus message payload |

Default: **3** (META + SIGNAL). Combine flags by adding values (e.g., `output-mode=7` enables all three primary modes).

---

## 1. META (value 1)

Attaches a `GstVlmVisionMeta` to each downstream buffer. The metadata contains the latest description text in a GStreamer metadata block.

**C usage:**
```c
#include "gstvlmvisionmeta.h"

// The meta is attached automatically when output-mode includes META (1).
// Access it from your own element using GstMeta API:
GstMeta *meta = gst_buffer_get_meta(buffer, GST_VLM_VISION_META_API_TYPE);
if (meta) {
    GstVlmVisionMeta *vmeta = (GstVlmVisionMeta *)meta;
    g_print("Description: %s\n", vmeta->description);
}
```

**Python usage:**
```python
# When using Gst.VlmVision, any downstream buffer can be queried
# for the GstVlmVisionMeta via the buffer metadata API
```

---

## 2. SIGNAL (value 2)

Emits the `description-received` GObject signal on the `vlmvision` element.

**Signal signature:**
```c
void (*description_received)(GstVlmVision *self, const gchar *description, GstBuffer *buffer);
```

**Python example:**
```python
def on_description(vlm, description, buffer, user_data):
    print(f"VLM says: {description}")

vlm.connect("description-received", on_description)
```

The `buffer` parameter is the original GstBuffer that triggered the analysis, allowing you to correlate descriptions with specific video frames.

---

## 3. BUS (value 4)

Posts a `vlmvision-result` message on the GStreamer bus.

**Python example:**
```python
def on_bus_message(bus, msg):
    if msg.type == Gst.MessageType.ELEMENT and msg.has_name("vlmvision-result"):
        s = msg.get_structure()
        print(f"Description: {s.get_string('description')}")
        print(f"PTS: {s.get_value('pts')}")
        print(f"Model: {s.get_string('model')}")
        # raw-json is always included in bus messages
        if s.has_field('raw-json'):
            print(f"Raw JSON: {s.get_string('raw-json')}")

bus.connect("message", on_bus_message)
```

**Bus message structure (`vlmvision-result`):**

| Field | Type | Description |
|---|---|---|
| `description` | string | The text description from the VLM |
| `pts` | uint64 | PTS of the triggering buffer (nanoseconds) |
| `model` | string | Model name used for the request |
| `raw-json` | string | Raw JSON response (always included in bus messages) |

**Error bus message (`vlmvision-error`):**

When `error-policy=bus`, errors post a `vlmvision-error` message:

| Field | Type | Description |
|---|---|---|
| `message` | string | Error description |
| `http-status` | int | HTTP status code (0 for network errors) |

**Python example:**
```python
def on_bus_message(bus, msg):
    if msg.type == Gst.MessageType.ELEMENT:
        if msg.has_name("vlmvision-error"):
            s = msg.get_structure()
            print(f"Error: {s.get_string('message')} (HTTP {s.get_int('http-status')})")
```

---

## 4. JSON (value 8)

When BUS mode is enabled, the `vlmvision-result` bus message always includes a `raw-json` field containing the complete API response body. The JSON flag (8) is reserved for future fine-grained control.

---

## Error Signal

**`analysis-error`** — emitted when `error-policy=signal`.

**Signal signature:**
```c
void (*analysis_error)(GstVlmVision *self, const gchar *error_message, gint http_status);
```

```python
def on_error(vlm, error_message, http_status, user_data):
    print(f"VLM error (HTTP {http_status}): {error_message}")

vlm.connect("analysis-error", on_error)
```
