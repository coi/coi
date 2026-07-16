#include "component.h"

// Resolve a component reference as it appears at a use site (a view child key
// like "Button" or "TurboUI_Button", a state type like "Store" or
// "Turbo::Store", a route target) to the canonical qualified component name
// used as the key of session.component_info and session.components_with_tick.
// Returns "" if the name does not refer to a component (e.g. a pod type).
static std::string resolve_component_qname(const CompilerSession &session,
                                           const std::string &module_name,
                                           std::string name)
{
    size_t dcolon = name.find("::");
    if (dcolon != std::string::npos)
        name = name.substr(0, dcolon) + "_" + name.substr(dcolon + 2);
    if (session.component_info.count(name))
        return name;
    std::string same_module = qualified_name(module_name, name);
    if (session.component_info.count(same_module))
        return same_module;
    return "";
}

void emit_component_lifecycle_methods(std::stringstream &ss,
                                      CompilerSession &session,
                                      const Component &component,
                                      const EventMasks &masks,
                                      const std::vector<IfRegion> &if_regions,
                                      int element_count,
                                      const std::map<std::string, int> &component_members)
{
    auto emit_listen_unregistration = [&]() {
        for (size_t i = 0; i < component.listen_entries.size(); ++i)
        {
            const auto &entry = component.listen_entries[i];
            std::string target_expr = entry.target_is_reference ? ("(*" + entry.target_name + ")") : entry.target_name;
            ss << "        if (_listen_reg_" << i << " != 0) {\n";
            ss << "            " << target_expr << "._remove_listener_" << entry.signal_name << "(_listen_reg_" << i << ");\n";
            ss << "            _listen_reg_" << i << " = 0;\n";
            ss << "        }\n";
        }
    };

    // WebSocket callbacks are registered in global dispatchers keyed by the
    // socket handle and captured with [this]. When a component is destroyed
    // (e.g. a routed page swapped out by _sync_route), those entries must be
    // removed, or a later async message/open/close/error event dispatches into
    // freed memory (use-after-free). Owned (non-reference) WebSocket members are
    // torn down here; any component with a WebSocket member always has the
    // websocket feature enabled, so the g_ws_* dispatchers are guaranteed to
    // exist.
    auto emit_websocket_teardown = [&]() {
        for (const auto &var : component.state)
        {
            if (!var || var->type != "WebSocket" || var->is_reference)
                continue;
            const std::string &name = var->name;
            ss << "        if (" << name << ".is_valid()) { webcc::websocket::close(" << name << "); }\n";
            ss << "        g_ws_message_dispatcher.remove(" << name << ");\n";
            ss << "        g_ws_open_dispatcher.remove(" << name << ");\n";
            ss << "        g_ws_close_dispatcher.remove(" << name << ");\n";
            ss << "        g_ws_error_dispatcher.remove(" << name << ");\n";
        }
    };

    // Destroy method
    ss << "    void _destroy() {\n";

    // Collect all elements that are conditionally created in if/else regions
    std::set<int> conditional_els;
    for (const auto &region : if_regions)
    {
        for (int el_id : region.then_element_ids)
            conditional_els.insert(el_id);
        for (int el_id : region.else_element_ids)
            conditional_els.insert(el_id);
    }

    auto emit_remove_handlers_for_element = [&](int el_id, const std::string &indent, const std::string &condition = "") {
        for (const auto &spec : get_event_specs())
        {
            if (!event_mask_test(masks, spec.type, el_id))
            {
                continue;
            }
            ss << indent;
            if (!condition.empty())
            {
                ss << "if (" << condition << ") ";
            }
            ss << spec.dispatcher_name << ".remove(el[" << el_id << "]);\n";
        }
    };

    auto emit_remove_handlers_loop = [&](const std::string &indent) {
        for (const auto &spec : get_event_specs())
        {
            if (!has_event_mask(masks, spec.type))
            {
                continue;
            }
            ss << indent << "for (int i = 0; i < " << element_count << "; i++) if ("
               << get_event_mask_name(spec.type) << "[i >> 6] & (1ULL << (i & 63))) " << spec.dispatcher_name << ".remove(el[i]);\n";
        }
    };

    // Determine if the view has if/else regions that control root elements
    // If so, we need to conditionally remove elements based on _if_N_state
    std::set<int> then_els, else_els;
    int root_if_id = -1;
    for (const auto &region : if_regions)
    {
        // Check if this if region contains root-level elements (el[0] or similar low indices)
        for (int el_id : region.then_element_ids)
        {
            then_els.insert(el_id);
            if (el_id == 0)
                root_if_id = region.if_id;
        }
        for (int el_id : region.else_element_ids)
        {
            else_els.insert(el_id);
            if (root_if_id < 0)
            {
                // Check if first else element could be a root
                for (int tel_id : region.then_element_ids)
                {
                    if (tel_id == 0)
                    {
                        root_if_id = region.if_id;
                        break;
                    }
                }
            }
        }
    }

    // If we have if/else at root level, generate conditional destroy
    if (root_if_id >= 0 && !if_regions.empty())
    {
        // Find the root if region
        const IfRegion *root_region = nullptr;
        for (const auto &region : if_regions)
        {
            if (region.if_id == root_if_id)
            {
                root_region = &region;
                break;
            }
        }

        if (root_region)
        {
            // Remove event handlers conditionally based on which branch is active
            ss << "        if (_if_" << root_if_id << "_state) {\n";
            // Remove handlers for then-branch elements
            for (int el_id : root_region->then_element_ids)
            {
                emit_remove_handlers_for_element(el_id, "            ");
            }
            // Remove the then-branch root element
            if (!root_region->then_element_ids.empty())
            {
                ss << "            webcc::dom::remove_element(el[" << root_region->then_element_ids[0] << "]);\n";
            }
            ss << "        } else {\n";
            // Remove handlers for else-branch elements
            for (int el_id : root_region->else_element_ids)
            {
                emit_remove_handlers_for_element(el_id, "            ");
            }
            // Remove the else-branch root element
            if (!root_region->else_element_ids.empty())
            {
                ss << "            webcc::dom::remove_element(el[" << root_region->else_element_ids[0] << "]);\n";
            }
            ss << "        }\n";
        }
    }
    else if (!conditional_els.empty())
    {
        // Has if/else regions but not at root level - need conditional cleanup
        // First remove unconditional element handlers
        for (int i = 0; i < element_count; ++i)
        {
            if (conditional_els.count(i))
                continue; // Skip conditional elements
            emit_remove_handlers_for_element(i, "        ");
        }
        // Now handle each if region's conditional elements
        for (const auto &region : if_regions)
        {
            ss << "        if (_if_" << region.if_id << "_state) {\n";
            for (int el_id : region.then_element_ids)
            {
                emit_remove_handlers_for_element(el_id, "            ");
            }
            ss << "        } else {\n";
            for (int el_id : region.else_element_ids)
            {
                emit_remove_handlers_for_element(el_id, "            ");
            }
            ss << "        }\n";
        }
        // Remove root element (which removes all children)
        if (element_count > 0)
        {
            ss << "        webcc::dom::remove_element(el[0]);\n";
        }
    }
    else
    {
        // No if/else regions at all, use the original simple approach
        emit_remove_handlers_loop("        ");
        if (element_count > 0)
        {
            ss << "        webcc::dom::remove_element(el[0]);\n";
        }
    }
    // Cleanup route components
    if (component.router)
    {
        for (size_t i = 0; i < component.router->routes.size(); ++i)
        {
            ss << "        if (_route_" << i << ") { _route_" << i << "->_destroy(); delete _route_" << i << "; }\n";
        }
    }

    // Unregister signal listeners bound via listen { ... }
    emit_listen_unregistration();

    // Tear down owned WebSocket callbacks so no async event fires into freed
    // memory after this component is deleted.
    emit_websocket_teardown();

    ss << "    }\n";

    // Remove view method - removes DOM elements but keeps component state intact
    // Used for member references inside if-statements that toggle visibility
    // skip_dom_removal: if true, only unregisters handlers (caller will bulk-clear DOM)
    ss << "    void _remove_view(bool skip_dom_removal = false) {\n";

    // Never rendered (e.g. a member component inside a region whose condition
    // was never true): nothing to remove, no handlers to unregister, and the
    // el/anchor handles are still invalid, so removing would warn JS-side.
    if (root_if_id >= 0 && !if_regions.empty())
        ss << "        if (!_if_" << root_if_id << "_anchor.is_valid()) return;\n";
    else if (element_count > 0)
        ss << "        if (!el[0].is_valid()) return;\n";

    // If we have if/else at root level, handle both branches
    if (root_if_id >= 0 && !if_regions.empty())
    {
        const IfRegion *root_region = nullptr;
        for (const auto &region : if_regions)
        {
            if (region.if_id == root_if_id)
            {
                root_region = &region;
                break;
            }
        }
        if (root_region)
        {
            ss << "        if (_if_" << root_if_id << "_state) {\n";
            // Remove handlers for then-branch elements
            for (int el_id : root_region->then_element_ids)
            {
                emit_remove_handlers_for_element(el_id, "            ");
            }
            // Remove the then-branch root element
            if (!root_region->then_element_ids.empty())
            {
                ss << "            if (!skip_dom_removal) webcc::dom::remove_element(el[" << root_region->then_element_ids[0] << "]);\n";
            }
            ss << "        } else {\n";
            // Remove handlers for else-branch elements
            for (int el_id : root_region->else_element_ids)
            {
                emit_remove_handlers_for_element(el_id, "            ");
            }
            // Remove the else-branch root element
            if (!root_region->else_element_ids.empty())
            {
                ss << "            if (!skip_dom_removal) webcc::dom::remove_element(el[" << root_region->else_element_ids[0] << "]);\n";
            }
            ss << "        }\n";
            // Also remove the anchor
            ss << "        if (!skip_dom_removal) webcc::dom::remove_element(_if_" << root_if_id << "_anchor);\n";
        }
    }
    else if (!conditional_els.empty())
    {
        // Has if/else regions but not at root level - need conditional cleanup
        // First remove unconditional element handlers
        for (int i = 0; i < element_count; ++i)
        {
            if (conditional_els.count(i))
                continue; // Skip conditional elements
            emit_remove_handlers_for_element(i, "        ");
        }
        // Now handle each if region's conditional elements
        for (const auto &region : if_regions)
        {
            ss << "        if (_if_" << region.if_id << "_state) {\n";
            for (int el_id : region.then_element_ids)
            {
                emit_remove_handlers_for_element(el_id, "            ");
            }
            ss << "        } else {\n";
            for (int el_id : region.else_element_ids)
            {
                emit_remove_handlers_for_element(el_id, "            ");
            }
            ss << "        }\n";
        }
        // Remove child component views recursively
        for (auto const &[comp_name, count] : component_members)
        {
            for (int i = 0; i < count; ++i)
            {
                ss << "        " << comp_name << "_" << i << "._remove_view(skip_dom_removal);\n";
            }
        }
        // Remove root element (which removes all children)
        if (element_count > 0)
        {
            ss << "        if (!skip_dom_removal) webcc::dom::remove_element(el[0]);\n";
        }
    }
    else
    {
        // No if/else regions at all, use the original simple approach
        // Remove all event handlers
        emit_remove_handlers_loop("        ");
        // Remove child component views recursively
        for (auto const &[comp_name, count] : component_members)
        {
            for (int i = 0; i < count; ++i)
            {
                ss << "        " << comp_name << "_" << i << "._remove_view(skip_dom_removal);\n";
            }
        }
        // Remove root element (which removes all children)
        if (element_count > 0)
        {
            ss << "        if (!skip_dom_removal) webcc::dom::remove_element(el[0]);\n";
        }
    }
    ss << "    }\n";

    // _get_root_element method - returns the root DOM element for this component
    // Handles if/else at root level by checking _if_X_state
    ss << "    webcc::handle _get_root_element() {\n";
    if (root_if_id >= 0)
    {
        // Has if/else at root level
        auto &root_region = if_regions[root_if_id];
        ss << "        if (_if_" << root_if_id << "_state) {\n";
        if (!root_region.then_element_ids.empty())
        {
            ss << "            return el[" << root_region.then_element_ids[0] << "];\n";
        }
        else
        {
            ss << "            return webcc::handle{0};\n";
        }
        ss << "        } else {\n";
        if (!root_region.else_element_ids.empty())
        {
            ss << "            return el[" << root_region.else_element_ids[0] << "];\n";
        }
        else
        {
            ss << "            return webcc::handle{0};\n";
        }
        ss << "        }\n";
    }
    else
    {
        // No if/else at root, just return el[0]
        if (element_count > 0)
        {
            ss << "        return el[0];\n";
        }
        else
        {
            ss << "        return webcc::handle{0};\n";
        }
    }
    ss << "    }\n";

    // Tick method
    bool has_user_tick = false;
    bool user_tick_has_args = false;
    for (auto &m : component.methods)
        if (m.name == "tick")
        {
            has_user_tick = true;
            if (!m.params.empty())
                user_tick_has_args = true;
        }

    // Whether a component reference at a use site resolves to a component that
    // emitted a tick method. Topological sort guarantees children (view,
    // state-member, and route dependencies alike) are emitted before their
    // parents, so components_with_tick is complete for every name checked here.
    auto ticks = [&](const std::string &name) {
        std::string qname = resolve_component_qname(session, component.module_name, name);
        return !qname.empty() && session.components_with_tick.count(qname) > 0;
    };

    bool has_child_with_tick = false;
    for (auto const &[comp_name, count] : component_members)
    {
        if (ticks(comp_name))
        {
            has_child_with_tick = true;
            break;
        }
    }

    // Component-typed state members (e.g. a logic-only Store, or a member
    // mounted via <{name}/>) never appear in component_members, which only
    // covers components instantiated directly in the view, so collect them
    // here or they would never receive tick. Reference members are skipped:
    // the component that owns them forwards tick to them.
    std::vector<const VarDeclaration *> tickable_state_members;
    for (const auto &var : component.state)
    {
        if (var->is_reference)
            continue;
        std::string base_type = var->type;
        if (base_type.ends_with("[]"))
            base_type = base_type.substr(0, base_type.size() - 2);
        if (ticks(base_type))
            tickable_state_members.push_back(var.get());
    }

    // Router-mounted pages are dynamic children too: if any route target has a
    // tick, this component must forward tick to the active route. Only one
    // _route_N is non-null at a time (see _sync_route), so the guarded forward
    // below hits exactly the mounted page.
    bool has_route_with_tick = false;
    if (component.router)
    {
        for (auto const &route : component.router->routes)
        {
            if (ticks(qualified_name(route.module_name, route.component_name)))
            {
                has_route_with_tick = true;
                break;
            }
        }
    }

    bool needs_tick = has_user_tick || has_child_with_tick || has_route_with_tick ||
                      !tickable_state_members.empty();
    if (needs_tick)
    {
        session.components_with_tick.insert(qualified_name(component.module_name, component.name));
        ss << "    void tick(double dt) {\n";

        if (has_user_tick)
        {
            if (user_tick_has_args)
                ss << "        _user_tick(dt);\n";
            else
                ss << "        _user_tick();\n";
        }

        for (auto const &[comp_name, count] : component_members)
        {
            if (ticks(comp_name))
            {
                for (int i = 0; i < count; ++i)
                {
                    ss << "        " << comp_name << "_" << i << ".tick(dt);\n";
                }
            }
        }

        for (const auto *var : tickable_state_members)
        {
            if (var->type.ends_with("[]"))
                ss << "        for (auto &_c : " << var->name << ") _c.tick(dt);\n";
            else
                ss << "        " << var->name << ".tick(dt);\n";
        }

        // Forward tick to the currently-mounted route page. Route pages are heap
        // pointers, so guard on non-null (inactive routes are nullptr).
        if (component.router)
        {
            for (size_t i = 0; i < component.router->routes.size(); ++i)
            {
                const auto &route = component.router->routes[i];
                if (ticks(qualified_name(route.module_name, route.component_name)))
                {
                    ss << "        if (_route_" << i << ") _route_" << i << "->tick(dt);\n";
                }
            }
        }
        ss << "    }\n";
    }
}
