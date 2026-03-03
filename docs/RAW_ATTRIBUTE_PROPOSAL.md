# Proposal: Raw Shared Attribute API for ThingsBoard Client SDK

## Problem Statement

When sending complex nested JSON structures via ThingsBoard shared attributes (like `regCfg`), the ThingsBoard client library attempts to fully parse the entire JSON hierarchy. This causes:

1. **Parsing Failures**: The library calculates JsonDocument size based on ALL commas/brackets/braces in the payload, including those deeply nested within attribute values
2. **Memory Exhaustion**: A simple attribute with nested JSON can require 15+ fields instead of the expected 8 (MaxResponse)
3. **Configuration Limitation**: Increasing `MaxResponse` globally affects ALL API implementations and wastes memory for simple responses

**Example Error:**
```
[TB] Attempt to enter too many JSON fields into StaticJsonDocument (15), increase (MaxResponse) (8) accordingly
```

**Current Payload:**
```json
{
  "regCfg": {
    "rtu": {
      "baud": 9600,
      "devices": [{
        "uid": 1,
        "name": "Inverter",
        "maps": [/* many register mappings */]
      }]
    }
  }
}
```

## Why THINGSBOARD_ENABLE_DYNAMIC Doesn't Solve This

Setting `THINGSBOARD_ENABLE_DYNAMIC=1` would:
- ✅ Dynamically allocate JsonDocument on heap
- ❌ **Still attempt to parse the entire nested JSON structure**
- ❌ Still count all commas/brackets/braces to calculate size
- ❌ Still fail if the nested structure is too complex
- ❌ Allocate heap memory unnecessarily for every response

**What We Actually Need:**
- Parse ONLY to the attribute key level
- Extract the VALUE as a raw string (unparsed)
- Let application code parse the nested JSON with its own DynamicJsonDocument

---

## Architectural Analysis

### Current Message Flow

```
MQTT Message Arrives
    ↓
On_MQTT_Message(topic, payload, length)
    ↓
    ├─→ Filter APIs with Get_Process_Type() == RAW
    │       ↓
    │   Process_Response(topic, payload, length)  ← OTA uses this
    │       ↓
    │   [Return early - skip JSON parsing]
    │
    └─→ Otherwise:
            ↓
        Calculate size = count(',') + count('{') + count('[')  ← PROBLEM
            ↓
        Allocate JsonDocument(JSON_OBJECT_SIZE(size))
            ↓
        deserializeJson(json_buffer, payload, length)  ← Parses EVERYTHING
            ↓
        Filter APIs with Get_Process_Type() == JSON
            ↓
        Process_Json_Response(topic, json_buffer)  ← Shared_Attribute_Update uses this
```

### Key Insight from OTA Implementation

**OTA_Firmware_Update.h** shows the pattern:

```cpp
API_Process_Type Get_Process_Type() const override {
    return API_Process_Type::RAW;  // ← Bypasses JSON parsing
}

void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
    // Receives raw bytes, processes firmware chunks
    size_t const chunk = Helper::parseRequestId(response_topic, topic);
    m_ota.Process_Firmware_Packet(chunk, payload, length);
}

void Process_Json_Response(char const * topic, JsonDocument const & data) override {
    // Not used - OTA doesn't need parsed JSON
}
```

---

## Proposed Solution: Raw_Shared_Attribute_Update API

Create a new API implementation that:

1. **Returns `API_Process_Type::RAW`** - bypasses full JSON parsing
2. **Subscribes to same topic** as Shared_Attribute_Update (`v1/devices/me/attributes`)
3. **Manually parses only outer JSON** to extract specific attribute key's raw value
4. **Passes raw string to callback** for application-level parsing

### Design Constraints

