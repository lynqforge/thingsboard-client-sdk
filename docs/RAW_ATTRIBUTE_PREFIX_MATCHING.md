# RAW Attribute Prefix Matching (Implemented)

Prefix matching is fully implemented in the current RAW attribute path.

## What Is Implemented

- `Raw_Attribute_Callback` supports exact and prefix matching.
- `Matches(...)` determines if a callback handles a discovered top-level key.
- `Raw_Shared_Attribute_Update` scans top-level keys and invokes the first matching callback.
- Prefix subscriptions can auto-request all shared attributes (`{}` request payload).

## Callback Construction

```cpp
// Exact key
Raw_Attribute_Callback exact_cb(&onRaw, "cfg");

// Prefix key (matches DEVICE_battery1, DEVICE_inverter2, ...)
Raw_Attribute_Callback prefix_cb(&onRawDevice, "DEVICE_", true);
```

## Matching Rules

- `is_prefix == false`: exact compare (`strcmp`)
- `is_prefix == true`: prefix compare (`strncmp(key, prefix, strlen(prefix)) == 0`)
- Empty/null callback key does not match.
- For each discovered top-level attribute key, first matching callback wins.

## Request Behavior

`Raw_Attributes_Subscribe(callback, true)` (default) automatically requests current values.

- Exact callback request payload: `{"sharedKeys":"<key>"}`
- Prefix callback request payload: `{}` (request all, filter client-side)

The response is received on `v1/devices/me/attributes/response/+`.

## Typical Gateway Pattern

```cpp
void onRawDevice(char const * key, char const * value, size_t length) {
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, value, length)) {
        return;
    }
    // parse DEVICE_* config
}

void onSharedSimple(JsonObjectConst const & data) {
    // handle OUTPUT1, fw_*, etc.
}

Raw_Attribute_Callback device_cb(&onRawDevice, "DEVICE_", true);
raw_shared_update.Set_Json_Passthrough_Callback(&onSharedSimple);
raw_shared_update.Raw_Attributes_Subscribe(device_cb);  // request_on_subscribe defaults to true
```

## Operational Notes

- Prefix matching removes the need to register one callback per device attribute key.
- Large responses containing many `DEVICE_*` values require adequate MQTT RX buffer sizing.
- If mixed payloads contain many top-level keys, increase `ThingsBoardSized<MaxResponse>` in static mode.

**CPU:** Slightly more than exact matching due to iteration:
- Exact match: `O(n)` callbacks × `O(1)` strcmp = `O(n)`
- Prefix match: `O(n)` callbacks × `O(m)` strncmp = `O(n×m)`
- Where `n` = number of callbacks, `m` = prefix length (typically 7-10 chars)

**Practical Impact:** Negligible - we're still doing a single JSON parse + string extractions, which dominate the timing

## Example: ModbusToCloud Cleanup

With this enhancement, the gateway code simplifies from:

**Current (with workaround):**
```cpp
void processSharedAttributeUpdate(const JsonObjectConst& data) {
    // ... manual DEVICE_* checking ...
    for (JsonPairConst pair : data) {
        const char* key = pair.key().c_str();
        if (strncmp(key, "DEVICE_", 7) == 0) {
            if (value.is<const char*>()) {
                processRawDeviceConfigUpdate(key, jsonStr, strlen(jsonStr));
            } else if (value.is<JsonObjectConst>()) {
                String serialized;
                serializeJson(value, serialized);
                processRawDeviceConfigUpdate(key, serialized.c_str(), serialized.length());
            }
        }
    }
    // ... handle other attributes ...
}

void subscribeToSharedAttributes() {
    raw_shared_update.Set_Json_Passthrough_Callback(&processSharedAttributeUpdate);  // Manual routing!
    // ...
}
```

**Simplified (with prefix matching):**
```cpp
void onDeviceConfig(char const * key, char const * value, size_t length) {
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, value, length);
    parseDeviceConfig(doc);
}

void subscribeToSharedAttributes() {
    Raw_Attribute_Callback device_config(&onDeviceConfig, "DEVICE_", true);  // Direct prefix!
    raw_shared_update.Raw_Attributes_Subscribe(device_config);
    
    raw_shared_update.Set_Json_Passthrough_Callback(&processSimpleAttributes);  // Only simple attrs
}
```

**Benefits:**
- ✅ Eliminate manual string checking (`strncmp`, string extraction)
- ✅ Cleaner architecture (direct routing, no double-handling)
- ✅ Type-safe (callback receives char*, not JsonVariant)
- ✅ Extensible (add more prefixes without code changes)

## Future Enhancements

1. **Pattern-based matching:** Support wildcards like `"DEVICE_*"` (currently must be prefix only)
2. **Callback filtering:** Add optional filter function to further refine which attributes to process
3. **Request optimization:** Smarter request handling for prefix-based subscriptions
4. **Statistics:** Track matches per callback for debugging/monitoring

## Recommendation

This enhancement solves the architectural problem identified in the ModbusToCloud example. Implement with:

1. **Priority:** High - Directly addresses user request
2. **Complexity:** Low - Minimal changes to existing code
3. **Impact:** Backward compatible - No breaking changes
4. **Timeline:** 1-2 hours for implementation + testing

The cleaner architecture eliminates the need for manual passthrough callbacks and prefix checking, making the code more maintainable and the intent clearer.
