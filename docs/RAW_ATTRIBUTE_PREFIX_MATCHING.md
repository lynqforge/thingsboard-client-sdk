# Raw Attribute API - Prefix Matching Extension

## Problem Statement

The current `Raw_Attribute_Callback` supports **exact attribute name matching only**. This requires a separate subscription for each attribute, or manual iteration in a passthrough callback.

**Current Limitation:**
```cpp
// Only matches "DEVICE_inverter" exactly
Raw_Attribute_Callback device_callback(&callback, "DEVICE_inverter");

// To match multiple DEVICE_* attributes, must subscribe to each one:
Raw_Attribute_Callback device1(&callback, "DEVICE_inverter");
Raw_Attribute_Callback device2(&callback, "DEVICE_battery");
Raw_Attribute_Callback device3(&callback, "DEVICE_meter");
// ... or use passthrough and manually check prefixes
```

**Desired Solution:**
```cpp
// NEW: Match ALL attributes starting with "DEVICE_" prefix
Raw_Attribute_Callback device_callback(&callback, "DEVICE_", true);  // is_prefix=true
// Automatically matches: DEVICE_inverter, DEVICE_battery, DEVICE_meter, etc.
```

## Solution Design

### 1. Extend Raw_Attribute_Callback

Add optional `is_prefix` parameter (default `false` for backward compatibility):

```cpp
class Raw_Attribute_Callback {
  public:
    /// @brief Constructs callback for exact attribute key match
    /// @param callback User callback function
    /// @param attribute_key Specific attribute key to match (e.g., "regCfg")
    Raw_Attribute_Callback(Callback<void, char const *, char const *, size_t>::function callback, 
                           char const * attribute_key)
      : Raw_Attribute_Callback(callback, attribute_key, false)  // Delegate with is_prefix=false
    {
        // Nothing to do
    }

    /// @brief Constructs callback for attribute key matching (exact or prefix)
    /// @param callback User callback function
    /// @param attribute_key Attribute key or prefix to match
    /// @param is_prefix If true, match attributes starting with this prefix.
    ///                  If false, match exact attribute name (default)
    Raw_Attribute_Callback(Callback<void, char const *, char const *, size_t>::function callback, 
                           char const * attribute_key,
                           bool is_prefix = false)
      : m_callback(callback)
      , m_attribute_key(attribute_key)
      , m_is_prefix(is_prefix)
    {
        // Nothing to do
    }

    /// @brief Checks if this callback matches the given attribute key
    /// @param key Attribute key to test
    /// @return true if this callback should handle this key
    bool Matches(char const * key) const {
        if (Helper::stringIsNullorEmpty(m_attribute_key) || Helper::stringIsNullorEmpty(key)) {
            return false;
        }
        
        if (m_is_prefix) {
            // Prefix match: "DEVICE_" matches "DEVICE_inverter"
            return strncmp(key, m_attribute_key, strlen(m_attribute_key)) == 0;
        } else {
            // Exact match: "regCfg" matches only "regCfg"
            return strcmp(key, m_attribute_key) == 0;
        }
    }

    /// @brief Gets whether this callback uses prefix matching
    /// @return Whether prefix matching is enabled
    bool Is_Prefix() const {
        return m_is_prefix;
    }

  private:
    Callback<void, char const *, char const *, size_t> m_callback;      // Callback to call
    char const *                                       m_attribute_key;  // Key or prefix to match
    bool                                               m_is_prefix;      // Whether to match by prefix
};
```

### 2. Update Raw_Shared_Attribute_Update::Process_Response

Change the matching logic in `Process_Response`:

**Before:**
```cpp
void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
    for (auto const & callback : m_raw_callbacks) {
        char const * attr_key = callback.Get_Attribute_Key();
        
        // ... exact match logic ...
        if (Extract_Attribute_Value((char *)payload, length, attr_key, ...)) {
            callback.Call_Callback(attr_key, value_start, value_length);
        }
    }
}
```