**Topic Matching:**
- PROBLEM: Both `Shared_Attribute_Update` (JSON) and `Raw_Shared_Attribute_Update` (RAW) need same topic
- SOLUTION: ThingsBoard library processes ALL matching APIs
  - RAW APIs process first (return early if processed)
  - JSON APIs process if no RAW API handled it
  - We can have BOTH APIs active:
    - `Raw_Shared_Attribute_Update` handles specific keys (regCfg)
    - `Shared_Attribute_Update` handles other keys (forceReregister, etc.)

**Implementation Strategy:**

```cpp
bool Is_Response_Topic_Matching(char const * topic) const override {
    // Match "v1/devices/me/attributes"
    return strncmp(ATTRIBUTE_TOPIC, topic, strlen(ATTRIBUTE_TOPIC)) == 0;
}

void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
    // Step 1: Minimal JSON parse - ONLY outer level
    StaticJsonDocument<256> outer_doc;  // Small fixed size for outer structure
    DeserializationError error = deserializeJson(outer_doc, payload, length);
    if (error) return;  // Not valid JSON - let other APIs try
    
    JsonObjectConst root = outer_doc.as<JsonObjectConst>();
    
    // Step 2: Check if any subscribed keys exist
    for (auto const & callback : m_raw_callbacks) {
        char const * key = callback.Get_Attribute_Key();
        
        if (root.containsKey(key)) {
            // Step 3: Extract raw substring for this key's value
            char const * key_start = strstr((char*)payload, key);
            if (key_start == nullptr) continue;
            
            // Find the value after the key (skip "key":")
            char const * value_start = strchr(key_start + strlen(key), ':');
            if (value_start == nullptr) continue;
            value_start++;  // Skip ':'
            
            // Skip whitespace
            while (*value_start == ' ' || *value_start == '\t' || *value_start == '\n') {
                value_start++;
            }
            
            // Find value end (handle objects, arrays, strings, primitives)
            char const * value_end = Find_Value_End(value_start, payload + length);
            size_t value_len = value_end - value_start;
            
            // Step 4: Call user callback with RAW value string
            callback.Call_Callback(key, value_start, value_len);
        }
    }
}
```

### Callback Interface

```cpp
/// @brief Callback for raw shared attribute values (unparsed strings)
class Raw_Attribute_Callback {
public:
    using function = std::function<void(char const * key, char const * value, size_t length)>;
    
    Raw_Attribute_Callback(function callback, char const * attribute_key)
        : m_callback(callback)
        , m_attribute_key(attribute_key)
    {}
    
    void Call_Callback(char const * key, char const * value, size_t length) const {
        if (m_callback) {
            m_callback(key, value, length);
        }
    }
    
    char const * Get_Attribute_Key() const {
        return m_attribute_key;
    }
    
private:
    function m_callback;
    char const * m_attribute_key;
};
```

### User-Side Usage

**Gateway Application Code:**

```cpp
// Global callback for raw regCfg attribute
void onRawRegCfgReceived(char const * key, char const * value, size_t length) {
    SERIALPRINTF("[Raw Attribute] Received %s: %u bytes\n", key, (unsigned)length);
    
    // Now parse with OUR DynamicJsonDocument (4096 bytes)
    extern DynamicJsonDocument jsonWorkspace;
    jsonWorkspace.clear();
    
    DeserializationError error = deserializeJson(jsonWorkspace, value, length);
    if (error) {
        SERIALPRINTF("[Raw Attribute] Parse failed: %s\n", error.c_str());
        return;
    }
    
    // Extract the actual configuration object
    JsonObjectConst regCfg = jsonWorkspace.as<JsonObjectConst>();
    
    // Pass to existing parser
    parseRegCfg(regCfg);
}

// In setup():
Raw_Shared_Attribute_Update<> raw_attr;
tb.Subscribe_API_Implementation(raw_attr);

Raw_Attribute_Callback regCfgCallback(onRawRegCfgReceived, "regCfg");
raw_attr.Raw_Attributes_Subscribe(regCfgCallback);
```

---

## Implementation Plan

### Phase 1: Create Core Classes

**Files to Create:**

