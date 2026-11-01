#include <stdafx.hpp>
#include <platform/windows/process.hpp>
#include <game/memory/modules.hpp>

namespace game
{

bool module_catalog::discover(const platform::windows::process_session& process)
{
    const auto lookup = [&process](std::string_view name) { return process.module_base(name); };

#if defined(__linux__)
    client = lookup("libclient.so");
    engine = lookup("libengine2.so");
    input_system = lookup("libinputsystem.so");
    tier = lookup("libtier0.so");
    schema = lookup("libschemasystem.so");
    physics = lookup("libvphysics2.so");
    panorama = lookup("libpanorama.so");
#else
    client = lookup("client.dll");
    engine = lookup("engine2.dll");
    input_system = lookup("inputsystem.dll");
    tier = lookup("tier0.dll");
    schema = lookup("schemasystem.dll");
    physics = lookup("vphysics2.dll");
    panorama = lookup("panorama.dll");
#endif

    return client && engine && input_system && tier && schema && physics;
}


} // namespace game
