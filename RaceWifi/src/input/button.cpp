#include "button.h"

#include "../config.h"

BtnEvent Button::update(bool rawDown, uint32_t now) {
    if (rawDown != raw_) {
        raw_ = rawDown;
        rawSince_ = now;
    }
    BtnEvent ev = BtnEvent::None;
    if (raw_ != stable_ && now - rawSince_ >= BTN_DEBOUNCE_MS) {
        stable_ = raw_;
        if (stable_) {
            pressMs_ = now;
            armed_ = true;
            vlongFired_ = false;
        } else if (armed_) {
            armed_ = false;
            if (!vlongFired_) ev = (now - pressMs_ < BTN_CLICK_MAX_MS) ? BtnEvent::Click : BtnEvent::Long;
        }
    }
    if (stable_ && armed_ && !vlongFired_ && now - pressMs_ >= BTN_VLONG_MS) {
        vlongFired_ = true;
        ev = BtnEvent::VeryLong;
    }
    return ev;
}
