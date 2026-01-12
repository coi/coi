#include "component.h"
#include "formatter.h"
#include "../schema_loader.h"
#include <cctype>
#include <algorithm>


/*  TODO (simplifing or outsourced into seperate functions): 
    - Event handle bitmask calcuation (click_mask, input_mask, change_mask, and keydown_mask can be moved to a helper function)
    - Child component member and loop vector declarations
    - Loop region and if region tracking declarations
    - Update method code generation (logic for building _update_{varname}() methods)
    - Sync loop and sync if method generation (code for generating _sync_loop_X() and _sync_if_X())
    - Child update collection ( (collect_child_updates) could be made more modular)
    - Method code generation (generate_method could be a standalone function)
    - Event handler method generation (_handler_X_event() methods can be extracted)
    - View method event handler registration ( registering event handlers in the view method can be moved to a helper)
    - Rebind and destroy method generation (_rebind() and _destroy() methods can be split out)
    - Tick method generation (tick method can be moved to a helper)
    - Callback name generation (The repeated logic for generating callback names (e.g., "on" + std::toupper(...) + ... + "Change") can be a utility function)
    - Formatter block generation  (generating formatter blocks for attributes and text can be centralized) (see formatter.cc)
    - Parsing and splitting argument strings 
    - Global context management (The logic for managing g_ref_props can be encapsulated)

    ... phew  ._.
*/


// Per-component context for tracking reference props
std::set<std::string> g_ref_props;

void Component::collect_child_components(ASTNode* node, std::map<std::string, int>& counts) {
    if(auto comp = dynamic_cast<ComponentInstantiation*>(node)) {
        counts[comp->component_name]++;
    }
    if(auto el = dynamic_cast<HTMLElement*>(node)) {
        for(auto& child : el->children) {
            collect_child_components(child.get(), counts);
        }
    }
    if(auto viewIf = dynamic_cast<ViewIfStatement*>(node)) {
        for(auto& child : viewIf->then_children) {
            collect_child_components(child.get(), counts);
        }
        for(auto& child : viewIf->else_children) {
            collect_child_components(child.get(), counts);
        }
    }
}

