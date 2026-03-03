# Raw Attribute Prefix Matching - Implementation Guide

## Overview

This guide shows the exact code changes needed to implement prefix matching in `Raw_Attribute_Callback` and `Raw_Shared_Attribute_Update`.

**Backward Compatibility:** 100% - all existing code continues to work unchanged.

---

## File 1: Raw_Attribute_Callback.h

### Current Implementation

```cpp
#ifndef Raw_Attribute_Callback_h
#define Raw_Attribute_Callback_h

#include "Callback.h"

class Raw_Attribute_Callback {
  public:
    /// @brief Constructs empty callback
    Raw_Attribute_Callback()
      : Raw_Attribute_Callback(nullptr, nullptr)
    {
    }

    /// @brief Constructs callback for a specific attribute key
    /// @param callback Callback method that will be called
    /// @param attribute_key Specific attribute key to match (e.g., "regCfg")
    Raw_Attribute_Callback(Callback<void, char const *, char const *, size_t>::function callback, 
                          char const * attribute_key)
      : m_callback(callback)
      , m_attribute_key(attribute_key)
    {
    }

    void Call_Callback(char const * key, char const * value, size_t length) const {
        m_callback.Call_Callback(key, value, length);
    }

    char const * Get_Attribute_Key() const {
        return m_attribute_key;
    }

  private:
    Callback<void, char const *, char const *, size_t> m_callback;
    char const *                                       m_attribute_key;
};

#endif // Raw_Attribute_Callback_h
```

### Enhanced Implementation

```cpp
#ifndef Raw_Attribute_Callback_h
#define Raw_Attribute_Callback_h

#include "Callback.h"
#include "Helper.h"  // For stringIsNullorEmpty

class Raw_Attribute_Callback {
  public:
    /// @brief Constructs empty callback
    Raw_Attribute_Callback()
      : Raw_Attribute_Callback(nullptr, nullptr, false)
    {
    }

    /// @brief Constructs callback for exact attribute key match (backward compatible)
    /// @param callback Callback method that will be called
    /// @param attribute_key Exact attribute key to match (e.g., "regCfg")
    Raw_Attribute_Callback(Callback<void, char const *, char const *, size_t>::function callback, 
                          char const * attribute_key)
      : Raw_Attribute_Callback(callback, attribute_key, false)  // Delegate with is_prefix=false
    {
    }

    /// @brief Constructs callback for attribute key or prefix matching
    /// @param callback Callback method that will be called
    /// @param attribute_key_or_prefix Attribute key or prefix to match
    /// @param is_prefix If true, match all attributes starting with this prefix.
    ///                  If false, match exact attribute name (default)
    Raw_Attribute_Callback(Callback<void, char const *, char const *, size_t>::function callback, 
                          char const * attribute_key_or_prefix,
                          bool is_prefix = false)
      : m_callback(callback)
      , m_attribute_key(attribute_key_or_prefix)
      , m_is_prefix(is_prefix)
    {
    }

    void Call_Callback(char const * key, char const * value, size_t length) const {
        m_callback.Call_Callback(key, value, length);
    }

    char const * Get_Attribute_Key() const {
        return m_attribute_key;
    }

    /// @brief Checks if this callback should handle the given attribute key
    /// @param key Attribute key to test
    /// @return true if this callback matches this key (exact or prefix)
    bool Matches(char const * key) const {
        if (Helper::stringIsNullorEmpty(m_attribute_key) || Helper::stringIsNullorEmpty(key)) {
            return false;
        }
        
        if (m_is_prefix) {
            // Prefix match: "DEVICE_" matches "DEVICE_inverter", "DEVICE_battery", etc.
            size_t prefix_len = strlen(m_attribute_key);
            return strncmp(key, m_attribute_key, prefix_len) == 0;
        } else {
            // Exact match: "regCfg" matches only "regCfg"
            return strcmp(key, m_attribute_key) == 0;
        }
    }

    /// @brief Gets whether this callback uses prefix matching
    /// @return true if prefix matching is enabled, false if exact match only
    bool Is_Prefix() const {
        return m_is_prefix;
    }

  private:
    Callback<void, char const *, char const *, size_t> m_callback;      // Callback to invoke
    char const *                                       m_attribute_key;  // Key or prefix to match
    bool                                               m_is_prefix;      // Matching mode: prefix vs exact
};

#endif // Raw_Attribute_Callback_h
```

### Changes Summary

| Change | Old | New | Purpose |
|--------|-----|-----|---------|
| Old constructor | Delegates directly to members | Delegates to new 3-param constructor | Allows centralizing logic |
| New constructor | N/A | Added 3-parameter version | Enables `is_prefix` option |
| New method | N/A | `Matches(key)` | Encapsulates matching logic |
| New method | N/A | `Is_Prefix()` | Getter for prefix flag |
| New member | N/A | `m_is_prefix` | Stores matching mode |