1. **`Raw_Attribute_Callback.h`**
   - Callback wrapper for raw attribute values
   - Stores attribute key to match against
   - Similar to `Shared_Attribute_Callback`

2. **`Raw_Shared_Attribute_Update.h`**
   - Main API implementation
   - Inherits from `IAPI_Implementation`
   - Returns `API_Process_Type::RAW`
   - Subscribes to `v1/devices/me/attributes`
   - Manually extracts raw value strings

**Key Functions:**

```cpp
// Parse raw payload to find specific attribute value
char const * Find_Value_End(char const * value_start, char const * payload_end);

// Extract substring for attribute value
bool Extract_Attribute_Value(uint8_t * payload, unsigned int length, 
                             char const * key, 
                             char const ** out_value_start, 
                             size_t * out_value_len);
```

### Phase 2: String Extraction Logic

**Challenges:**
- Attribute value could be: object `{...}`, array `[...]`, string `"..."`, number, boolean
- Must handle nested braces/brackets correctly
- Must handle escaped quotes in strings

**Solution - State Machine:**

```cpp
char const * Find_Value_End(char const * value_start, char const * payload_end) {
    char const * p = value_start;
    char first_char = *p;
    
    // Handle different JSON types
    if (first_char == '{' || first_char == '[') {
        // Object or array - track bracket depth
        char close_char = (first_char == '{') ? '}' : ']';
        int depth = 1;
        p++;
        bool in_string = false;
        
        while (p < payload_end && depth > 0) {
            if (*p == '"' && *(p-1) != '\\') {
                in_string = !in_string;
            }
            else if (!in_string) {
                if (*p == first_char) depth++;
                else if (*p == close_char) depth--;
            }
            p++;
        }
        return p;
    }
    else if (first_char == '"') {
        // String - find closing unescaped quote
        p++;
        while (p < payload_end) {
            if (*p == '"' && *(p-1) != '\\') {
                return p + 1;  // Include closing quote
            }
            p++;
        }
    }
    else {
        // Number, boolean, null - find delimiter (comma, }, ])
        while (p < payload_end && *p != ',' && *p != '}' && *p != ']') {
            p++;
        }
        return p;
    }
    
    return payload_end;  // Fallback
}
```

### Phase 3: Integration with Gateway

**Modify Gateway Code:**

1. Add `Raw_Shared_Attribute_Update` to API implementations array
2. Create callback for `regCfg` key
3. Remove/reduce `MaxResponse` template parameter (no longer needed)
4. Keep existing `Shared_Attribute_Update` for simple attributes

**Before:**
```cpp
Shared_Attribute_Update<1U, MAX_ATTRIBUTES> shared_update;
ThingsBoard tb(..., apis);  // MaxResponse=8 causes failures
```

**After:**
```cpp
Shared_Attribute_Update<1U, MAX_ATTRIBUTES> shared_update;
Raw_Shared_Attribute_Update<1U> raw_attr_update;

const std::array<IAPI_Implementation*, 6U> apis = {
    &prov, &attr_request, &shared_update, &raw_attr_update, &ota, &rpc
};

ThingsBoard tb(..., apis);  // MaxResponse=8 is fine now
```

---

## Benefits

1. ✅ **Solves Nested JSON Problem**: No full parse of complex structures
2. ✅ **Memory Efficient**: Uses small fixed StaticJsonDocument (256 bytes) for outer parse
3. ✅ **Flexible**: Application controls parsing of complex structures
4. ✅ **Backward Compatible**: Existing shared attributes still work normally
5. ✅ **Reusable**: Any attribute with nested JSON can use this
6. ✅ **Clean Architecture**: Follows existing OTA pattern

## Potential Issues & Mitigations

### Issue 1: String Extraction Complexity

**Problem**: Correctly finding value boundaries in JSON is non-trivial

**Mitigation**:
- Use robust state machine for bracket/quote tracking
- Extensive testing with various JSON structures
- Fallback: If extraction fails, return early (other APIs can try)

