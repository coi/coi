#include "component.h"

void emit_component_router_methods(std::stringstream &ss, const Component &component)
{
    if (!component.router)
    {
        return;
    }

    // navigate() method - changes route and updates browser URL.
    // The actual route swap is DEFERRED to _apply_route() (called from
    // update_wrapper at a safe point): navigate() is typically invoked from a
    // method of the route component that is about to be destroyed, and a
    // synchronous _sync_route() would delete that component while its method
    // is still on the stack. The generated epilogue (_update_*/_sync_if_*)
    // would then run on freed memory — use-after-free, DOM handle garbage,
    // and eventually OOB crashes (timing/data dependent).
    ss << "    void navigate(const coi::string& route) {\n";
    ss << "        if (_current_route == route) return;\n";
    ss << "        _current_route = route;\n";
    ss << "        webcc::system::push_state(route);\n";
    ss << "        webcc::dom::scroll_to_top();\n";
    ss << "        _route_dirty = true;\n";
    ss << "    }\n";

    // _handle_popstate() method - called when browser back/forward buttons are clicked
    ss << "    void _handle_popstate(const coi::string& path) {\n";
    ss << "        if (_current_route == path) return;\n";
    ss << "        _current_route = path;\n";
    // For popstate, we don't need to validate - _sync_route will handle fallback via else
    ss << "        _route_dirty = true;\n";
    ss << "    }\n";

    // _apply_route() - performs a pending route swap. Called from
    // update_wrapper after all events/ticks have been dispatched, so no
    // route component method is on the stack when components are destroyed.
    ss << "    void _apply_route() {\n";
    ss << "        if (!_route_dirty) return;\n";
    ss << "        _route_dirty = false;\n";
    ss << "        _sync_route();\n";
    ss << "    }\n";

    // _sync_route() method - destroys old component and creates the matching one
    ss << "    void _sync_route() {\n";
    // First destroy any existing route component
    for (size_t i = 0; i < component.router->routes.size(); ++i)
    {
        ss << "        if (_route_" << i << ") { _route_" << i << "->_destroy(); delete _route_" << i << "; _route_" << i << " = nullptr; }\n";
    }

    // Emit the target component's constructor arguments, filling each param from
    // either a captured path param (_rp_<i>_<k>) or an explicit route argument.
    auto emit_ctor_args = [&](size_t i, const RouteEntry &route)
    {
        for (size_t s = 0; s < route.ctor_order.size(); ++s)
        {
            if (s > 0)
                ss << ", ";
            const auto &slot = route.ctor_order[s];
            if (slot.is_path_param)
            {
                ss << "_rp_" << i << "_" << slot.index;
                continue;
            }

            // Explicit argument - same handling as component construction.
            const auto &arg = route.args[slot.index];
            if (arg.is_reference)
            {
                if (auto *ident = dynamic_cast<Identifier *>(arg.value.get()))
                {
                    bool is_method_ref = false;
                    for (const auto &method : component.methods)
                    {
                        if (method.name == ident->name)
                        {
                            is_method_ref = true;
                            break;
                        }
                    }
                    if (is_method_ref)
                        ss << "[this]() { this->" << ident->name << "(); }";
                    else
                        ss << "&(" << ident->name << ")";
                }
                else
                {
                    ss << "&(" << arg.value->to_webcc() << ")";
                }
            }
            else if (arg.is_move)
            {
                ss << "std::move(" << arg.value->to_webcc() << ")";
            }
            else
            {
                ss << arg.value->to_webcc();
            }
        }
    };

    // Emit creation, mounting, and early-return for a matched route.
    auto emit_route_mount = [&](size_t i, const RouteEntry &route, const std::string &indent)
    {
        ss << indent << "_route_" << i << " = new " << qualified_name(route.module_name, route.component_name) << "{";
        emit_ctor_args(i, route);
        ss << "};\n";
        ss << indent << "_route_" << i << "->view(_route_parent);\n";
        ss << indent << "webcc::dom::insert_before(_route_parent, _route_" << i << "->_get_root_element(), _route_anchor);\n";
        ss << indent << "webcc::flush();\n";
        ss << indent << "return;\n";
    };

    // Try each non-default route in order; the first match renders and returns.
    for (size_t i = 0; i < component.router->routes.size(); ++i)
    {
        const auto &route = component.router->routes[i];
        if (route.is_default)
            continue;

        if (route.path_params.empty())
        {
            // Static route: exact path comparison.
            ss << "        if (_current_route == \"" << route.path << "\") {\n";
            emit_route_mount(i, route, "            ");
            ss << "        }\n";
        }
        else
        {
            // Dynamic route: structural match, then parse each param to its type.
            // A parse failure (e.g. "abc" for an int) leaves the route unmatched.
            size_t k = route.path_params.size();
            ss << "        {\n";
            ss << "            coi::string _rc_" << i << "[" << k << "];\n";
            ss << "            if (__coi_route::match(\"" << route.path << "\", " << route.path.size() << ", _current_route, _rc_" << i << ")) {\n";
            ss << "                bool _ok_" << i << " = true;\n";
            for (size_t p = 0; p < route.path_params.size(); ++p)
            {
                const std::string &t = route.path_params[p].type;
                std::string lhs = "_rp_" + std::to_string(i) + "_" + std::to_string(p);
                std::string src = "_rc_" + std::to_string(i) + "[" + std::to_string(p) + "]";
                if (t == "int")
                    ss << "                int " << lhs << " = __coi_route::to_int(" << src << ", _ok_" << i << ");\n";
                else if (t == "bool")
                    ss << "                bool " << lhs << " = __coi_route::to_bool(" << src << ", _ok_" << i << ");\n";
                else
                    ss << "                coi::string " << lhs << " = " << src << ";\n";
            }
            ss << "                if (_ok_" << i << ") {\n";
            emit_route_mount(i, route, "                    ");
            ss << "                }\n";
            ss << "            }\n";
            ss << "        }\n";
        }
    }

    // Catch-all: the 'else' route if defined, otherwise the first static route so
    // the app always renders something for an unmatched path.
    int default_idx = -1;
    for (size_t i = 0; i < component.router->routes.size(); ++i)
    {
        if (component.router->routes[i].is_default)
        {
            default_idx = static_cast<int>(i);
            break;
        }
    }
    if (default_idx >= 0)
    {
        emit_route_mount(static_cast<size_t>(default_idx), component.router->routes[default_idx], "        ");
    }
    else
    {
        for (size_t i = 0; i < component.router->routes.size(); ++i)
        {
            const auto &route = component.router->routes[i];
            if (!route.is_default && route.path_params.empty())
            {
                emit_route_mount(i, route, "        ");
                break;
            }
        }
    }

    ss << "    }\n";
}
