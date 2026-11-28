#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "swbfo_info.h"
#include "swbfo_net.h"
#include "swbfo_store.h"
#include "swbfo_terrain.h"

namespace {

void initialize_swbfo(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(swbfo::godot_bindings::SwbfoInfo);
    GDREGISTER_CLASS(swbfo::godot_bindings::SwbfoClient);
    GDREGISTER_CLASS(swbfo::godot_bindings::SwbfoServer);
    GDREGISTER_CLASS(swbfo::godot_bindings::SwbfoCharacterStore);
    GDREGISTER_CLASS(swbfo::godot_bindings::SwbfoTerrain);
}

void uninitialize_swbfo(godot::ModuleInitializationLevel) {}

} // namespace

extern "C" {

// Entry point named in game/swbfo.gdextension.
GDExtensionBool GDE_EXPORT swbfo_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                            GDExtensionClassLibraryPtr library,
                                            GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_swbfo);
    init.register_terminator(uninitialize_swbfo);
    init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}

}
