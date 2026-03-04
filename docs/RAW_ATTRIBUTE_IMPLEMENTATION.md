# RAW Shared Attribute Update (Current Behavior)

This document reflects the current implementation in `src/Raw_Shared_Attribute_Update.h` and its interaction with the ThingsBoard dispatcher.

## Goal

Handle large/complex shared attributes (for example `DEVICE_*` JSON configs) as raw strings, while still allowing regular JSON handlers (shared attributes, OTA) to process simple keys in the same MQTT message.

## End-to-End Flow

1. MQTT message arrives on `v1/devices/me/attributes` or `v1/devices/me/attributes/response/<id>`.
2. `ThingsBoard::On_MQTT_Message()` runs RAW API handlers first.
3. `Raw_Shared_Attribute_Update::Process_Response()` scans top-level keys without fully parsing nested values.
4. Matching RAW callbacks receive extracted raw substrings (`key`, `value`, `length`).
5. RAW handler deserializes the payload once (single parse) and forwards JSON via passthrough callback.
6. Dispatcher continues JSON API handling for attribute topics, so OTA/shared handlers still receive updates.

## Matching Model

- Exact matching and prefix matching are both supported through `Raw_Attribute_Callback::Matches(...)`.
- Typical gateway usage: `"DEVICE_"` prefix callback to process many per-device configs.
- Non-matching keys (like `fw_*`, `OUTPUT1`) still flow through passthrough JSON callback.

## Auto-Request on Subscribe

`Raw_Attributes_Subscribe(..., request_on_subscribe = true)` does all of the following by default:

- Subscribes to `v1/devices/me/attributes`
- Subscribes to `v1/devices/me/attributes/response/+`
- Sends a shared attribute request immediately

Request payload behavior:

- Prefix callback (`Is_Prefix() == true`): sends `{}` (request all shared attributes)
- Exact callback: sends `{"sharedKeys":"<key>"}`

## Why Single-Parse Passthrough Exists

ArduinoJson zero-copy deserialization writes null terminators into the mutable payload buffer. If the same payload is deserialized a second time elsewhere, it can fail or produce invalid data.

Current mitigation:

- RAW handler performs one controlled `deserializeJson(...)`
- Forwards parsed root object via passthrough callback
- Returns from RAW processing path to avoid unsafe reparse patterns

## Dispatcher Behavior

The dispatcher logic intentionally does **not** short-circuit JSON handlers for attribute topics:

- For non-attribute RAW topics (for example binary chunks): RAW can short-circuit.
- For attribute topics: RAW + JSON coexist so OTA/shared callbacks still work.

## Runtime Sizing Guidance

### MQTT RX Buffer

Large server sync payloads containing multiple `DEVICE_*` objects can exceed 4 KB.

- If payload is truncated/dropped, increase receive buffer (for example to 16384).

### ThingsBoard `MaxResponse`

Top-level key count is enforced in `ThingsBoardSized<MaxResponse, ...>` for static mode.

- Default `MaxResponse` is 8.
- Mixed payloads (`fw_*`, `OUTPUT*`, multiple `DEVICE_*`) can exceed this.
- Set `MaxResponse` higher (for example 16+) in your application instance.

### Raw Response Topic Buffer

`Raw_Shared_Attribute_Update` currently uses a dedicated response-topic buffer sized for full request-response topic strings. Keep it large enough for `v1/devices/me/attributes/response/<id>` plus terminator.

## Example Usage

```cpp
Raw_Attribute_Callback raw_device_callback(&processRawDeviceConfigUpdate, "DEVICE_", true);
raw_shared_update.Set_Json_Passthrough_Callback(&processSharedAttributeUpdate);
raw_shared_update.Raw_Attributes_Subscribe(raw_device_callback);  // auto-request enabled by default
```

## Known Tradeoffs

- Passthrough currently forwards all keys, including keys already consumed by RAW callbacks.
- If regular JSON handlers do heavy processing on large nested objects, increase `MaxResponse` and ensure handler-side filtering is strict by key/type.
- Removing matched RAW keys before JSON forwarding is possible in principle, but requires careful payload/document handling to avoid extra memory pressure and zero-copy hazards.

## Recommended Baseline (ESP32 Gateway)

- MQTT RX buffer: 16384
- `ThingsBoardSized<16>` (or larger based on key count)
- RAW prefix callback for `DEVICE_`
- JSON passthrough callback for simple/OTA/shared keys
