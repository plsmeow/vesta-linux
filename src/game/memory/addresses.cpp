#include <stdafx.hpp>
#include <game/memory/addresses.hpp>
#include <game/memory/memory_services.hpp>
#include <game/body/body.hpp>

#if defined(__linux__) && defined(VESTA_CS2_DUMP_AVAILABLE)
#include <cs2/offsets.hpp>
#endif

bool game::address_catalog::initialize(const memory_services& services)
{
    const auto& process = services.process;
    const auto client = services.modules.client;
    const auto image_size = process.module_image_size(client);
    if (!client || !image_size)
        return false;

#if defined(__linux__) && defined(VESTA_CS2_DUMP_AVAILABLE)
    const auto resolve = [&](const std::string_view name, const std::uintptr_t offset) -> std::uintptr_t
    {
        const auto target = client + offset;
        if (offset >= image_size || target < client || target >= client + image_size)
        {
            services.log.warning("[offsets] {} is outside libclient.so.", name);
            return 0;
        }
        std::byte probe{};
        if (!process.copy(target, &probe, sizeof(probe)))
            return 0;
        return target;
    };
    csgo_input = resolve("dwCSGOInput", cs2_dumper::offsets::libclient_so::dwCSGOInput);
    entity_list = resolve("dwEntityList", cs2_dumper::offsets::libclient_so::dwEntityList);
    local_player_controller = resolve("dwLocalPlayerController", cs2_dumper::offsets::libclient_so::dwLocalPlayerController);
    global_vars = resolve("dwGlobalVars", cs2_dumper::offsets::libclient_so::dwGlobalVars);
    view_matrix = resolve("dwViewMatrix", cs2_dumper::offsets::libclient_so::dwViewMatrix);
    auto_accept = 0;
#else
    const auto resolve = [&](const std::string_view name, const std::string_view pattern) -> std::uintptr_t
    {
        const auto match = process.scan_code_signature(client, pattern);
        const auto target = match ? process.decode_rip(match) : 0;
        if (!target || target < client || target >= client + image_size)
        {
            services.log.warning("[offsets] {} pattern is outdated or resolved outside client.dll.", name);
            return 0;
        }
        std::byte probe{};
        if (!process.copy(target, &probe, sizeof(probe)))
        {
            services.log.warning("[offsets] {} resolved to unreadable memory at client.dll+{:#x}.", name,
                                 target - client);
            return 0;
        }
        return target;
    };
    csgo_input = resolve("dwCSGOInput", "48 89 05 ? ? ? ? 0F 57 C0 0F 11 05");
    entity_list = resolve("dwEntityList", "48 89 0D ? ? ? ? E9 ? ? ? ? CC");
    local_player_controller = resolve("dwLocalPlayerController", "48 8B 05 ? ? ? ? 41 89 BE");
    global_vars = resolve("dwGlobalVars", "48 89 15 ? ? ? ? 48 89 42");
    view_matrix = resolve("dwViewMatrix", "48 8D 0D ? ? ? ? 48 C1 E0 06");
    if (const auto match = process.scan_code_signature(client, "48 89 05 ? ? ? ? E8 ? ? ? ? 48 85 DB"))
    {
        const auto target = process.decode_rip(match);
        if (target >= client && target < client + image_size)
            auto_accept = target;
    }
#endif
    const auto complete = entity_list && local_player_controller && global_vars && view_matrix;
    if (complete)
        services.log.success("[offsets] live client globals resolved.");
    return complete;
}