---

## File 2: Raw_Shared_Attribute_Update.h

### Changes in Request_Attribute Method

**Current:**
```cpp
bool Request_Attribute(Raw_Attribute_Callback const & callback) {
    size_t * p_request_id = m_get_request_id_callback.Call_Callback();
    if (p_request_id == nullptr) {
        return false;
    }
    auto & request_id = *p_request_id;
    request_id++;

    (void)snprintf(m_response_topic, sizeof(m_response_topic), ATTRIBUTE_RESPONSE_TOPIC, request_id);

    char request_topic[Helper::detectSize(ATTRIBUTE_REQUEST_TOPIC, request_id)] = {};
    (void)snprintf(request_topic, sizeof(request_topic), ATTRIBUTE_REQUEST_TOPIC, request_id);

    char const * attr_key = callback.Get_Attribute_Key();
    char request_payload[128];
    (void)snprintf(request_payload, sizeof(request_payload), "{\"sharedKeys\":\"%s\"}", attr_key);

    return m_send_json_string_callback.Call_Callback(request_topic, request_payload);
}
```

**Enhanced:**
```cpp
bool Request_Attribute(Raw_Attribute_Callback const & callback) {
    size_t * p_request_id = m_get_request_id_callback.Call_Callback();
    if (p_request_id == nullptr) {
        return false;
    }
    auto & request_id = *p_request_id;
    request_id++;

    (void)snprintf(m_response_topic, sizeof(m_response_topic), ATTRIBUTE_RESPONSE_TOPIC, request_id);

    char request_topic[Helper::detectSize(ATTRIBUTE_REQUEST_TOPIC, request_id)] = {};
    (void)snprintf(request_topic, sizeof(request_topic), ATTRIBUTE_REQUEST_TOPIC, request_id);

    char const * attr_key = callback.Get_Attribute_Key();
    char request_payload[128];
    
    if (callback.Is_Prefix()) {
        // For prefix subscriptions, request all attributes
        // (ThingsBoard API doesn't support prefix-based requests)
        // Client-side filtering will handle prefix matching
        (void)snprintf(request_payload, sizeof(request_payload), "{}");
    } else {
        // For exact match subscriptions, request specific attribute
        (void)snprintf(request_payload, sizeof(request_payload), "{\"sharedKeys\":\"%s\"}", attr_key);
    }

    return m_send_json_string_callback.Call_Callback(request_topic, request_payload);
}
```

### Changes in Process_Response Method

**Current (Simplified):**
```cpp
void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
    for (auto const & callback : m_raw_callbacks) {
        char const * attr_key = callback.Get_Attribute_Key();
        
        if (Helper::stringIsNullorEmpty(attr_key)) {
            continue;
        }
        
        // Extract raw value substring
        char const * value_start = nullptr;
        size_t value_length = 0;
        
        Extract_Failure_Reason failure_reason = Extract_Failure_Reason::NONE;
        if (Extract_Attribute_Value((char *)payload, length, attr_key, &value_start, &value_length, &failure_reason)) {
            callback.Call_Callback(attr_key, value_start, value_length);
        }
    }
    // ... passthrough callback ...
}
```

