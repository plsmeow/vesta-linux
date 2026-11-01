#include <platform/lnx/input.hpp>
#include <platform/lnx/key_encoding.hpp>

#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <unistd.h>

namespace platform::lnx
{

namespace
{
std::atomic<input_gateway*> g_bound_input{};
} // namespace

void bind_input(input_gateway& gateway) noexcept
{
    g_bound_input.store(&gateway, std::memory_order_release);
}

input_gateway& bound_input() noexcept
{
    static input_gateway disconnected{};
    auto* bound = g_bound_input.load(std::memory_order_acquire);
    return bound ? *bound : disconnected;
}

input_gateway::~input_gateway()
{
    disconnect();
}

void input_gateway::disconnect() noexcept
{
    const auto buttons = m_injected_pointer_down.load(std::memory_order_acquire);
    for (const auto& [down, up] : pointer_buttons)
        if ((buttons & static_cast<std::uint32_t>(down)) != 0)
            pointer(0, 0, up);
    std::array<key_transition, 8> releases{};
    std::size_t release_count{};
    for (std::uint16_t key = 1; key < 256; ++key)
    {
        if (!m_injected_down[key].load(std::memory_order_acquire))
            continue;
        releases[release_count++] = {key, false};
        if (release_count == releases.size())
        {
            keys(std::span{releases}.first(release_count));
            release_count = 0;
        }
    }
    if (release_count)
        keys(std::span{releases}.first(release_count));
    stop_key_gate();
    if (m_uinput_fd >= 0)
    {
        ::ioctl(m_uinput_fd, UI_DEV_DESTROY);
        ::close(m_uinput_fd);
        m_uinput_fd = -1;
    }
    m_connected.store(false, std::memory_order_release);
}

bool input_gateway::connect() noexcept
{
    m_uinput_fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (m_uinput_fd < 0)
        return false;

    ::ioctl(m_uinput_fd, UI_SET_EVBIT, EV_KEY);
    ::ioctl(m_uinput_fd, UI_SET_EVBIT, EV_REL);
    ::ioctl(m_uinput_fd, UI_SET_RELBIT, REL_X);
    ::ioctl(m_uinput_fd, UI_SET_RELBIT, REL_Y);
    for (int key = BTN_LEFT; key <= BTN_TASK; ++key)
        ::ioctl(m_uinput_fd, UI_SET_KEYBIT, key);
    for (int key = KEY_RESERVED; key < KEY_MAX; ++key)
        ::ioctl(m_uinput_fd, UI_SET_KEYBIT, key);

    struct uinput_setup setup{};
    std::snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "vesta-input");
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    ::ioctl(m_uinput_fd, UI_DEV_SETUP, &setup);

    if (::ioctl(m_uinput_fd, UI_DEV_CREATE) < 0)
    {
        ::close(m_uinput_fd);
        m_uinput_fd = -1;
        return false;
    }

    ::usleep(200000);
    m_connected.store(true, std::memory_order_release);
    return true;
}

void input_gateway::emit_event(std::uint16_t type, std::uint16_t code, std::int32_t value) const noexcept
{
    if (m_uinput_fd < 0)
        return;
    struct input_event event{};
    event.type = type;
    event.code = code;
    event.value = value;
    ::write(m_uinput_fd, &event, sizeof(event));
}

void input_gateway::emit_sync() const noexcept
{
    emit_event(EV_SYN, SYN_REPORT, 0);
}

bool input_gateway::walker_pointer(const int dx, const int dy) const noexcept
{
    s_walker_pointer = true;
    const auto sent = pointer(dx, dy, pointer_action::relative_move);
    s_walker_pointer = false;
    return sent;
}

