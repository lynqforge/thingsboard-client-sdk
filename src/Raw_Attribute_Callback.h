#ifndef Raw_Attribute_Callback_h
#define Raw_Attribute_Callback_h

// Local includes.
#include "Callback.h"


/// @brief Raw attribute callback wrapper,
/// contains the needed configuration settings to create the request that should be sent to the server.
/// Documentation about the specific use of the callback can be found in Raw_Shared_Attribute_Update
class Raw_Attribute_Callback {
  public:
    /// @brief Constructs empty callback, will result in never being called
    Raw_Attribute_Callback()
      : Raw_Attribute_Callback(nullptr, nullptr)
    {
        // Nothing to do
    }

    /// @brief Constructs callback that will be called for a specific attribute key, receiving the raw value string without full JSON parsing
    /// @param callback Callback method that will be called with the attribute key, raw value pointer, and value length
    /// @param attribute_key Specific attribute key to match against (e.g., "regCfg")
    Raw_Attribute_Callback(Callback<void, char const *, char const *, size_t>::function callback, char const * attribute_key)
      : m_callback(callback)
      , m_attribute_key(attribute_key)
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

  private:
    Callback<void, char const *, char const *, size_t> m_callback;   // Callback to call
    char const *                                       m_attribute_key; // Attribute key to match
};

#endif // Raw_Attribute_Callback_h
