#include "component.h"

void emit_component_lifecycle_methods(std::stringstream &ss,
                                      CompilerSession &session,
                                      const Component &component,
                                      const EventMasks &masks,
                                      const std::vector<IfRegion> &if_regions,
                                      int element_count,
                                      const std::map<std::string, int> &component_members)
{
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
                if (masks.click && (masks.click & (1ULL << el_id)))
                    ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.input && (masks.input & (1ULL << el_id)))
                    ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.change && (masks.change & (1ULL << el_id)))
                    ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.keydown && (masks.keydown & (1ULL << el_id)))
                    ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
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
                if (masks.click && (masks.click & (1ULL << el_id)))
                    ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.input && (masks.input & (1ULL << el_id)))
                    ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.change && (masks.change & (1ULL << el_id)))
                    ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.keydown && (masks.keydown & (1ULL << el_id)))
                    ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
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
            if (masks.click && (masks.click & (1ULL << i)))
                ss << "        g_dispatcher.remove(el[" << i << "]);\n";
            if (masks.input && (masks.input & (1ULL << i)))
                ss << "        g_input_dispatcher.remove(el[" << i << "]);\n";
            if (masks.change && (masks.change & (1ULL << i)))
                ss << "        g_change_dispatcher.remove(el[" << i << "]);\n";
            if (masks.keydown && (masks.keydown & (1ULL << i)))
                ss << "        g_keydown_dispatcher.remove(el[" << i << "]);\n";
        }
        // Now handle each if region's conditional elements
        for (const auto &region : if_regions)
        {
            ss << "        if (_if_" << region.if_id << "_state) {\n";
            for (int el_id : region.then_element_ids)
            {
                if (masks.click && (masks.click & (1ULL << el_id)))
                    ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.input && (masks.input & (1ULL << el_id)))
                    ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.change && (masks.change & (1ULL << el_id)))
                    ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.keydown && (masks.keydown & (1ULL << el_id)))
                    ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
            }
            ss << "        } else {\n";
            for (int el_id : region.else_element_ids)
            {
                if (masks.click && (masks.click & (1ULL << el_id)))
                    ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.input && (masks.input & (1ULL << el_id)))
                    ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.change && (masks.change & (1ULL << el_id)))
                    ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.keydown && (masks.keydown & (1ULL << el_id)))
                    ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
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
        if (masks.click)
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_click_mask & (1ULL << i)) g_dispatcher.remove(el[i]);\n";
        if (masks.input)
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_input_mask & (1ULL << i)) g_input_dispatcher.remove(el[i]);\n";
        if (masks.change)
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_change_mask & (1ULL << i)) g_change_dispatcher.remove(el[i]);\n";
        if (masks.keydown)
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_keydown_mask & (1ULL << i)) g_keydown_dispatcher.remove(el[i]);\n";
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
    ss << "    }\n";

    // Remove view method - removes DOM elements but keeps component state intact
    // Used for member references inside if-statements that toggle visibility
    // skip_dom_removal: if true, only unregisters handlers (caller will bulk-clear DOM)
    ss << "    void _remove_view(bool skip_dom_removal = false) {\n";

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
                if (masks.click && (masks.click & (1ULL << el_id)))
                    ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
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
                if (masks.click && (masks.click & (1ULL << el_id)))
                    ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
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
            if (masks.click && (masks.click & (1ULL << i)))
                ss << "        g_dispatcher.remove(el[" << i << "]);\n";
            if (masks.input && (masks.input & (1ULL << i)))
                ss << "        g_input_dispatcher.remove(el[" << i << "]);\n";
            if (masks.change && (masks.change & (1ULL << i)))
                ss << "        g_change_dispatcher.remove(el[" << i << "]);\n";
            if (masks.keydown && (masks.keydown & (1ULL << i)))
                ss << "        g_keydown_dispatcher.remove(el[" << i << "]);\n";
        }
        // Now handle each if region's conditional elements
        for (const auto &region : if_regions)
        {
            ss << "        if (_if_" << region.if_id << "_state) {\n";
            for (int el_id : region.then_element_ids)
            {
                if (masks.click && (masks.click & (1ULL << el_id)))
                    ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.input && (masks.input & (1ULL << el_id)))
                    ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.change && (masks.change & (1ULL << el_id)))
                    ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.keydown && (masks.keydown & (1ULL << el_id)))
                    ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
            }
            ss << "        } else {\n";
            for (int el_id : region.else_element_ids)
            {
                if (masks.click && (masks.click & (1ULL << el_id)))
                    ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.input && (masks.input & (1ULL << el_id)))
                    ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.change && (masks.change & (1ULL << el_id)))
                    ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
                if (masks.keydown && (masks.keydown & (1ULL << el_id)))
                    ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
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
        if (masks.click)
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_click_mask & (1ULL << i)) g_dispatcher.remove(el[i]);\n";
        if (masks.input)
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_input_mask & (1ULL << i)) g_input_dispatcher.remove(el[i]);\n";
        if (masks.change)
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_change_mask & (1ULL << i)) g_change_dispatcher.remove(el[i]);\n";
        if (masks.keydown)
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_keydown_mask & (1ULL << i)) g_keydown_dispatcher.remove(el[i]);\n";
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

    bool has_child_with_tick = false;
    for (auto const &[comp_name, count] : component_members)
    {
        if (session.components_with_tick.count(comp_name))
        {
            has_child_with_tick = true;
            break;
        }
    }

    bool needs_tick = has_user_tick || has_child_with_tick;
    if (needs_tick)
    {
        session.components_with_tick.insert(component.name);
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
            if (session.components_with_tick.count(comp_name))
            {
                for (int i = 0; i < count; ++i)
                {
                    ss << "        " << comp_name << "_" << i << ".tick(dt);\n";
                }
            }
        }
        ss << "    }\n";
    }
}
