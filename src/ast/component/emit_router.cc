#include "component.h"

void emit_component_router_methods(std::stringstream &ss, const Component &component)
{
    if (!component.router)
    {
        return;
    }

    // Find default route index (if any)
    int default_route_idx = -1;
    std::string fallback_path = "/";
    for (size_t i = 0; i < component.router->routes.size(); ++i)
    {
        if (component.router->routes[i].is_default)
        {
            default_route_idx = static_cast<int>(i);
        }
        else if (fallback_path == "/" || i == 0)
        {
            fallback_path = component.router->routes[i].path;
        }
    }

    // navigate() method - changes route and updates browser URL
    ss << "    void navigate(const webcc::string& route) {\n";
    ss << "        if (_current_route == route) return;\n";
    ss << "        _current_route = route;\n";
    ss << "        webcc::system::push_state(route);\n";
    ss << "        webcc::dom::scroll_to_top();\n";
    ss << "        _sync_route();\n";
    ss << "    }\n";

    // _handle_popstate() method - called when browser back/forward buttons are clicked
    ss << "    void _handle_popstate(const webcc::string& path) {\n";
    ss << "        if (_current_route == path) return;\n";
    ss << "        _current_route = path;\n";
    // For popstate, we don't need to validate - _sync_route will handle fallback via else
    ss << "        _sync_route();\n";
    ss << "    }\n";

    // _sync_route() method - destroys old component and creates new one
    ss << "    void _sync_route() {\n";
    // First destroy any existing route component
    for (size_t i = 0; i < component.router->routes.size(); ++i)
    {
        ss << "        if (_route_" << i << ") { _route_" << i << "->_destroy(); delete _route_" << i << "; _route_" << i << " = nullptr; }\n";
    }

    // Helper lambda to generate component creation code
    auto emit_route_creation = [&](size_t i, const RouteEntry &route)
    {
        ss << "            _route_" << i << " = new " << qualified_name(route.module_name, route.component_name) << "{";
        // Pass arguments - same handling as component construction
        // Reference args (&) that are identifiers are callbacks and need lambda wrapping
        for (size_t j = 0; j < route.args.size(); ++j)
        {
            if (j > 0)
                ss << ", ";
            const auto &arg = route.args[j];

            // Check if this is a callback (reference to a method identifier)
            if (arg.is_reference)
            {
                if (auto *ident = dynamic_cast<Identifier *>(arg.value.get()))
                {
                    // Wrap method reference in a lambda
                    ss << "[this]() { this->" << ident->name << "(); }";
                }
                else
                {
                    // Reference to a variable - pass as pointer
                    ss << "&(" << arg.value->to_webcc() << ")";
                }
            }
            else if (arg.is_move)
            {
                // Move semantics
                ss << "std::move(" << arg.value->to_webcc() << ")";
            }
            else
            {
                // Regular value copy
                ss << arg.value->to_webcc();
            }
        }
        ss << "};\n";
        ss << "            _route_" << i << "->view(_route_parent);\n";
        // Move the routed component's root element before the anchor
        ss << "            webcc::dom::insert_before(_route_parent, _route_" << i << "->_get_root_element(), _route_anchor);\n";
        ss << "            webcc::flush();\n";
    };

    // Create the component for matching route and insert before anchor
    bool first = true;
    for (size_t i = 0; i < component.router->routes.size(); ++i)
    {
        const auto &route = component.router->routes[i];
        if (route.is_default)
            continue; // Handle default route at the end

        ss << "        " << (first ? "if" : "else if") << " (_current_route == \"" << route.path << "\") {\n";
        emit_route_creation(i, route);
        ss << "        }\n";
        first = false;
    }

    // Generate else route (default) if present
    if (default_route_idx >= 0)
    {
        const auto &route = component.router->routes[default_route_idx];
        if (first)
        {
            // Only have default route
            ss << "        {\n";
        }
        else
        {
            ss << "        else {\n";
        }
        emit_route_creation(default_route_idx, route);
        ss << "        }\n";
    }

    ss << "    }\n";
}
