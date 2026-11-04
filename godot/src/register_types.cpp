#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "swm_info.h"
#include "swm_net.h"

namespace {

void initialize_swm(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(swm::godot_bindings::SwmInfo);
    GDREGISTER_CLASS(swm::godot_bindings::SwmClient);
    GDREGISTER_CLASS(swm::godot_bindings::SwmServer);
}

void uninitialize_swm(godot::ModuleInitializationLevel) {}

} // namespace

extern "C" {

// Entry point named in game/swm.gdextension.
GDExtensionBool GDE_EXPORT swm_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                            GDExtensionClassLibraryPtr library,
                                            GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_swm);
    init.register_terminator(uninitialize_swm);
    init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}

}
