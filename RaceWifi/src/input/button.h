// Antirrebote y gestos del boton BOOT: corta, larga (al soltar) y muy larga (mientras
// se mantiene). Recibe el nivel crudo, asi que es portable.
#pragma once
#include <stdint.h>

enum class BtnEvent : uint8_t { None, Click, Long, VeryLong };

class Button {
public:
    BtnEvent update(bool rawDown, uint32_t now);
    bool down() const { return stable_; }
    uint32_t heldMs(uint32_t now) const { return (stable_ && armed_) ? now - pressMs_ : 0; }

private:
    bool raw_ = false;
    bool stable_ = false;
    bool armed_ = false;       // la pulsacion empezo despues del arranque
    bool vlongFired_ = false;
    uint32_t rawSince_ = 0;
    uint32_t pressMs_ = 0;
};
