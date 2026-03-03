#ifndef Raw_Attribute_Callback_h
#define Raw_Attribute_Callback_h

// Local includes.
#include "Callback.h"
#include "Helper.h"


/// @brief Raw attribute callback wrapper,
/// contains the needed configuration settings to create the request that should be sent to the server.
/// Documentation about the specific use of the callback can be found in Raw_Shared_Attribute_Update
class Raw_Attribute_Callback {
  public:
    /// @brief Constructs empty callback, will result in never being called
    Raw_Attribute_Callback()
      : Raw_Attribute_Callback(nullptr, nullptr, false)
    {
        // Nothing to do
    }

    /// @brief Constructs callback that will be called for a specific attribute key (exact match), receiving the raw value string without full JSON parsing
    /// @param callback Callback method that will be called with the attribute key, raw value pointer, and value length
    /// @param attribute_key Specific attribute key to match against (e.g., "regCfg") - exact match only
    Raw_Attribute_Callback(Callback<void, char const *, char const *, size_t>::function callback, char const * attribute_key)
      : Raw_Attribute_Callback(callback, attribute_key, false)
    {
        // Nothing to do
    }

    /// @brief Constructs callback that will be called for attribute key or prefix matching, receiving the raw value string without full JSON parsing
    /// @param callback Callback method that will be called with the attribute key, raw value pointer, and value length
    /// @param attribute_key_or_prefix Attribute key (exact match) or prefix string to match against
    /// @param is_prefix If true, matches all attributes starting with this prefix (e.g., "DEVICE_" matches "DEVICE_inverter", "DEVICE_battery", etc.)
    ///                  If false, matches exact attribute name only (default behavior)
    Raw_Attribute_Callback(Callback<void, char const *, char const *, size_t>::function callback, char const * attribute_key_or_prefix, bool is_prefix)
      : m_callback(callback)
      , m_attribute_key(attribute_key_or_prefix)
      , m_is_prefix(is_prefix)
    {
        // Nothing to do
    }

    /// @brief Internal method that calls the callback, if the given value is meant to be received by this callback instance,
    /// Passes raw attribute value string to the user callback without full JSON parsing
    /// @param key Attribute key name
    /// @param value Pointer to start of attribute value in raw payload (not null-terminated)
    /// @param length Length of the attribute value in bytes
    void Call_Callback(char const * key, char const * value, size_t length) const {
        m_callback.Call_Callback(key, value, length);
    }

    /// @brief Gets the immutable attribute key that this callback is meant to be called for
    /// @return Immutable pointer to the internal attribute key
    char const * Get_Attribute_Key() const {
        return m_attribute_key;
    }

    /// @brief Checks if this callback should handle the given attribute key (exact or prefix match)
    /// @param key Attribute key to test
    /// @return true if this callback matches this key (exact match or prefix match depending on is_prefix flag)
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
    Callback<void, char const *, char const *, size_t> m_callback;      // Callback to call
    char const *                                       m_attribute_key;  // Attribute key or prefix to match
    bool                                               m_is_prefix;      // Whether to use prefix matching
};

#endif // Raw_Attribute_Callback_h