**After:**
```cpp
void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
    // Parse outer JSON to enumerate all attribute keys
    static constexpr size_t OUTER_JSON_SIZE = 512U;
    StaticJsonDocument<OUTER_JSON_SIZE> outer_doc;
    DeserializationError error = deserializeJson(outer_doc, payload, length);
    
    if (error) {
        // Not valid JSON or too complex - skip processing
        return;
    }
    
    JsonObjectConst root = outer_doc.as<JsonObjectConst>();
    
    // Check for "shared" wrapper (attribute request responses)
    if (root.containsKey(SHARED_RESPONSE_KEY)) {
        root = root[SHARED_RESPONSE_KEY];
    }
    
    // Iterate through all attributes in the message
    for (JsonPairConst pair : root) {
        char const * attr_name = pair.key().c_str();
        
        // Try to match against all registered callbacks (exact and prefix)
        for (auto const & callback : m_raw_callbacks) {
            if (callback.Matches(attr_name)) {
                // Extract raw value for this matching attribute
                char const * value_start = nullptr;
                size_t value_length = 0;
                
                Extract_Failure_Reason failure_reason = Extract_Failure_Reason::NONE;
                if (Extract_Attribute_Value((char *)payload, length, attr_name, 
                                           &value_start, &value_length, &failure_reason)) {
                    // Call user callback with the matched attribute
                    callback.Call_Callback(attr_name, value_start, value_length);
                }
#if THINGSBOARD_ENABLE_DEBUG
                else {
                    // ... error logging ...
                }
#endif
                break;  // Move to next attribute (each attribute matched by first matching callback)
            }
        }
    }
}
```

### 3. Update Auto-Request for Prefix Subscriptions

When a prefix subscription is created, we need to request all attributes (cannot request by prefix via MQTT API).

**Note:** The standard ThingsBoard API only supports requesting specific named attributes. For prefix-based subscriptions, we have two options:

**Option A: Request All (Recommended)**
```cpp
bool Request_Attribute(Raw_Attribute_Callback const & callback) {
    // ... existing code ...
    
    char const * attr_key = callback.Get_Attribute_Key();
    char request_payload[128];
    
    if (callback.Is_Prefix()) {
        // Prefix match: Request all attributes (empty sharedKeys or specific pattern)
        // ThingsBoard will return all attributes, we filter client-side
        (void)snprintf(request_payload, sizeof(request_payload), "{}");
    } else {
        // Exact match: Request specific attribute
        (void)snprintf(request_payload, sizeof(request_payload), "{\"sharedKeys\":\"%s\"}", attr_key);
    }
    
    return m_send_json_string_callback.Call_Callback(request_topic, request_payload);
}
```

**Option B: Don't Auto-Request for Prefix**
```cpp
if (request_on_subscribe && !callback.Is_Prefix()) {
    Request_Attribute(callback);  // Only request for exact matches
}
```

We recommend **Option A** (request all) for consistency.

## Backward Compatibility

The solution maintains **100% backward compatibility**:

```cpp
// OLD CODE (still works)
Raw_Attribute_Callback callback1(&onRegCfg, "regCfg");
raw_shared_update.Raw_Attributes_Subscribe(callback1);  // Exact match, is_prefix defaults to false

// NEW CODE (same functionality, explicit parameter)
Raw_Attribute_Callback callback2(&onRegCfg, "regCfg", false);
raw_shared_update.Raw_Attributes_Subscribe(callback2);  // Same as above

// NEW CODE (new feature)
Raw_Attribute_Callback callback3(&onDevice, "DEVICE_", true);  // Prefix match
raw_shared_update.Raw_Attributes_Subscribe(callback3);  // Matches any DEVICE_*
```

## Usage Examples

### Example 1: Device Configuration (Prefix Matching)

