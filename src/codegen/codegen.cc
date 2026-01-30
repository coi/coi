#include "codegen.h"
#include "ast/ast.h"
#include "../analysis/feature_detector.h"
#include "../analysis/dependency_resolver.h"
#include "json_codegen.h"
#include <iostream>

void generate_cpp_code(
    std::ostream &out,
    std::vector<Component> &all_components,
    const std::vector<std::unique_ptr<DataDef>> &all_global_data,
    const std::vector<std::unique_ptr<EnumDef>> &all_global_enums,
    const AppConfig &final_app_config,
    const std::set<std::string> &required_headers,
    const FeatureFlags &features)
{
    // Include required headers
    for (const auto &header : required_headers)
    {
        out << "#include \"webcc/" << header << ".h\"\n";
    }
    out << "#include \"webcc/core/function.h\"\n";
    out << "#include \"webcc/core/allocator.h\"\n";
    out << "#include \"webcc/core/new.h\"\n";
    out << "#include \"webcc/core/array.h\"\n";
    out << "#include \"webcc/core/vector.h\"\n";
    out << "#include \"webcc/core/random.h\"\n";

    // Sort components topologically so dependencies come first
    auto sorted_components = topological_sort_components(all_components);

    // Emit JSON runtime helpers inline if Json.parse is used
    if (features.json)
    {
        emit_json_runtime(out);
    }
    out << "\n";

    // Register all data types in the DataTypeRegistry for JSON codegen
    // Component-local types are prefixed with ComponentName_
    DataTypeRegistry::instance().clear();
    for (const auto &data_def : all_global_data)
    {
        DataTypeRegistry::instance().register_type(data_def->name, data_def->fields);
    }
    for (const auto &comp : all_components)
    {
        for (const auto &data_def : comp.data)
        {
            // Prefix component-local data types
            DataTypeRegistry::instance().register_type(comp.name + "_" + data_def->name, data_def->fields);
        }
    }

    // Populate global set of components with scoped CSS (for view.cc to conditionally emit scope attributes)
    extern std::set<std::string> g_components_with_scoped_css;
    g_components_with_scoped_css.clear();
    for (const auto &comp : all_components)
    {
        if (!comp.css.empty())
        {
            g_components_with_scoped_css.insert(comp.name);
        }
    }

    // Generic event dispatcher template (only if needed)
    if (needs_dispatcher(features))
    {
        out << "template<typename Callback, int MaxListeners = 64>\n";
        out << "struct Dispatcher {\n";
        out << "    int32_t handles[MaxListeners];\n";
        out << "    Callback callbacks[MaxListeners];\n";
        out << "    int count = 0;\n";
        out << "    void set(webcc::handle h, Callback cb) {\n";
        out << "        int32_t hid = (int32_t)h;\n";
        out << "        for (int i = 0; i < count; i++) {\n";
        out << "            if (handles[i] == hid) { callbacks[i] = cb; return; }\n";
        out << "        }\n";
        out << "        if (count < MaxListeners) {\n";
        out << "            handles[count] = hid;\n";
        out << "            callbacks[count] = cb;\n";
        out << "            count++;\n";
        out << "        }\n";
        out << "    }\n";
        out << "    void remove(webcc::handle h) {\n";
        out << "        int32_t hid = (int32_t)h;\n";
        out << "        for (int i = 0; i < count; i++) {\n";
        out << "            if (handles[i] == hid) {\n";
        out << "                handles[i] = handles[count-1];\n";
        out << "                callbacks[i] = callbacks[count-1];\n";
        out << "                count--;\n";
        out << "                return;\n";
        out << "            }\n";
        out << "        }\n";
        out << "    }\n";
        out << "    template<typename... Args>\n";
        out << "    bool dispatch(webcc::handle h, Args&&... args) {\n";
        out << "        int32_t hid = (int32_t)h;\n";
        out << "        for (int i = 0; i < count; i++) {\n";
        out << "            if (handles[i] == hid) { callbacks[i](args...); return true; }\n";
        out << "        }\n";
        out << "        return false;\n";
        out << "    }\n";
        out << "};\n\n";
    }

    out << "int g_view_depth = 0;\n";

    // Emit feature-specific globals (dispatchers, callbacks, etc.)
    emit_feature_globals(out, features);
    out << "\n";

    // Create compiler session for cross-component state
    CompilerSession session;

    // Populate component info for parent-child reactivity wiring
    for (auto *comp : sorted_components)
    {
        ComponentMemberInfo info;
        for (const auto &param : comp->params)
        {
            if (param->is_public && param->is_mutable)
            {
                info.pub_mut_members.insert(param->name);
            }
        }
        session.component_info[comp->name] = info;
    }

    // Output global enums (defined outside components)
    for (const auto &enum_def : all_global_enums)
    {
        out << enum_def->to_webcc();
    }
    if (!all_global_enums.empty())
    {
        out << "\n";
    }

    // Output component-local enums (flattened with ComponentName_ prefix)
    for (const auto &comp : all_components)
    {
        for (const auto &enum_def : comp.enums)
        {
            out << "enum struct " << comp.name << "_" << enum_def->name << " : ";
            size_t total_values = enum_def->values.size() + 1;
            if (total_values <= 256) out << "uint8_t";
            else if (total_values <= 65536) out << "uint16_t";
            else out << "uint32_t";
            out << " {\n";
            for (const auto &val : enum_def->values)
            {
                out << "    " << val << ",\n";
            }
            out << "    _COUNT\n};\n";
        }
    }

    // Output global data types (defined outside components)
    for (const auto &data_def : all_global_data)
    {
        out << data_def->to_webcc();
    }
    if (!all_global_data.empty())
    {
        out << "\n";
    }

    // Output component-local data types (flattened with ComponentName_ prefix)
    for (const auto &comp : all_components)
    {
        // Set up ComponentTypeContext so convert_type can resolve nested local types
        std::set<std::string> local_data_names;
        std::set<std::string> local_enum_names;
        for (const auto &d : comp.data)
        {
            local_data_names.insert(d->name);
        }
        for (const auto &e : comp.enums)
        {
            local_enum_names.insert(e->name);
        }
        ComponentTypeContext::instance().set(comp.name, local_data_names, local_enum_names);

        for (const auto &data_def : comp.data)
        {
            out << "struct " << comp.name << "_" << data_def->name << " {\n";
            for (const auto &field : data_def->fields)
            {
                out << "    " << convert_type(field.type) << " " << field.name << ";\n";
            }
            out << "};\n";
        }

        ComponentTypeContext::instance().clear();
    }
    out << "\n";

    // Output Meta structs for JSON parsing (if Json.parse is used)
    if (features.json)
    {
        for (const auto &data_def : all_global_data)
        {
            out << generate_meta_struct(data_def->name);
        }
        for (const auto &comp : all_components)
        {
            for (const auto &data_def : comp.data)
            {
                // Use prefixed name for component-local types
                out << generate_meta_struct(comp.name + "_" + data_def->name);
            }
        }
        out << "\n";
    }

    // Forward declarations
    for (auto *comp : sorted_components)
    {
        out << "struct " << comp->name << ";\n";
    }
    out << "\n";

    // Forward declare global navigation functions (defined after components)
    out << "void g_app_navigate(const webcc::string& route);\n";
    out << "webcc::string g_app_get_route();\n\n";

    for (auto *comp : sorted_components)
    {
        out << comp->to_webcc(session);
    }

    if (final_app_config.root_component.empty())
    {
        std::cerr << "Error: No root component defined. Use 'app { root = ComponentName }' to define the entry point." << std::endl;
        exit(1);
    }

    out << "\n"
        << final_app_config.root_component << "* app = nullptr;\n";

    if (features.router)
    {
        out << "void g_app_navigate(const webcc::string& route) { if (app) app->navigate(route); }\n";
        out << "webcc::string g_app_get_route() { return app ? app->_current_route : \"\"; }\n";
    }
    else
    {
        // Stub functions if no router - prevents linker errors
        out << "void g_app_navigate(const webcc::string& route) {}\n";
        out << "webcc::string g_app_get_route() { return \"\"; }\n";
    }

    out << "void dispatch_events(const webcc::Event* events, uint32_t event_count) {\n";
    out << "    for (uint32_t i = 0; i < event_count; i++) {\n";
    out << "        const auto& e = events[i];\n";
    out << "        if (false) {\n"; // Dummy to allow all handlers to use "} else if"
    emit_feature_event_handlers(out, features);
    out << "        }\n";
    out << "    }\n";
    out << "}\n\n";

    out << "void update_wrapper(double time) {\n";
    out << "    static double last_time = 0;\n";
    out << "    double dt = (time - last_time) / 1000.0;\n";
    out << "    last_time = time;\n";
    out << "    if (dt > 0.1) dt = 0.1; // Cap dt to avoid huge jumps\n";
    out << "    static webcc::Event events[64];\n";
    out << "    uint32_t count = 0;\n";
    out << "    webcc::Event e;\n";
    out << "    while (webcc::poll_event(e) && count < 64) {\n";
    out << "        events[count++] = e;\n";
    out << "    }\n";
    out << "    dispatch_events(events, count);\n";
    
    // Only call tick if the root component has a tick method
    if (session.components_with_tick.count(final_app_config.root_component))
    {
        out << "    if (app) app->tick(dt);\n";
    }
    out << "    webcc::flush();\n";
    out << "}\n\n";

    out << "int main() {\n";
    out << "    // We allocate the app on the heap because the stack is destroyed when main() returns.\n";
    out << "    // The app needs to persist for the event loop (update_wrapper).\n";
    out << "    // We use webcc::malloc to ensure memory is tracked by the framework.\n";
    out << "    void* app_mem = webcc::malloc(sizeof(" << final_app_config.root_component << "));\n";
    out << "    app = new (app_mem) " << final_app_config.root_component << "();\n";
    emit_feature_init(out, features, final_app_config.root_component);
    out << "    app->view();\n";
    out << "    webcc::system::set_main_loop(update_wrapper);\n";
    out << "    webcc::flush();\n";
    out << "    return 0;\n";
    out << "}\n";
}