// Collect component types used inside for loops
static void collect_loop_components(ASTNode* node, std::set<std::string>& loop_components, bool in_loop = false) {
    if(auto comp = dynamic_cast<ComponentInstantiation*>(node)) {
        if (in_loop) {
            loop_components.insert(comp->component_name);
        }
    }
    if(auto el = dynamic_cast<HTMLElement*>(node)) {
        for(auto& child : el->children) {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
    }
    if(auto viewIf = dynamic_cast<ViewIfStatement*>(node)) {
        for(auto& child : viewIf->then_children) {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
        for(auto& child : viewIf->else_children) {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
    }
    if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(node)) {
        for(auto& child : viewFor->children) {
            collect_loop_components(child.get(), loop_components, true);
        }
    }
    if(auto viewForEach = dynamic_cast<ViewForEachStatement*>(node)) {
        for(auto& child : viewForEach->children) {
            collect_loop_components(child.get(), loop_components, true);
        }
    }
}

void Component::collect_child_updates(ASTNode* node, std::map<std::string, std::vector<std::string>>& updates, std::map<std::string, int>& counters) {
    if(auto comp = dynamic_cast<ComponentInstantiation*>(node)) {
        std::string instance_name = comp->component_name + "_" + std::to_string(counters[comp->component_name]++);
        
        for(const auto& prop : comp->props) {
            if(prop.is_reference) {
                std::set<std::string> deps;
                prop.value->collect_dependencies(deps);
                for(const auto& dep : deps) {
                    updates[dep].push_back("        " + instance_name + "._update_" + prop.name + "();\n");
                }
            }
        }
    }
    if(auto el = dynamic_cast<HTMLElement*>(node)) {
        for(auto& child : el->children) {
            collect_child_updates(child.get(), updates, counters);
        }
    }
    if(auto viewIf = dynamic_cast<ViewIfStatement*>(node)) {
        for(auto& child : viewIf->then_children) {
            collect_child_updates(child.get(), updates, counters);
        }
        for(auto& child : viewIf->else_children) {
            collect_child_updates(child.get(), updates, counters);
        }
    }
}

std::string Component::to_webcc(CompilerSession& session) {
    std::stringstream ss;
    std::vector<EventHandler> event_handlers;
    std::vector<Binding> bindings;
    std::map<std::string, int> component_counters;
    std::map<std::string, int> component_members;
    std::set<std::string> loop_component_types;
    std::vector<LoopRegion> loop_regions;
    std::vector<IfRegion> if_regions;
    int element_count = 0;
    int loop_counter = 0;
    int if_counter = 0;
    
    // Populate global context for reference params
    g_ref_props.clear();
    for(auto& param : params) {
        if(param->is_reference) {
            g_ref_props.insert(param->name);
        }
    }
    
    // Collect child components
    for(auto& root : render_roots) {
        collect_child_components(root.get(), component_members);
        collect_loop_components(root.get(), loop_component_types);
    }

    // Collect method names
    std::set<std::string> method_names;
    for(auto& m : methods) method_names.insert(m.name);
    
    // Track pub mut state variables
    std::set<std::string> pub_mut_vars;
    for(auto& var : state) {
        if(var->is_public && var->is_mutable) {
            pub_mut_vars.insert(var->name);
        }
    }

    std::stringstream ss_render;
    for(auto& root : render_roots){
        if(auto el = dynamic_cast<HTMLElement*>(root.get())){
            el->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        } else if(auto comp = dynamic_cast<ComponentInstantiation*>(root.get())){
            comp->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        } else if(auto viewIf = dynamic_cast<ViewIfStatement*>(root.get())){
            viewIf->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        } else if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(root.get())){
            viewFor->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        } else if(auto viewForEach = dynamic_cast<ViewForEachStatement*>(root.get())){
            viewForEach->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        }
    }

    // Generate component as a class
    ss << "class " << name << " {\n";
    ss << "public:\n";

    // Structs
    for(auto& s : structs){
        ss << s->to_webcc() << "\n";
    }
    
    // Enums
    for(auto& e : enums){
        ss << e->to_webcc() << "\n";
    }
    
    // Component parameters
    for(auto& param : params){
        ss << "    " << convert_type(param->type);
        if(param->is_reference) {
            ss << "* " << param->name << " = nullptr";
        } else {
            ss << " " << param->name;
            if(param->default_value){
                ss << " = " << param->default_value->to_webcc();
            }
        }
        ss << ";\n";
        
        if(param->is_reference && param->is_mutable) {
            std::string callback_name = "on" + std::string(1, std::toupper(param->name[0])) + param->name.substr(1) + "Change";
            ss << "    webcc::function<void()> " << callback_name << ";\n";
        }
    }
    
    // State variables
    for(auto& var : state){
        ss << "    " << (var->is_mutable ? "" : "const ") << convert_type(var->type);
        if(var->is_reference) ss << "&";
        ss << " " << var->name;
        if(var->initializer){
            if (SchemaLoader::instance().is_handle(var->type)) {
                ss << "{" << var->initializer->to_webcc() << "}";
            } else {
                ss << " = " << var->initializer->to_webcc();
            }
        }
        ss << ";\n";
        
        if(var->is_public && var->is_mutable) {
            std::string callback_name = "on" + std::string(1, std::toupper(var->name[0])) + var->name.substr(1) + "Change";
            ss << "    webcc::function<void()> " << callback_name << ";\n";
        }
    }

    // Element handles
    if (element_count > 0) {
        ss << "    webcc::handle el[" << element_count << "];\n";
    }
    
    // Event handler bitmasks
    uint64_t click_mask = 0, input_mask = 0, change_mask = 0, keydown_mask = 0;
    for (const auto& handler : event_handlers) {
        if (handler.element_id < 64) {
            if (handler.event_type == "click") click_mask |= (1ULL << handler.element_id);
            else if (handler.event_type == "input") input_mask |= (1ULL << handler.element_id);
            else if (handler.event_type == "change") change_mask |= (1ULL << handler.element_id);
            else if (handler.event_type == "keydown") keydown_mask |= (1ULL << handler.element_id);
        }
    }
    if (click_mask) ss << "    static constexpr uint64_t _click_mask = 0x" << std::hex << click_mask << std::dec << "ULL;\n";
    if (input_mask) ss << "    static constexpr uint64_t _input_mask = 0x" << std::hex << input_mask << std::dec << "ULL;\n";
    if (change_mask) ss << "    static constexpr uint64_t _change_mask = 0x" << std::hex << change_mask << std::dec << "ULL;\n";
    if (keydown_mask) ss << "    static constexpr uint64_t _keydown_mask = 0x" << std::hex << keydown_mask << std::dec << "ULL;\n";
    
    // Child component members
    for(auto const& [comp_name, count] : component_members) {
        for(int i=0; i<count; ++i) {
            ss << "    " << comp_name << " " << comp_name << "_" << i << ";\n";
        }
    }
    
    // Vector members for components in loops
    for(const auto& comp_name : loop_component_types) {
        ss << "    webcc::vector<" << comp_name << "> _loop_" << comp_name << "s;\n";
    }
    
    // Loop region tracking
    for(const auto& region : loop_regions) {
        ss << "    webcc::handle _loop_" << region.loop_id << "_parent;\n";
        if (region.is_keyed) {
            ss << "    webcc::unordered_map<" << region.key_type << ", int> _loop_" << region.loop_id << "_map;\n";
        } else {
            ss << "    int _loop_" << region.loop_id << "_count = 0;\n";
        }
        if (region.is_html_loop) {
            ss << "    webcc::vector<webcc::handle> _loop_" << region.loop_id << "_elements;\n";
        }
    }
    
    // If region tracking
    for(const auto& region : if_regions) {
        ss << "    webcc::handle _if_" << region.if_id << "_parent;\n";
        ss << "    bool _if_" << region.if_id << "_state = false;\n";
    }

    // Build update entries map
    struct UpdateEntry {
        std::string code;
        int if_region_id;
        bool in_then_branch;
    };
    std::map<std::string, std::vector<UpdateEntry>> var_update_entries;
    
    for(const auto& binding : bindings) {
        for(const auto& dep : binding.dependencies) {
            std::string el_var = "el[" + std::to_string(binding.element_id) + "]";
            std::string update_line;
            
            bool optimized = false;
            if(binding.expr) {
                if(auto strLit = dynamic_cast<StringLiteral*>(binding.expr)) {
                    if(binding.type == "attr") {
                        update_line = generate_formatter_block_from_string_literal(strLit,
                            "webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", ");
                    } else {
                        update_line = generate_formatter_block_from_string_literal(strLit,
                            "webcc::dom::set_inner_text(" + el_var + ", ");
                    }
                    optimized = true;
                }
            }
            
            if(!optimized && binding.value_code.find("webcc::string::concat(") == 0) {
                std::string args_str = binding.value_code.substr(22);
                if(!args_str.empty() && args_str.back() == ')') args_str.pop_back();
                
                std::vector<std::string> args;
                int paren_depth = 0;
                std::string current;
                for(size_t i = 0; i < args_str.size(); ++i) {
                    char c = args_str[i];
                    if(c == '(') paren_depth++;
                    else if(c == ')') paren_depth--;
                    else if(c == ',' && paren_depth == 0) {
                        while(!current.empty() && current.front() == ' ') current.erase(0, 1);
                        while(!current.empty() && current.back() == ' ') current.pop_back();
                        if(!current.empty()) args.push_back(current);
                        current.clear();
                        continue;
                    }
                    current += c;
                }
                while(!current.empty() && current.front() == ' ') current.erase(0, 1);
                while(!current.empty() && current.back() == ' ') current.pop_back();
                if(!current.empty()) args.push_back(current);
                
                if(binding.type == "attr") {
                    update_line = generate_formatter_block(args,
                        "webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", ");
                } else {
                    update_line = generate_formatter_block(args,
                        "webcc::dom::set_inner_text(" + el_var + ", ");
                }
                optimized = true;
            }
            
            if(!optimized) {
                if(binding.type == "attr") {
                    bool is_string_literal = !binding.value_code.empty() && binding.value_code.front() == '"';
                    if (is_string_literal) {
                        update_line = "webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", " + binding.value_code + ");";
                    } else {
                        update_line = generate_formatter_block({binding.value_code},
                            "webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", ");
                    }
                } else if(binding.type == "text") {
                    bool is_string_literal = !binding.value_code.empty() && binding.value_code.front() == '"';
                    if (is_string_literal) {
                        update_line = "webcc::dom::set_inner_text(" + el_var + ", " + binding.value_code + ");";
                    } else {
                        update_line = generate_formatter_block({binding.value_code},
                            "webcc::dom::set_inner_text(" + el_var + ", ");
                    }
                }
            }
            
            if(!update_line.empty()) {
                UpdateEntry entry;
                entry.code = update_line;
                entry.if_region_id = binding.if_region_id;
                entry.in_then_branch = binding.in_then_branch;
                var_update_entries[dep].push_back(entry);
            }
        }
    }
    
    // Generate _update_{varname}() methods
    std::set<std::string> generated_updaters;
    for(const auto& [var_name, entries] : var_update_entries) {
        if(!entries.empty()) {
            ss << "    void _update_" << var_name << "() {\n";
            
            for(const auto& entry : entries) {
                if(entry.if_region_id < 0) {
                    ss << "        " << entry.code << "\n";
                }
            }
            
            std::map<int, std::pair<std::vector<std::string>, std::vector<std::string>>> if_grouped;
            for(const auto& entry : entries) {
                if(entry.if_region_id >= 0) {
                    if(entry.in_then_branch) {
                        if_grouped[entry.if_region_id].first.push_back(entry.code);
                    } else {
                        if_grouped[entry.if_region_id].second.push_back(entry.code);
                    }
                }
            }
            
            for(const auto& [if_id, branches] : if_grouped) {
                const auto& then_codes = branches.first;
                const auto& else_codes = branches.second;
                
                if(!then_codes.empty() && !else_codes.empty()) {
                    ss << "        if (_if_" << if_id << "_state) {\n";
                    for(const auto& code : then_codes) {
                        ss << "            " << code << "\n";
                    }
                    ss << "        } else {\n";
                    for(const auto& code : else_codes) {
                        ss << "            " << code << "\n";
                    }
                    ss << "        }\n";
                } else if(!then_codes.empty()) {
                    ss << "        if (_if_" << if_id << "_state) {\n";
                    for(const auto& code : then_codes) {
                        ss << "            " << code << "\n";
                    }
                    ss << "        }\n";
                } else if(!else_codes.empty()) {
                    ss << "        if (!_if_" << if_id << "_state) {\n";
                    for(const auto& code : else_codes) {
                        ss << "            " << code << "\n";
                    }
                    ss << "        }\n";
                }
            }
            
            if(pub_mut_vars.count(var_name)) {
                std::string callback_name = "on" + std::string(1, std::toupper(var_name[0])) + var_name.substr(1) + "Change";
                ss << "        if(" << callback_name << ") " << callback_name << "();\n";
            }
            ss << "    }\n";
            generated_updaters.insert(var_name);
        }
    }
    
    // Generate _update methods for pub mut variables without UI bindings
    for(const auto& var_name : pub_mut_vars) {
        if(generated_updaters.find(var_name) == generated_updaters.end()) {
            std::string callback_name = "on" + std::string(1, std::toupper(var_name[0])) + var_name.substr(1) + "Change";
            ss << "    void _update_" << var_name << "() {\n";
            ss << "        if(" << callback_name << ") " << callback_name << "();\n";
            ss << "    }\n";
            generated_updaters.insert(var_name);
        }
    }

    // Ensure all params have update method
    for(const auto& param : params) {
        if(generated_updaters.find(param->name) == generated_updaters.end()) {
             ss << "    void _update_" << param->name << "() {}\n";
             generated_updaters.insert(param->name);
        }
    }
    
    // Map from variable to loop IDs
    std::map<std::string, std::vector<int>> var_to_loop_ids;
    for(const auto& region : loop_regions) {
        for(const auto& dep : region.dependencies) {
            var_to_loop_ids[dep].push_back(region.loop_id);
        }
    }
    
    // Generate _sync_loop_X() methods
    for(const auto& region : loop_regions) {
        ss << "    void _sync_loop_" << region.loop_id << "() {\n";
        
        if (region.is_keyed) {
            std::string map_name = "_loop_" + std::to_string(region.loop_id) + "_map";
            std::string vec_name = "_loop_" + region.component_type + "s";
            std::string key_field = region.key_expr.substr(region.var_name.length() + 1);
            
            ss << "        webcc::vector<int32_t> _new_keys;\n";
            ss << "        for (auto& " << region.var_name << " : " << region.iterable_expr << ") {\n";
            ss << "            _new_keys.push_back(" << region.key_expr << ");\n";
            ss << "        }\n";
            ss << "        \n";
            
            ss << "        webcc::vector<int32_t> _keys_to_remove;\n";
            ss << "        for (int _i = 0; _i < (int)" << vec_name << ".size(); _i++) {\n";
            ss << "            int32_t _old_key = " << vec_name << "[_i]." << key_field << ";\n";
            ss << "            bool _found = false;\n";
            ss << "            for (int _j = 0; _j < (int)_new_keys.size(); _j++) {\n";
            ss << "                if (_new_keys[_j] == _old_key) { _found = true; break; }\n";
            ss << "            }\n";
            ss << "            if (!_found) _keys_to_remove.push_back(_old_key);\n";
            ss << "        }\n";
            ss << "        \n";
            
            ss << "        bool _did_remove = false;\n";
            ss << "        for (int _r = 0; _r < (int)_keys_to_remove.size(); _r++) {\n";
            ss << "            int32_t _key_to_remove = _keys_to_remove[_r];\n";
            ss << "            for (int _i = (int)" << vec_name << ".size() - 1; _i >= 0; _i--) {\n";
            ss << "                if (" << vec_name << "[_i]." << key_field << " == _key_to_remove) {\n";
            ss << "                    " << vec_name << "[_i]._destroy();\n";
            ss << "                    " << vec_name << ".erase(_i);\n";
            ss << "                    " << map_name << ".erase(_key_to_remove);\n";
            ss << "                    _did_remove = true;\n";
            ss << "                    break;\n";
            ss << "                }\n";
            ss << "            }\n";
            ss << "        }\n";
            ss << "        if (_did_remove) {\n";
            ss << "            for (int _i = 0; _i < (int)" << vec_name << ".size(); _i++) " << vec_name << "[_i]._rebind();\n";
            ss << "        }\n";
            ss << "        \n";
            
            ss << "        int _old_size = (int)" << vec_name << ".size();\n";
            ss << "        for (auto& " << region.var_name << " : " << region.iterable_expr << ") {\n";
            ss << "            auto _key = " << region.key_expr << ";\n";
            ss << "            if (" << map_name << ".contains(_key)) continue;\n";
            
            std::string item_code = region.item_creation_code;
            std::stringstream indented;
            std::istringstream iss(item_code);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    indented << "        " << line << "\n";
                }
            }
            ss << indented.str();
            
            if (!region.component_type.empty()) {
                ss << "            " << map_name << "[_key] = 1;\n";
            }
            ss << "        }\n";
            ss << "        if ((int)" << vec_name << ".size() > _old_size) {\n";
            ss << "            for (int _i = 0; _i < _old_size; _i++) " << vec_name << "[_i]._rebind();\n";
            ss << "        }\n";
            
        } else {
            ss << "        int new_count = " << region.end_expr << " - " << region.start_expr << ";\n";
            ss << "        int old_count = _loop_" << region.loop_id << "_count;\n";
            ss << "        if (new_count == old_count) return;\n";
            ss << "        \n";
            
            if (!region.component_type.empty()) {
                std::string vec_name = "_loop_" + region.component_type + "s";
                
                ss << "        if (new_count > old_count) {\n";
                ss << "            for (int " << region.var_name << " = old_count; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";
                
                std::string item_code = region.item_creation_code;
                std::stringstream indented;
                std::istringstream iss(item_code);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty()) {
                        indented << "    " << line << "\n";
                    }
                }
                ss << indented.str();
                ss << "            }\n";
                
                ss << "            for (int _i = 0; _i < old_count; _i++) " << vec_name << "[_i]._rebind();\n";
                
                ss << "        } else {\n";
                ss << "            while ((int)" << vec_name << ".size() > new_count) {\n";
                ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
                ss << "                " << vec_name << ".pop_back();\n";
                ss << "            }\n";
                
                if (!region.item_update_code.empty()) {
                    ss << "            for (int " << region.var_name << " = 0; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";
                    ss << region.item_update_code;
                    ss << "            }\n";
                }
                ss << "        }\n";
            } else if (region.is_html_loop) {
                std::string vec_name = "_loop_" + std::to_string(region.loop_id) + "_elements";
                
                ss << "        if (new_count > old_count) {\n";
                ss << "            for (int " << region.var_name << " = old_count; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";
                
                std::string item_code = region.item_creation_code;
                std::stringstream indented;
                std::istringstream iss(item_code);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty()) {
                        indented << "    " << line << "\n";
                    }
                }
                ss << indented.str();
                
                if (!region.root_element_var.empty()) {
                    ss << "            " << vec_name << ".push_back(" << region.root_element_var << ");\n";
                }
                ss << "            }\n";
                ss << "        } else {\n";
                ss << "            while ((int)" << vec_name << ".size() > new_count) {\n";
                ss << "                webcc::dom::remove_element(" << vec_name << "[" << vec_name << ".size() - 1]);\n";
                ss << "                " << vec_name << ".pop_back();\n";
                ss << "            }\n";
                ss << "        }\n";
            }
            ss << "        _loop_" << region.loop_id << "_count = new_count;\n";
        }
        ss << "    }\n";
    }
    
    // Map from variable to if IDs
    std::map<std::string, std::vector<int>> var_to_if_ids;
    for(const auto& region : if_regions) {
        for(const auto& dep : region.dependencies) {
            var_to_if_ids[dep].push_back(region.if_id);
        }
    }
    
    // Generate _sync_if_X() methods
    for(const auto& region : if_regions) {
        ss << "    void _sync_if_" << region.if_id << "() {\n";
        ss << "        bool new_state = " << region.condition_code << ";\n";
        ss << "        if (new_state == _if_" << region.if_id << "_state) return;\n";
        ss << "        _if_" << region.if_id << "_state = new_state;\n";
        ss << "        \n";
        
        std::set<int> click_els, input_els, change_els, keydown_els;
        for (const auto& handler : event_handlers) {
            if (handler.event_type == "click") click_els.insert(handler.element_id);
            else if (handler.event_type == "input") input_els.insert(handler.element_id);
            else if (handler.event_type == "change") change_els.insert(handler.element_id);
            else if (handler.event_type == "keydown") keydown_els.insert(handler.element_id);
        }
        
        ss << "        if (new_state) {\n";
        for (int el_id : region.else_element_ids) {
            if (click_els.count(el_id)) ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
            if (input_els.count(el_id)) ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
            if (change_els.count(el_id)) ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
            if (keydown_els.count(el_id)) ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
        }
        for (int el_id : region.else_element_ids) {
            ss << "            webcc::dom::remove_element(el[" << el_id << "]);\n";
        }
        for (const auto& [comp_name, inst_id] : region.else_components) {
            ss << "            " << comp_name << "_" << inst_id << "._destroy();\n";
        }
        for (int loop_id : region.else_loop_ids) {
            for (const auto& lr : loop_regions) {
                if (lr.loop_id == loop_id) {
                    if (!lr.component_type.empty()) {
                        std::string vec_name = "_loop_" + lr.component_type + "s";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    } else if (lr.is_html_loop) {
                        std::string vec_name = "_loop_" + std::to_string(loop_id) + "_elements";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                webcc::dom::remove_element(" << vec_name << "[" << vec_name << ".size() - 1]);\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    }
                    break;
                }
            }
        }
        for (int nested_if_id : region.else_if_ids) {
            for (const auto& nested_region : if_regions) {
                if (nested_region.if_id == nested_if_id) {
                    for (int el_id : nested_region.then_element_ids) {
                        if (click_els.count(el_id)) ss << "            if (_if_" << nested_if_id << "_state) g_dispatcher.remove(el[" << el_id << "]);\n";
                        if (input_els.count(el_id)) ss << "            if (_if_" << nested_if_id << "_state) g_input_dispatcher.remove(el[" << el_id << "]);\n";
                        if (change_els.count(el_id)) ss << "            if (_if_" << nested_if_id << "_state) g_change_dispatcher.remove(el[" << el_id << "]);\n";
                        if (keydown_els.count(el_id)) ss << "            if (_if_" << nested_if_id << "_state) g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
                        ss << "            if (_if_" << nested_if_id << "_state) webcc::dom::remove_element(el[" << el_id << "]);\n";
                    }
                    for (int el_id : nested_region.else_element_ids) {
                        if (click_els.count(el_id)) ss << "            if (!_if_" << nested_if_id << "_state) g_dispatcher.remove(el[" << el_id << "]);\n";
                        if (input_els.count(el_id)) ss << "            if (!_if_" << nested_if_id << "_state) g_input_dispatcher.remove(el[" << el_id << "]);\n";
                        if (change_els.count(el_id)) ss << "            if (!_if_" << nested_if_id << "_state) g_change_dispatcher.remove(el[" << el_id << "]);\n";
                        if (keydown_els.count(el_id)) ss << "            if (!_if_" << nested_if_id << "_state) g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
                        ss << "            if (!_if_" << nested_if_id << "_state) webcc::dom::remove_element(el[" << el_id << "]);\n";
                    }
                }
            }
        }
        ss << region.then_creation_code;
        
        ss << "        } else {\n";
        for (int el_id : region.then_element_ids) {
            if (click_els.count(el_id)) ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
            if (input_els.count(el_id)) ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
            if (change_els.count(el_id)) ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
            if (keydown_els.count(el_id)) ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
        }
        for (int el_id : region.then_element_ids) {
            ss << "            webcc::dom::remove_element(el[" << el_id << "]);\n";
        }
        for (const auto& [comp_name, inst_id] : region.then_components) {
            ss << "            " << comp_name << "_" << inst_id << "._destroy();\n";
        }
        for (int loop_id : region.then_loop_ids) {
            for (const auto& lr : loop_regions) {
                if (lr.loop_id == loop_id) {
                    if (!lr.component_type.empty()) {
                        std::string vec_name = "_loop_" + lr.component_type + "s";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    } else if (lr.is_html_loop) {
                        std::string vec_name = "_loop_" + std::to_string(loop_id) + "_elements";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                webcc::dom::remove_element(" << vec_name << "[" << vec_name << ".size() - 1]);\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    }
                    break;
                }
            }
        }
        for (int nested_if_id : region.then_if_ids) {
            for (const auto& nested_region : if_regions) {
                if (nested_region.if_id == nested_if_id) {
                    for (int el_id : nested_region.then_element_ids) {
                        if (click_els.count(el_id)) ss << "            if (_if_" << nested_if_id << "_state) g_dispatcher.remove(el[" << el_id << "]);\n";
                        if (input_els.count(el_id)) ss << "            if (_if_" << nested_if_id << "_state) g_input_dispatcher.remove(el[" << el_id << "]);\n";
                        if (change_els.count(el_id)) ss << "            if (_if_" << nested_if_id << "_state) g_change_dispatcher.remove(el[" << el_id << "]);\n";
                        if (keydown_els.count(el_id)) ss << "            if (_if_" << nested_if_id << "_state) g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
                        ss << "            if (_if_" << nested_if_id << "_state) webcc::dom::remove_element(el[" << el_id << "]);\n";
                    }
                    for (int el_id : nested_region.else_element_ids) {
                        if (click_els.count(el_id)) ss << "            if (!_if_" << nested_if_id << "_state) g_dispatcher.remove(el[" << el_id << "]);\n";
                        if (input_els.count(el_id)) ss << "            if (!_if_" << nested_if_id << "_state) g_input_dispatcher.remove(el[" << el_id << "]);\n";
                        if (change_els.count(el_id)) ss << "            if (!_if_" << nested_if_id << "_state) g_change_dispatcher.remove(el[" << el_id << "]);\n";
                        if (keydown_els.count(el_id)) ss << "            if (!_if_" << nested_if_id << "_state) g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
                        ss << "            if (!_if_" << nested_if_id << "_state) webcc::dom::remove_element(el[" << el_id << "]);\n";
                    }
                }
            }
        }
        if (!region.else_creation_code.empty()) {
            ss << region.else_creation_code;
        }
        
        ss << "        }\n";
        if (!event_handlers.empty()) {
            ss << "        _rebind();\n";
        }
        ss << "    }\n";
    }

    // Build child updates map
    std::map<std::string, std::vector<std::string>> child_updates;
    std::map<std::string, int> update_counters;
    for(auto& root : render_roots) {
        collect_child_updates(root.get(), child_updates, update_counters);
    }

    // Helper lambda for method generation
    auto generate_method = [&](FunctionDef& method) {
        std::set<std::string> modified_vars;
        method.collect_modifications(modified_vars);
        
        std::string updates;
        bool is_init_method = (method.name == "init");
        for(const auto& mod : modified_vars) {
            if(generated_updaters.count(mod) && !is_init_method) {
                updates += "        _update_" + mod + "();\n";
            }
            if(child_updates.count(mod) && !is_init_method) {
                for(const auto& call : child_updates[mod]) {
                    updates += call;
                }
            }
            if(var_to_if_ids.count(mod) && !is_init_method) {
                for(int if_id : var_to_if_ids[mod]) {
                    updates += "        _sync_if_" + std::to_string(if_id) + "();\n";
                }
            }
            if(var_to_loop_ids.count(mod) && !is_init_method) {
                for(int loop_id : var_to_loop_ids[mod]) {
                    updates += "        _sync_loop_" + std::to_string(loop_id) + "();\n";
                }
            }
        }
        
        for(const auto& mod : modified_vars) {
            if(g_ref_props.count(mod)) {
                std::string callback_name = "on" + std::string(1, std::toupper(mod[0])) + mod.substr(1) + "Change";
                updates += "        if(" + callback_name + ") " + callback_name + "();\n";
            }
        }
        
        std::string original_name = method.name;
        if (method.name == "tick") {
            method.name = "_user_tick";
        } else if (method.name == "init") {
            method.name = "_user_init";
        } else if (method.name == "mount") {
            method.name = "_user_mount";
        }
        ss << "    " << method.to_webcc(updates);
        if (original_name == "tick" || original_name == "init" || original_name == "mount") {
            method.name = original_name;
        }
    };

    // All methods
    for(auto& method : methods){
        generate_method(method);
    }

    // Event handlers
    for(auto& handler : event_handlers) {
        if (handler.event_type == "click") {
            ss << "    void _handler_" << handler.element_id << "_click() {\n";
            if (handler.is_function_call) {
                ss << "        " << handler.handler_code << ";\n";
            } else {
                ss << "        " << handler.handler_code << "();\n";
            }
            ss << "    }\n";
        } else if (handler.event_type == "input" || handler.event_type == "change") {
            ss << "    void _handler_" << handler.element_id << "_" << handler.event_type << "(const webcc::string& _value) {\n";
            if (handler.is_function_call) {
                ss << "        " << handler.handler_code << ";\n";
            } else {
                ss << "        " << handler.handler_code << "(_value);\n";
            }
            ss << "    }\n";
        } else if (handler.event_type == "keydown") {
            ss << "    void _handler_" << handler.element_id << "_keydown(int _keycode) {\n";
            if (handler.is_function_call) {
                ss << "        " << handler.handler_code << ";\n";
            } else {
                ss << "        " << handler.handler_code << "(_keycode);\n";
            }
            ss << "    }\n";
        }
    }

    // View method
    ss << "    void view(webcc::handle parent = webcc::dom::get_body()) {\n";
    bool has_init = false;
    bool has_mount = false;
    for(auto& m : methods) {
        if(m.name == "init") has_init = true;
        if(m.name == "mount") has_mount = true;
    }
    if(has_init) ss << "        _user_init();\n";
    if(!render_roots.empty()){
        ss << ss_render.str();
    }
    // Register event handlers
    if (click_mask) {
        ss << "        for (int i = 0; i < " << element_count << "; i++) if (_click_mask & (1ULL << i)) g_dispatcher.set(el[i], [this, i]() {\n";
        ss << "            switch(i) {\n";
        for (const auto& handler : event_handlers) {
            if (handler.event_type == "click") {
                ss << "                case " << handler.element_id << ": _handler_" << handler.element_id << "_click(); break;\n";
            }
        }
        ss << "            }\n";
        ss << "        });\n";
    }
    if (input_mask) {
        ss << "        for (int i = 0; i < " << element_count << "; i++) if (_input_mask & (1ULL << i)) g_input_dispatcher.set(el[i], [this, i](const webcc::string& v) {\n";
        ss << "            switch(i) {\n";
        for (const auto& handler : event_handlers) {
            if (handler.event_type == "input") {
                ss << "                case " << handler.element_id << ": _handler_" << handler.element_id << "_input(v); break;\n";
            }
        }
        ss << "            }\n";
        ss << "        });\n";
    }
    if (change_mask) {
        ss << "        for (int i = 0; i < " << element_count << "; i++) if (_change_mask & (1ULL << i)) g_change_dispatcher.set(el[i], [this, i](const webcc::string& v) {\n";
        ss << "            switch(i) {\n";
        for (const auto& handler : event_handlers) {
            if (handler.event_type == "change") {
                ss << "                case " << handler.element_id << ": _handler_" << handler.element_id << "_change(v); break;\n";
            }
        }
        ss << "            }\n";
        ss << "        });\n";
    }
    if (keydown_mask) {
        ss << "        for (int i = 0; i < " << element_count << "; i++) if (_keydown_mask & (1ULL << i)) g_keydown_dispatcher.set(el[i], [this, i](int k) {\n";
        ss << "            switch(i) {\n";
        for (const auto& handler : event_handlers) {
            if (handler.event_type == "keydown") {
                ss << "                case " << handler.element_id << ": _handler_" << handler.element_id << "_keydown(k); break;\n";
            }
        }
        ss << "            }\n";
        ss << "        });\n";
    }
    
    // Wire up onChange callbacks for child component pub mut members
    for(const auto& region : if_regions) {
        for(const auto& mem_dep : region.member_dependencies) {
            std::string callback_name = "on" + std::string(1, std::toupper(mem_dep.member[0])) + mem_dep.member.substr(1) + "Change";
            ss << "        " << mem_dep.object << "." << callback_name << " = [this]() { _sync_if_" << region.if_id << "(); };\n";
        }
    }
    
    if(has_mount) ss << "        _user_mount();\n";
    ss << "    }\n";
    
    // Rebind method
    if (!event_handlers.empty()) {
        ss << "    void _rebind() {\n";
        if (click_mask) {
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_click_mask & (1ULL << i)) g_dispatcher.set(el[i], [this, i]() {\n";
            ss << "            switch(i) {\n";
            for (const auto& handler : event_handlers) {
                if (handler.event_type == "click") {
                    ss << "                case " << handler.element_id << ": _handler_" << handler.element_id << "_click(); break;\n";
                }
            }
            ss << "            }\n";
            ss << "        });\n";
        }
        if (input_mask) {
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_input_mask & (1ULL << i)) g_input_dispatcher.set(el[i], [this, i](const webcc::string& v) {\n";
            ss << "            switch(i) {\n";
            for (const auto& handler : event_handlers) {
                if (handler.event_type == "input") {
                    ss << "                case " << handler.element_id << ": _handler_" << handler.element_id << "_input(v); break;\n";
                }
            }
            ss << "            }\n";
            ss << "        });\n";
        }
        if (change_mask) {
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_change_mask & (1ULL << i)) g_change_dispatcher.set(el[i], [this, i](const webcc::string& v) {\n";
            ss << "            switch(i) {\n";
            for (const auto& handler : event_handlers) {
                if (handler.event_type == "change") {
                    ss << "                case " << handler.element_id << ": _handler_" << handler.element_id << "_change(v); break;\n";
                }
            }
            ss << "            }\n";
            ss << "        });\n";
        }
        if (keydown_mask) {
            ss << "        for (int i = 0; i < " << element_count << "; i++) if (_keydown_mask & (1ULL << i)) g_keydown_dispatcher.set(el[i], [this, i](int k) {\n";
            ss << "            switch(i) {\n";
            for (const auto& handler : event_handlers) {
                if (handler.event_type == "keydown") {
                    ss << "                case " << handler.element_id << ": _handler_" << handler.element_id << "_keydown(k); break;\n";
                }
            }
            ss << "            }\n";
            ss << "        });\n";
        }
        ss << "    }\n";
    }
    
    // Destroy method
    ss << "    void _destroy() {\n";
    if (click_mask) ss << "        for (int i = 0; i < " << element_count << "; i++) if (_click_mask & (1ULL << i)) g_dispatcher.remove(el[i]);\n";
    if (input_mask) ss << "        for (int i = 0; i < " << element_count << "; i++) if (_input_mask & (1ULL << i)) g_input_dispatcher.remove(el[i]);\n";
    if (change_mask) ss << "        for (int i = 0; i < " << element_count << "; i++) if (_change_mask & (1ULL << i)) g_change_dispatcher.remove(el[i]);\n";
    if (keydown_mask) ss << "        for (int i = 0; i < " << element_count << "; i++) if (_keydown_mask & (1ULL << i)) g_keydown_dispatcher.remove(el[i]);\n";
    if (element_count > 0) {
        ss << "        webcc::dom::remove_element(el[0]);\n";
    }
    ss << "    }\n";

    // Tick method
    bool has_user_tick = false;
    for(auto& m : methods) if(m.name == "tick") has_user_tick = true;
    
    bool has_child_with_tick = false;
    for(auto const& [comp_name, count] : component_members) {
        if(session.components_with_tick.count(comp_name)) {
            has_child_with_tick = true;
            break;
        }
    }
    
    bool needs_tick = has_user_tick || has_child_with_tick;
    if(needs_tick) {
        session.components_with_tick.insert(name);
        ss << "    void tick(float dt) {\n";
        
        if(has_user_tick) ss << "        _user_tick(dt);\n";

        for(auto const& [comp_name, count] : component_members) {
            if(session.components_with_tick.count(comp_name)) {
                for(int i=0; i<count; ++i) {
                    ss << "        " << comp_name << "_" << i << ".tick(dt);\n";
                }
            }
        }
        ss << "    }\n";
    }

    ss << "};\n";

    g_ref_props.clear();
    
    return ss.str();
}