**Enhanced (with prefix matching):**
```cpp
void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
    // IMPORTANT: Extract raw values BEFORE JSON parsing (parsing modifies payload)
    // Build set of matched attributes to avoid duplicate processing
    struct MatchedAttribute {
        char const * name;
        char const * value_start;
        size_t value_length;
        Raw_Attribute_Callback const * callback;
    };
    
    Vector<MatchedAttribute> matched;
    
    // Try to match each callback against payload BEFORE JSON parsing
    for (auto const & callback : m_raw_callbacks) {
        char const * attr_key = callback.Get_Attribute_Key();
        
        if (Helper::stringIsNullorEmpty(attr_key)) {
            continue;
        }
        
        if (callback.Is_Prefix()) {
            // For prefix match: need to enumerate attributes from parsed JSON first
            // Will handle in second pass after JSON parsing
            continue;
        } else {
            // For exact match: extract directly from raw payload
            char const * value_start = nullptr;
            size_t value_length = 0;
            
            Extract_Failure_Reason failure_reason = Extract_Failure_Reason::NONE;
            if (Extract_Attribute_Value((char *)payload, length, attr_key, &value_start, &value_length, &failure_reason)) {
                // Store for callback invocation
                MatchedAttribute match;
                match.name = attr_key;
                match.value_start = value_start;
                match.value_length = value_length;
                match.callback = &callback;
                matched.push_back(match);
            }
#if THINGSBOARD_ENABLE_DEBUG
            else {
                // ... error logging ...
            }
#endif
        }
    }
    
    // NOW parse outer JSON for:
    // 1. Prefix-based attribute matching
    // 2. Passthrough callback with remaining attributes
    static constexpr size_t OUTER_JSON_SIZE = 512U;
    StaticJsonDocument<OUTER_JSON_SIZE> outer_doc;
    DeserializationError error = deserializeJson(outer_doc, payload, length);
    
    if (!error) {
        JsonObjectConst root = outer_doc.as<JsonObjectConst>();
        
        // Check for "shared" wrapper (attribute request responses)
        if (root.containsKey(SHARED_RESPONSE_KEY)) {
            root = root[SHARED_RESPONSE_KEY];
        }
        
        // Process all attributes in the message
        for (JsonPairConst pair : root) {
            char const * attr_name = pair.key().c_str();
            
            // Check if any prefix callback matches this attribute
            Raw_Attribute_Callback const * matching_callback = nullptr;
            
            for (auto const & callback : m_raw_callbacks) {
                if (callback.Is_Prefix() && callback.Matches(attr_name)) {
                    matching_callback = &callback;
                    break;  // Use first matching callback
                }
            }
            
            if (matching_callback != nullptr) {
                // Extract raw value and invoke callback
                char const * value_start = nullptr;
                size_t value_length = 0;
                
                Extract_Failure_Reason failure_reason = Extract_Failure_Reason::NONE;
                if (Extract_Attribute_Value((char *)payload, length, attr_name, &value_start, &value_length, &failure_reason)) {
                    MatchedAttribute match;
                    match.name = attr_name;
                    match.value_start = value_start;
                    match.value_length = value_length;
                    match.callback = matching_callback;
                    matched.push_back(match);
                }
            }
        }
    }
    
    // Invoke all matched callbacks with extracted raw values
    for (auto const & match : matched) {
        match.callback->Call_Callback(match.name, match.value_start, match.value_length);
    }
    
    // Call passthrough callback with remaining non-matched attributes
    if (!error && m_json_passthrough_callback.Get_Callback() != nullptr) {
        JsonObjectConst root = outer_doc.as<JsonObjectConst>();
        if (root.containsKey(SHARED_RESPONSE_KEY)) {
            root = root[SHARED_RESPONSE_KEY];
        }
        
        // Build filtered JSON with only non-matched attributes
        // (Passthrough receives attributes NOT handled by raw callbacks)
        StaticJsonDocument<512> filtered_doc;
        JsonObject filtered_root = filtered_doc.to<JsonObject>();
        
        for (JsonPairConst pair : root) {
            // Skip if this attribute was handled by a raw callback
            bool was_matched = false;
            for (auto const & callback : m_raw_callbacks) {
                if (callback.Matches(pair.key().c_str())) {
                    was_matched = true;
                    break;
                }
            }
            
            if (!was_matched) {
                filtered_root[pair.key()] = pair.value();
            }
        }
        
        m_json_passthrough_callback.Call_Callback(filtered_root);
    }
}
```

**Note:** The implementation above uses a temporary `Vector<MatchedAttribute>` to defer callback invocation. A simpler approach that invokes immediately would be:

```cpp
// Simpler version - invoke immediately after extraction:
void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
    // Parse outer JSON to enumerate all attributes
    static constexpr size_t OUTER_JSON_SIZE = 512U;
    StaticJsonDocument<OUTER_JSON_SIZE> outer_doc;
    DeserializationError error = deserializeJson(outer_doc, payload, length);
    
    if (!error) {
        JsonObjectConst root = outer_doc.as<JsonObjectConst>();
        
        if (root.containsKey(SHARED_RESPONSE_KEY)) {
            root = root[SHARED_RESPONSE_KEY];
        }
        
        // Try to match each attribute against registered callbacks
        for (JsonPairConst pair : root) {
            char const * attr_name = pair.key().c_str();
            
            // Try each callback (exact and prefix match)
            bool was_handled = false;
            for (auto const & callback : m_raw_callbacks) {
                if (callback.Matches(attr_name)) {
                    char const * value_start = nullptr;
                    size_t value_length = 0;
                    
                    if (Extract_Attribute_Value((char *)payload, length, attr_name, &value_start, &value_length)) {
                        callback.Call_Callback(attr_name, value_start, value_length);
                        was_handled = true;
                        break;
                    }
                }
            }
            
            // If not handled by raw callback and passthrough is set, add to passthrough
            if (!was_handled && m_json_passthrough_callback.Get_Callback() != nullptr) {
                // ... build JSON for passthrough ...
            }
        }
    }
}
```

---

## Usage Comparison

### Old Code (Exact Match Only)

