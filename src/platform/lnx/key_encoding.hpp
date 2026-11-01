#pragma once
#include <cstdint>
#include <linux/input-event-codes.h>

namespace platform::lnx
{
[[nodiscard]] inline std::uint16_t vk_to_linux_key(std::uint16_t vk) noexcept
{
    if (vk >= 'A' && vk <= 'Z')
        return KEY_A + (vk - 'A');
    if (vk >= '0' && vk <= '9')
        return KEY_0 + (vk - '0');
    switch (vk)
    {
        case 0x09: return KEY_TAB;
        case 0x0D: return KEY_ENTER;
        case 0x1B: return KEY_ESC;
        case 0x20: return KEY_SPACE;
        case 0x21: return KEY_PAGEUP;
        case 0x22: return KEY_PAGEDOWN;
        case 0x23: return KEY_END;
        case 0x24: return KEY_HOME;
        case 0x25: return KEY_LEFT;
        case 0x26: return KEY_UP;
        case 0x27: return KEY_RIGHT;
        case 0x28: return KEY_DOWN;
        case 0x2C: return KEY_SYSRQ;
        case 0x2D: return KEY_INSERT;
        case 0x2E: return KEY_DELETE;
        case 0x30: return KEY_0;
        case 0x31: return KEY_1;
        case 0x32: return KEY_2;
        case 0x33: return KEY_3;
        case 0x34: return KEY_4;
        case 0x35: return KEY_5;
        case 0x36: return KEY_6;
        case 0x37: return KEY_7;
        case 0x38: return KEY_8;
        case 0x39: return KEY_9;
        case 0x5B: return KEY_LEFTMETA;
        case 0x5C: return KEY_RIGHTMETA;
        case 0x5D: return KEY_COMPOSE;
        case 0x60: return KEY_KP0;
        case 0x61: return KEY_KP1;
        case 0x62: return KEY_KP2;
        case 0x63: return KEY_KP3;
        case 0x64: return KEY_KP4;
        case 0x65: return KEY_KP5;
        case 0x66: return KEY_KP6;
        case 0x67: return KEY_KP7;
        case 0x68: return KEY_KP8;
        case 0x69: return KEY_KP9;
        case 0x6A: return KEY_KPASTERISK;
        case 0x6B: return KEY_KPPLUS;
        case 0x6D: return KEY_KPMINUS;
        case 0x6E: return KEY_KPDOT;
        case 0x6F: return KEY_KPSLASH;
        case 0x70: return KEY_F1;
        case 0x71: return KEY_F2;
        case 0x72: return KEY_F3;
        case 0x73: return KEY_F4;
        case 0x74: return KEY_F5;
        case 0x75: return KEY_F6;
        case 0x76: return KEY_F7;
        case 0x77: return KEY_F8;
        case 0x78: return KEY_F9;
        case 0x79: return KEY_F10;
        case 0x7A: return KEY_F11;
        case 0x7B: return KEY_F12;
        case 0x90: return KEY_NUMLOCK;
        case 0x91: return KEY_SCROLLLOCK;
        case 0xA0: return KEY_LEFTSHIFT;
        case 0xA1: return KEY_RIGHTSHIFT;
        case 0xA2: return KEY_LEFTCTRL;
        case 0xA3: return KEY_RIGHTCTRL;
        case 0xA4: return KEY_LEFTALT;
        case 0xA5: return KEY_RIGHTALT;
        case 0xBA: return KEY_SEMICOLON;
        case 0xBB: return KEY_EQUAL;
        case 0xBC: return KEY_COMMA;
        case 0xBD: return KEY_MINUS;
        case 0xBE: return KEY_DOT;
        case 0xBF: return KEY_SLASH;
        case 0xC0: return KEY_GRAVE;
        case 0xDB: return KEY_LEFTBRACE;
        case 0xDC: return KEY_BACKSLASH;
        case 0xDD: return KEY_RIGHTBRACE;
        case 0xDE: return KEY_APOSTROPHE;
        default: return 0;
    }
}
} // namespace platform::lnx
