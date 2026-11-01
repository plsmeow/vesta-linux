#include <platform/lnx/activation.hpp>
#include <platform/lnx/key_encoding.hpp>

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

namespace platform::lnx
{
bool key_activation::physical_down(int key) noexcept
{
    const auto linux_key = vk_to_linux_key(static_cast<std::uint16_t>(key));
    if (!linux_key)
        return false;

    static int fd = -1;
    if (fd < 0)
    {
        fd = ::open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
        if (fd < 0)
        {
            for (int i = 1; i < 20; ++i)
            {
                char path[64];
                std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
                fd = ::open(path, O_RDONLY | O_NONBLOCK);
                if (fd >= 0)
                    break;
            }
        }
        if (fd < 0)
            return false;
    }

    std::uint8_t bits[(KEY_MAX + 7) / 8]{};
    if (::ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0)
        return false;
    return (bits[linux_key / 8] >> (linux_key % 8)) & 1;
}
} // namespace platform::lnx