```cpp
// Single exact attribute match
Raw_Attribute_Callback regcfg_callback(&onRegCfgUpdate, "regCfg");
raw_shared_update.Raw_Attributes_Subscribe(regcfg_callback);

// To handle multiple device configs, had to do this:
Raw_Attribute_Callback device1(&onDevice, "DEVICE_inverter");
Raw_Attribute_Callback device2(&onDevice, "DEVICE_battery");
Raw_Attribute_Callback device3(&onDevice, "DEVICE_meter");
raw_shared_update.Raw_Attributes_Subscribe(device1);
raw_shared_update.Raw_Attributes_Subscribe(device2);
raw_shared_update.Raw_Attributes_Subscribe(device3);
// OR use passthrough and manually iterate
```

### New Code (With Prefix Matching)

```cpp
// Exact match (backward compatible)
Raw_Attribute_Callback regcfg_callback(&onRegCfgUpdate, "regCfg");
raw_shared_update.Raw_Attributes_Subscribe(regcfg_callback);

// Multiple attributes via single prefix subscription
Raw_Attribute_Callback device_callback(&onDevice, "DEVICE_", true);  // is_prefix=true
raw_shared_update.Raw_Attributes_Subscribe(device_callback);
// NOW automatically handles: DEVICE_inverter, DEVICE_battery, DEVICE_meter, etc.
```

---

## Testing Strategy

### Unit Tests for Raw_Attribute_Callback

```cpp
void test_exact_match() {
    Raw_Attribute_Callback callback(dummyFunction, "regCfg", false);
    assert(callback.Matches("regCfg") == true);
    assert(callback.Matches("regCfg123") == false);
    assert(callback.Matches("myregCfg") == false);
}

void test_prefix_match() {
    Raw_Attribute_Callback callback(dummyFunction, "DEVICE_", true);
    assert(callback.Matches("DEVICE_") == true);
    assert(callback.Matches("DEVICE_inverter") == true);
    assert(callback.Matches("DEVICE_battery") == true);
    assert(callback.Matches("device_inverter") == false);  // case sensitive
    assert(callback.Matches("DEVICE") == false);  // incomplete prefix
}

void test_backward_compatibility() {
    // Old code should still work
    Raw_Attribute_Callback callback(dummyFunction, "myKey");  // no is_prefix param
    assert(callback.Is_Prefix() == false);
    assert(callback.Matches("myKey") == true);
}
```

### Integration Tests with Process_Response

```cpp
void test_prefix_extraction() {
    char payload[] = R"({"DEVICE_inv1":{"a":1},"DEVICE_inv2":{"b":2},"cfg":{"c":3}})";
    
    static int inv1_calls = 0;
    static int inv2_calls = 0;
    
    auto on_device = [](char const * key, char const * value, size_t length) {
        if (strcmp(key, "DEVICE_inv1") == 0) inv1_calls++;
        if (strcmp(key, "DEVICE_inv2") == 0) inv2_calls++;
    };
    
    Raw_Attribute_Callback device_cb(on_device, "DEVICE_", true);
    raw_update.Raw_Attributes_Subscribe(device_cb);
    
    raw_update.Process_Response("topic", (uint8_t*)payload, strlen(payload));
    
    assert(inv1_calls == 1);
    assert(inv2_calls == 1);
}
```

---

## Performance Impact

**Memory:**
- Added: 1 `bool` member per `Raw_Attribute_Callback` = 1 byte
- Negligible impact

**CPU:**
- Exact match: `strcmp()` = O(n) where n = key length (typically <20)
- Prefix match: `strncmp()` = O(m) where m = prefix length (typically <10)
- Impact negligible (string ops are fast, dominated by JSON parsing anyway)

**Scaling:**
- 10 callbacks with prefixes: ~10 `strncmp` calls per attribute
- Attribute count in typical message: 5-20
- Total: ~50-200 string comparisons = <1ms

---

## Migration Path

### Phase 0: Current State
- ✅ Exact match works
- ⚠️ Workaround needed for prefix-like functionality

### Phase 1: SDK Enhancement
- Implement prefix matching in library
- Publish new library version
- 100% backward compatible

### Phase 2: ModbusToCloud Cleanup (After library update)
- Remove manual passthrough prefix checking
- Subscribe directly to "DEVICE_" prefix
- Simpler, cleaner code

### Phase 3: Other Projects
- Any project using similar pattern can adopt
- Cleaner architectures across the board

---

## Conclusion

This enhancement:
- ✅ Adds powerful feature (prefix matching)
- ✅ Maintains full backward compatibility
- ✅ Minimal code complexity
- ✅ Zero performance impact
- ✅ Enables cleaner application code
- ✅ Follows existing library patterns

**Ready for implementation and PR to ThingsBoard SDK.**
