# Template Mode

For nonstandard APIs that don't follow the OpenAI `/v1/chat/completions` format, template files override the built-in request serialization and response parsing.

## How It Works

When `template-body` or `template-headers` is set, the plugin switches to "template mode":
- **Request body**: read from `template-body` file, with `{{PLACEHOLDER}}` values substituted at runtime.
- **Request headers**: read from `template-headers` file, with `{{PLACEHOLDER}}` values substituted.
- **Response extraction**: `template-response` specifies a JSON path to extract the description text from the API response.

## Properties

| Property | Description |
|---|---|
| `template-body` | Path to a file containing the JSON request body template |
| `template-headers` | Path to a file containing per-line header templates (one header per line, `Name: {{VALUE}}` format) |
| `template-response` | JSON path expression for extracting the description from the response |

---

## Body Template

File referenced by `template-body`. Example for an OpenAI-compatible endpoint:

```json
{
  "model": "{{MODEL}}",
  "messages": [
    {"role": "user", "content": [
      {"type": "image_url", "image_url": {"url": "{{IMAGE_0_DATA_URL}}"}},
      {"type": "text", "text": "{{USER_PROMPT}}"}
    ]}
  ]
}
```

## Headers Template

File referenced by `template-headers`. Example:

```
Content-Type: application/json
Authorization: Bearer {{API_KEY}}
X-Custom-Header: {{MODEL}}
```

## Available Placeholders

| Placeholder | Description |
|---|---|
| `{{BASE_URL}}` | The `base-url` property value |
| `{{MODEL}}` | The `model` property value |
| `{{SYSTEM_PROMPT}}` | The `system-prompt` property value |
| `{{USER_PROMPT}}` | The `user-prompt` property value |
| `{{IMAGE_0_BASE64}}` | First frame as raw base64-encoded JPEG |
| `{{IMAGE_0_DATA_URL}}` | First frame as `data:image/jpeg;base64,...` |
| `{{IMAGE_ARRAY_JSON}}` | All frames as a JSON image array |
| `{{TEMPERATURE}}` | Temperature value |
| `{{MAX_TOKENS}}` | Max output tokens |
| `{{TOP_P}}` | Top-p value |
| `{{SCHEMA_JSON}}` | Response format schema |
| `{{REQUEST_ID}}` | Unique request identifier (reserved for future use; currently always empty) |
| `{{STOP_SEQUENCES_JSON}}` | Stop sequences as JSON array |

## Response Template

Set `template-response` to a JSON path expression like `$.choices[0].message.content`. The default when in template mode (but `template-response` is unset) is `$.choices[0].message.content` for success and `$.error.message` for errors.

## Usage Example

```bash
# Use custom template files with a nonstandard API
gst-launch-1.0 videotestsrc ! videoconvert ! \
  vlmvision base-url=http://localhost:8765 api-key="" model=custom-model \
    template-body=/path/to/body.json \
    template-headers=/path/to/headers.txt \
    template-response="$.result.text" \
  ! videoconvert ! autovideosink
```