### Issue 2: Multiple APIs on Same Topic

**Problem**: Both RAW and JSON APIs match `v1/devices/me/attributes`

**Mitigation**:
- Library processes RAW APIs first
- RAW API only handles specific keys (regCfg)
- Other attributes fall through to JSON API
- No conflict if keys are distinct

### Issue 3: Partial Processing

**Problem**: What if RAW API extracts one key but there are others?

**Solution**: DON'T return early after processing
- Let RAW API extract its keys
- Continue to JSON parsing for remaining keys
- Or: Have two strategies:
  - Exclusive: RAW API returns true if it handled ANY key (stops processing)
  - Shared: RAW API always returns false (allows JSON API to also process)

**Proposed Behavior:**
```cpp
void Process_Response(...) override {
    bool handled_any = false;
    
    // Extract raw values for subscribed keys
    for (auto const & callback : m_raw_callbacks) {
        if (Extract_And_Call(callback)) {
            handled_any = true;
        }
    }
    
    // Don't return early - let JSON API handle other keys
    // This allows mixed usage
}
```

---

## Alternative Approaches Considered

### Alternative 1: String-Based Shared Attributes

**Idea**: Force ThingsBoard to send regCfg as escaped JSON string

```json
{
  "regCfg": "{\"rtu\":{\"baud\":9600,...}}"
}
```

**Pros**: 
- Existing Shared_Attribute_Update works
- Single string value, no nested parsing

**Cons**:
- ❌ Requires double-escaping when setting attribute
- ❌ Not human-readable in ThingsBoard UI
- ❌ Error-prone (forget to escape)
- ❌ Non-standard approach

### Alternative 2: Split Configuration Into Multiple Attributes

**Idea**: Break regCfg into smaller attributes

```json
{
  "regCfg_rtu_baud": 9600,
  "regCfg_rtu_device_1": {...},
  "regCfg_tcp_target_1": {...}
}
```

**Pros**:
- Each attribute is simpler

**Cons**:
- ❌ Requires many attribute updates (synchronization issues)
- ❌ Loss of atomic configuration updates
- ❌ Complex management logic
- ❌ Doesn't match gateway design philosophy

### Alternative 3: Increase MaxResponse Globally

**Idea**: Set `MaxResponse=64` or use `THINGSBOARD_ENABLE_DYNAMIC`

**Pros**:
- Simple configuration change

**Cons**:
- ❌ Wastes memory on simple responses
- ❌ **Still counts ALL nested fields** - can still fail
- ❌ Doesn't solve root cause
- ❌ May hit malicious payload protection

---

## Recommendation

**Implement Raw_Shared_Attribute_Update API** for the following reasons:

1. **Clean Architecture**: Follows existing OTA pattern
2. **Memory Efficient**: Only parses what's needed
3. **Flexible**: Works for any complex nested attribute
4. **Future-Proof**: Handles arbitrarily deep structures
5. **Backward Compatible**: Doesn't break existing code

**Implementation Priority:**
1. Core Raw_Attribute_Callback class
2. Raw_Shared_Attribute_Update with basic extraction
3. Robust string extraction state machine
4. Integration tests with gateway
5. Documentation and examples

**Estimated Effort:** 
- Core implementation: 4-6 hours
- Testing & refinement: 2-3 hours
- Documentation: 1-2 hours
- **Total: ~1 day**

---

## Next Steps

1. **Review Proposal**: Confirm approach with team
2. **Prototype**: Create basic Raw_Shared_Attribute_Update
3. **Test**: Verify with actual regCfg payload
4. **Refine**: Handle edge cases in string extraction
5. **Document**: Add usage examples
6. **Submit PR**: Contribute to thingsboard-client-sdk repo (optional)

The key insight is that **not all data from ThingsBoard needs full JSON parsing**. For complex configuration structures, raw string extraction + application-level parsing is more appropriate and efficient.