```cpp
void onDeviceConfigReceived(char const * key, char const * value, size_t length) {
    SERIALPRINTF("[TB] Device config '%s': %u bytes\n", key, (unsigned)length);
    
    // Parse with your DynamicJsonDocument
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, value, length);
    
    // Process configuration (key will be like "DEVICE_inverter", "DEVICE_battery", etc.)
    parseDeviceConfig(key, doc);
}

void subscribeToAttributes() {
    // Subscribe to DEVICE_* prefix - matches DEVICE_inverter, DEVICE_battery, etc.
    Raw_Attribute_Callback device_callback(&onDeviceConfigReceived, "DEVICE_", true);
    raw_shared_update.Raw_Attributes_Subscribe(device_callback, true);  // with auto-request
    
    // Other exact-match attributes still work
    Raw_Attribute_Callback config_callback(&onConfigUpdate, "cfg");
    raw_shared_update.Raw_Attributes_Subscribe(config_callback);
}
```

### Example 2: Multiple Prefixes

```cpp
void onConfigReceived(char const * key, char const * value, size_t length) {
    if (strncmp(key, "DEVICE_", 7) == 0) {
        // Handle device config
    } else if (strncmp(key, "CONFIG_", 7) == 0) {
        // Handle other config
    } else if (strcmp(key, "regCfg") == 0) {
        // Handle exact match
    }
}

void subscribeToAttributes() {
    // Multiple prefix subscriptions to single callback
    Raw_Attribute_Callback device_callback(&onConfigReceived, "DEVICE_", true);
    raw_shared_update.Raw_Attributes_Subscribe(device_callback);
    
    Raw_Attribute_Callback config_callback(&onConfigReceived, "CONFIG_", true);
    raw_shared_update.Raw_Attributes_Subscribe(config_callback);
    
    Raw_Attribute_Callback exact_callback(&onConfigReceived, "regCfg");
    raw_shared_update.Raw_Attributes_Subscribe(exact_callback);
}
```

### Example 3: Gateway With Per-Device Configuration (Use Case From ModbusToCloud)

**Current workaround (manual prefix checking):**
```cpp
void processSharedAttributeUpdate(const JsonObjectConst& data) {
    // Manual iteration to find DEVICE_* attributes
    for (JsonPairConst pair : data) {
        const char* key = pair.key().c_str();
        if (strncmp(key, "DEVICE_", 7) == 0) {
            // Manual routing...
        }
    }
}
```

**With prefix matching (cleaner):**
```cpp
void onDeviceAttribute(char const * key, char const * value, size_t length) {
    SERIALPRINTF("[TB] Device '%s' config received: %u bytes\n", key, (unsigned)length);
    
    // Parse and process device config
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, value, length);
    parseDeviceConfig(key + 7, doc);  // Skip "DEVICE_" prefix for device name
}

void subscribeToSharedAttributes() {
    // Subscribe to device configs via prefix - NO MANUAL ITERATION NEEDED!
    Raw_Attribute_Callback device_config(&onDeviceAttribute, "DEVICE_", true);
    raw_shared_update.Raw_Attributes_Subscribe(device_config);
    
    // Passthrough callback still handles other attributes
    raw_shared_update.Set_Json_Passthrough_Callback(&processSimpleAttributes);
}
```

This eliminates the workaround! The architecture becomes:
- **Before:** Server → Passthrough → Manual Check → Route to Raw
- **After:** Server → Raw (direct prefix match) + Passthrough (other attributes)

## Implementation Checklist

- [ ] Extend `Raw_Attribute_Callback` with `is_prefix` parameter
- [ ] Add `Matches()` method to `Raw_Attribute_Callback`
- [ ] Add `Is_Prefix()` getter to `Raw_Attribute_Callback`
- [ ] Update `Raw_Shared_Attribute_Update::Process_Response()` to enumerate attributes
- [ ] Update attribute matching logic to use `Matches()` method
- [ ] Update `Request_Attribute()` for prefix handling (request all or skip)
- [ ] Add unit tests for prefix matching
- [ ] Update documentation and examples
- [ ] Verify backward compatibility with existing code
- [ ] Test with ModbusToCloud example (DEVICE_* attributes)

## Performance Considerations

**Memory:** Minimal - only adds one `bool` member to `Raw_Attribute_Callback`

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
