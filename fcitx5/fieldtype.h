#pragma once

#include <fcitx-utils/capabilityflags.h>

namespace floatkey {

// The kind of text field that has focus, from the hints the app gave fcitx5,
// named the way `floatkey focus` takes it.
inline const char *fieldType(fcitx::CapabilityFlags caps)
{
    using fcitx::CapabilityFlag;
    if (caps.testAny(fcitx::CapabilityFlags{CapabilityFlag::Password, CapabilityFlag::Sensitive}))
        return "password";
    if (caps.test(CapabilityFlag::Terminal))
        return "terminal";
    if (caps.test(CapabilityFlag::Url))
        return "url";
    if (caps.test(CapabilityFlag::Email))
        return "email";
    if (caps.testAny(fcitx::CapabilityFlags{CapabilityFlag::Number, CapabilityFlag::Digit, CapabilityFlag::Dialable}))
        return "number";
    return "text";
}

// Whether a focused field wants an on-screen keyboard at all. Qt keeps a
// field focused but marks it disabled when input is switched off for it.
inline bool wantsKeyboard(fcitx::CapabilityFlags caps)
{
    using fcitx::CapabilityFlag;
    return !caps.testAny(fcitx::CapabilityFlags{CapabilityFlag::NoOnScreenKeyboard, CapabilityFlag::Disable});
}

}