bool input_gateway::pointer(int dx, int dy, pointer_action actions) const noexcept
{
    if (!connected())
        return false;

    if (dx || dy)
    {
        emit_event(EV_REL, REL_X, dx);
        emit_event(EV_REL, REL_Y, dy);
    }

    auto button_event = [&](std::uint16_t btn, bool down)
    {
        emit_event(EV_KEY, btn, down ? 1 : 0);
    };

    if (contains(actions, pointer_action::primary_down))
        button_event(BTN_LEFT, true);
    if (contains(actions, pointer_action::primary_up))
        button_event(BTN_LEFT, false);
    if (contains(actions, pointer_action::secondary_down))
        button_event(BTN_RIGHT, true);
    if (contains(actions, pointer_action::secondary_up))
        button_event(BTN_RIGHT, false);
    if (contains(actions, pointer_action::middle_down))
        button_event(BTN_MIDDLE, true);
    if (contains(actions, pointer_action::middle_up))
        button_event(BTN_MIDDLE, false);
    if (contains(actions, pointer_action::auxiliary1_down))
        button_event(BTN_SIDE, true);
    if (contains(actions, pointer_action::auxiliary1_up))
        button_event(BTN_SIDE, false);
    if (contains(actions, pointer_action::auxiliary2_down))
        button_event(BTN_EXTRA, true);
    if (contains(actions, pointer_action::auxiliary2_up))
        button_event(BTN_EXTRA, false);

    emit_sync();

    if (contains(actions, pointer_action::relative_move) && (dx || dy) && !s_walker_pointer)
        m_foreign_pointer_ticks.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                                      std::memory_order_release);
    if ((static_cast<std::uint32_t>(actions) & pointer_button_action_mask) != 0)
    {
        auto previous = m_injected_pointer_down.load(std::memory_order_relaxed);
        while (!m_injected_pointer_down.compare_exchange_weak(previous, pointer_owned_after(previous, actions),
                                                              std::memory_order_release, std::memory_order_relaxed))
        {
        }
    }
    return true;
}

bool input_gateway::key(std::uint16_t virtual_key, bool pressed) const noexcept
{
    const std::array transitions{key_transition{virtual_key, pressed}};
    return keys(transitions);
}

bool input_gateway::keys(std::span<const key_transition> transitions) const noexcept
{
    if (!connected() || transitions.empty() || transitions.size() > 8)
        return false;

    for (const auto& transition : transitions)
    {
        const auto linux_key = vk_to_linux_key(transition.virtual_key);
        if (!linux_key)
            return false;
        emit_event(EV_KEY, linux_key, transition.pressed ? 1 : 0);
    }
    emit_sync();

    for (const auto& transition : transitions)
    {
        if (transition.virtual_key < m_injected_down.size())
            m_injected_down[transition.virtual_key].store(transition.pressed, std::memory_order_release);
    }
    return true;
}

void input_gateway::set_key_gate(const std::uint16_t virtual_key, const bool enabled) noexcept
{
    const auto reserved = protected_key(virtual_key);
    const auto next_key = enabled && !reserved && virtual_key > 0 && virtual_key < 256 ? virtual_key : std::uint16_t{};
    const auto next_enabled = next_key != 0;
    const auto previous_key = m_gate_key.exchange(next_key, std::memory_order_acq_rel);
    const auto previous_enabled = m_primary_gate_enabled.exchange(next_enabled, std::memory_order_acq_rel);
    m_gate_enabled.store(next_enabled || m_movement_gate_count.load(std::memory_order_acquire) > 0,
                         std::memory_order_release);
    if (previous_key == next_key && previous_enabled == next_enabled)
        return;
    if (next_enabled && m_gate_physical_down.load(std::memory_order_acquire))
        key(next_key, false);
}

void input_gateway::set_movement_gate(const gate_owner owner, std::span<const std::uint16_t> virtual_keys,
                                      const bool enabled) noexcept
{
    std::scoped_lock lock(m_gate_owner_mutex);
    auto& mine = m_owner_keys[static_cast<std::size_t>(owner)];
    mine.fill(false);
    if (enabled)
        for (const auto key : virtual_keys)
            if (key > 0 && key < 256 && !protected_key(key))
                mine[key] = true;
    std::array<bool, 256> desired{};
    for (const auto& keys : m_owner_keys)
        for (std::size_t key = 1; key < 256; ++key)
            desired[key] = desired[key] || keys[key];
    apply_movement_gate(desired);
}

void input_gateway::clear_movement_gates() noexcept
{
    std::scoped_lock lock(m_gate_owner_mutex);
    for (auto& keys : m_owner_keys)
        keys.fill(false);
    std::array<bool, 256> desired{};
    apply_movement_gate(desired);
}

void input_gateway::apply_movement_gate(const std::array<bool, 256>& desired) noexcept
{
    std::uint16_t count{};
    for (std::size_t key = 1; key < 256; ++key)
    {
        const bool was = m_movement_gated[key].load(std::memory_order_relaxed);
        if (desired[key] != was)
        {
            m_movement_gated[key].store(desired[key], std::memory_order_release);
            if (!desired[key] && m_movement_physical_down[key].load(std::memory_order_acquire))
                this->key(static_cast<std::uint16_t>(key), true);
        }
        if (desired[key])
            ++count;
    }
    m_movement_gate_count.store(count, std::memory_order_release);
}

bool input_gateway::physical_key_down(std::uint16_t virtual_key) const noexcept
{
    (void)virtual_key;
    return false;
}

void input_gateway::stop_key_gate() noexcept
{
}

} // namespace platform::lnx
