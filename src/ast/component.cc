#include "component.h"
#include "formatter.h"
#include "../def_parser.h"
#include <cctype>
#include <algorithm>
#include <sstream>

// Per-component context for tracking reference props
std::set<std::string> g_ref_props;

// Info for inlining DOM operations on component arrays used in for-each loops
struct ComponentArrayLoopInfo
{
    int loop_id;
    std::string component_type;
    std::string parent_var;         // e.g., "_loop_0_parent"
    std::string var_name;           // Loop variable name (e.g., "row")
    std::string item_creation_code; // Code to render one item
    bool is_member_ref_loop;        // True if <varName/> syntax is used
    bool is_only_child;             // True if loop is only child of parent element
};
std::map<std::string, ComponentArrayLoopInfo> g_component_array_loops;

// ============================================================================
// Utility Functions
// ============================================================================

// Generate callback name from variable name (e.g., "count" -> "onCountChange")
static std::string make_callback_name(const std::string &var_name)
{
    return "on" + std::string(1, std::toupper(var_name[0])) + var_name.substr(1) + "Change";
}

// Transform append_child calls to insert_before for anchor-based regions
// Transforms: webcc::dom::append_child(parent_var, el[N]);
// To:         webcc::dom::insert_before(parent_var, el[N], anchor_var);
static std::string transform_to_insert_before(const std::string &code, const std::string &parent_var, const std::string &anchor_var)
{
    std::string result;
    std::string search_pattern = "webcc::dom::append_child(" + parent_var + ", ";
    size_t pos = 0;
    size_t last_pos = 0;

    while ((pos = code.find(search_pattern, last_pos)) != std::string::npos)
    {
        result += code.substr(last_pos, pos - last_pos);

        size_t end_pos = code.find(");", pos);
        if (end_pos == std::string::npos)
        {
            result += code.substr(pos);
            return result;
        }

        size_t elem_start = pos + search_pattern.length();
        std::string elem = code.substr(elem_start, end_pos - elem_start);

        result += "webcc::dom::insert_before(" + parent_var + ", " + elem + ", " + anchor_var + ");";
        last_pos = end_pos + 2;
    }

    result += code.substr(last_pos);
    return result;
}

// Trim whitespace from both ends of a string
static void trim(std::string &s)
{
    while (!s.empty() && s.front() == ' ')
        s.erase(0, 1);
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
}

// Parse comma-separated arguments respecting parentheses depth
static std::vector<std::string> parse_concat_args(const std::string &args_str)
{
    std::vector<std::string> args;
    int paren_depth = 0;
    bool in_string = false;
    std::string current;

    for (size_t i = 0; i < args_str.size(); ++i)
    {
        char c = args_str[i];
        // Track string literals (handle escaped quotes)
        if (c == '"' && (i == 0 || args_str[i - 1] != '\\'))
        {
            in_string = !in_string;
        }
        if (!in_string)
        {
            if (c == '(')
                paren_depth++;
            else if (c == ')')
                paren_depth--;
            else if (c == ',' && paren_depth == 0)
            {
                trim(current);
                if (!current.empty())
                    args.push_back(current);
                current.clear();
                continue;
            }
        }
        current += c;
    }
    trim(current);
    if (!current.empty())
        args.push_back(current);

    return args;
}

// Indent a multi-line code block
static std::string indent_code(const std::string &code, const std::string &prefix = "        ")
{
    std::stringstream indented;
    std::istringstream iss(code);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty())
        {
            indented << prefix << line << "\n";
        }
    }
    return indented.str();
}

// ============================================================================
// Event Handler Bitmask Helpers
// ============================================================================

struct EventMasks
{
    uint64_t click = 0;
    uint64_t input = 0;
    uint64_t change = 0;
    uint64_t keydown = 0;
};

static EventMasks compute_event_masks(const std::vector<EventHandler> &handlers)
{
    EventMasks masks;
    for (const auto &handler : handlers)
    {
        if (handler.element_id < 64)
        {
            uint64_t bit = 1ULL << handler.element_id;
            if (handler.event_type == "click")
                masks.click |= bit;
            else if (handler.event_type == "input")
                masks.input |= bit;
            else if (handler.event_type == "change")
                masks.change |= bit;
            else if (handler.event_type == "keydown")
                masks.keydown |= bit;
        }
    }
    return masks;
}

static std::set<int> get_elements_for_event(const std::vector<EventHandler> &handlers, const std::string &event_type)
{
    std::set<int> elements;
    for (const auto &handler : handlers)
    {
        if (handler.event_type == event_type)
        {
            elements.insert(handler.element_id);
        }
    }
    return elements;
}

// ============================================================================
// Code Generation Helpers
// ============================================================================

static void emit_event_mask_constants(std::stringstream &ss, const EventMasks &masks)
{
    if (masks.click)
        ss << "    static constexpr uint64_t _click_mask = 0x" << std::hex << masks.click << std::dec << "ULL;\n";
    if (masks.input)
        ss << "    static constexpr uint64_t _input_mask = 0x" << std::hex << masks.input << std::dec << "ULL;\n";
    if (masks.change)
        ss << "    static constexpr uint64_t _change_mask = 0x" << std::hex << masks.change << std::dec << "ULL;\n";
    if (masks.keydown)
        ss << "    static constexpr uint64_t _keydown_mask = 0x" << std::hex << masks.keydown << std::dec << "ULL;\n";
}

static void emit_component_members(std::stringstream &ss, const std::map<std::string, int> &component_members)
{
    for (const auto &[comp_name, count] : component_members)
    {
        for (int i = 0; i < count; ++i)
        {
            ss << "    " << comp_name << " " << comp_name << "_" << i << ";\n";
        }
    }
}

static void emit_loop_vector_members(std::stringstream &ss, const std::set<std::string> &loop_component_types)
{
    for (const auto &comp_name : loop_component_types)
    {
        ss << "    webcc::vector<" << comp_name << "> _loop_" << comp_name << "s;\n";
    }
}

static void emit_loop_region_members(std::stringstream &ss, const std::vector<LoopRegion> &loop_regions)
{
    for (const auto &region : loop_regions)
    {
        ss << "    webcc::handle _loop_" << region.loop_id << "_parent;\n";
        if (region.is_keyed)
        {
            // Simple count tracking - no map needed for inline sync
            ss << "    int _loop_" << region.loop_id << "_count = 0;\n";
        }
        else
        {
            ss << "    int _loop_" << region.loop_id << "_count = 0;\n";
        }
        if (region.is_html_loop)
        {
            ss << "    webcc::vector<webcc::handle> _loop_" << region.loop_id << "_elements;\n";
        }
    }
}

static void emit_if_region_members(std::stringstream &ss, const std::vector<IfRegion> &if_regions)
{
    for (const auto &region : if_regions)
    {
        ss << "    webcc::handle _if_" << region.if_id << "_parent;\n";
        ss << "    webcc::handle _if_" << region.if_id << "_anchor;\n";
        ss << "    bool _if_" << region.if_id << "_state = false;\n";
    }
}

// Generate event handler switch cases for a specific event type
static void emit_handler_switch_cases(std::stringstream &ss,
                                      const std::vector<EventHandler> &handlers,
                                      const std::string &event_type,
                                      const std::string &suffix = "")
{
    for (const auto &handler : handlers)
    {
        if (handler.event_type == event_type)
        {
            ss << "                case " << handler.element_id << ": _handler_"
               << handler.element_id << "_" << event_type << "(" << suffix << "); break;\n";
        }
    }
}

// Generate event dispatcher registration for a specific event type
static void emit_event_registration(std::stringstream &ss,
                                    int element_count,
                                    const std::vector<EventHandler> &handlers,
                                    const std::string &event_type,
                                    const std::string &mask_name,
                                    const std::string &dispatcher_name,
                                    const std::string &lambda_params,
                                    const std::string &call_suffix)
{
    ss << "        for (int i = 0; i < " << element_count << "; i++) if ((" << mask_name
       << " & (1ULL << i)) && el[i].is_valid()) " << dispatcher_name << ".set(el[i], [this, i](" << lambda_params << ") {\n";
    ss << "            switch(i) {\n";
    emit_handler_switch_cases(ss, handlers, event_type, call_suffix);
    ss << "            }\n";
    ss << "        });\n";
}

// ============================================================================
// Tree Traversal Functions
// ============================================================================

void Component::collect_child_components(ASTNode *node, std::map<std::string, int> &counts)
{
    if (auto comp = dynamic_cast<ComponentInstantiation *>(node))
    {
        // Don't count member references - they're already declared as member variables
        if (!comp->is_member_reference)
        {
            counts[comp->component_name]++;
        }
    }
    if (auto el = dynamic_cast<HTMLElement *>(node))
    {
        for (auto &child : el->children)
        {
            collect_child_components(child.get(), counts);
        }
    }
    if (auto viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (auto &child : viewIf->then_children)
        {
            collect_child_components(child.get(), counts);
        }
        for (auto &child : viewIf->else_children)
        {
            collect_child_components(child.get(), counts);
        }
    }
}

// Collect component types used inside for loops
static void collect_loop_components(ASTNode *node, std::set<std::string> &loop_components, bool in_loop = false)
{
    if (auto comp = dynamic_cast<ComponentInstantiation *>(node))
    {
        // Don't collect member references - they're already declared as member variables
        if (in_loop && !comp->is_member_reference)
        {
            loop_components.insert(comp->component_name);
        }
    }
    if (auto el = dynamic_cast<HTMLElement *>(node))
    {
        for (auto &child : el->children)
        {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
    }
    if (auto viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (auto &child : viewIf->then_children)
        {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
        for (auto &child : viewIf->else_children)
        {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
    }
    if (auto viewFor = dynamic_cast<ViewForRangeStatement *>(node))
    {
        for (auto &child : viewFor->children)
        {
            collect_loop_components(child.get(), loop_components, true);
        }
    }
    if (auto viewForEach = dynamic_cast<ViewForEachStatement *>(node))
    {
        for (auto &child : viewForEach->children)
        {
            collect_loop_components(child.get(), loop_components, true);
        }
    }
}

void Component::collect_child_updates(ASTNode *node, std::map<std::string, std::vector<std::string>> &updates, std::map<std::string, int> &counters)
{
    if (auto comp = dynamic_cast<ComponentInstantiation *>(node))
    {
        // For member references, use member_name; otherwise construct instance name
        std::string instance_name;
        if (comp->is_member_reference)
        {
            instance_name = comp->member_name;
        }
        else
        {
            instance_name = comp->component_name + "_" + std::to_string(counters[comp->component_name]++);
        }

        for (const auto &prop : comp->props)
        {
            if (prop.is_reference)
            {
                std::set<std::string> deps;
                prop.value->collect_dependencies(deps);
                for (const auto &dep : deps)
                {
                    updates[dep].push_back("        " + instance_name + "._update_" + prop.name + "();\n");
                }
            }
        }
    }
    if (auto el = dynamic_cast<HTMLElement *>(node))
    {
        for (auto &child : el->children)
        {
            collect_child_updates(child.get(), updates, counters);
        }
    }
    if (auto viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (auto &child : viewIf->then_children)
        {
            collect_child_updates(child.get(), updates, counters);
        }
        for (auto &child : viewIf->else_children)
        {
            collect_child_updates(child.get(), updates, counters);
        }
    }
}

std::string Component::to_webcc(CompilerSession &session)
{
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
    for (auto &param : params)
    {
        if (param->is_reference)
        {
            g_ref_props.insert(param->name);
        }
    }

    // Collect child components
    for (auto &root : render_roots)
    {
        collect_child_components(root.get(), component_members);
        collect_loop_components(root.get(), loop_component_types);
    }

    // Collect method names
    std::set<std::string> method_names;
    for (auto &m : methods)
        method_names.insert(m.name);

    // Track pub mut state variables
    std::set<std::string> pub_mut_vars;
    for (auto &var : state)
    {
        if (var->is_public && var->is_mutable)
        {
            pub_mut_vars.insert(var->name);
        }
    }

    // Track pub mut params (for parent notification callbacks)
    std::set<std::string> pub_mut_params;
    for (auto &param : params)
    {
        if (param->is_public && param->is_mutable)
        {
            pub_mut_params.insert(param->name);
        }
    }

    std::stringstream ss_render;
    for (auto &root : render_roots)
    {
        if (auto el = dynamic_cast<HTMLElement *>(root.get()))
        {
            el->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        }
        else if (auto comp = dynamic_cast<ComponentInstantiation *>(root.get()))
        {
            comp->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        }
        else if (auto viewIf = dynamic_cast<ViewIfStatement *>(root.get()))
        {
            viewIf->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        }
        else if (auto viewFor = dynamic_cast<ViewForRangeStatement *>(root.get()))
        {
            viewFor->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        }
        else if (auto viewForEach = dynamic_cast<ViewForEachStatement *>(root.get()))
        {
            viewForEach->generate_code(ss_render, "parent", element_count, event_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        }
    }

    // Populate global context for component array loops (for inline DOM operations)
    g_component_array_loops.clear();
    for (const auto &region : loop_regions)
    {
        if (region.is_keyed && region.is_member_ref_loop)
        {
            ComponentArrayLoopInfo info;
            info.loop_id = region.loop_id;
            info.component_type = region.component_type;
            info.parent_var = "_loop_" + std::to_string(region.loop_id) + "_parent";
            info.var_name = region.var_name;
            info.item_creation_code = region.item_creation_code;
            info.is_member_ref_loop = true;
            info.is_only_child = region.is_only_child;
            g_component_array_loops[region.iterable_expr] = info;
        }
    }

    // Generate component as a struct
    ss << "struct " << name << " {\n";

    // Structs
    for (auto &s : structs)
    {
        ss << s->to_webcc() << "\n";
    }

    // Enums
    for (auto &e : enums)
    {
        ss << e->to_webcc() << "\n";
    }

    // Component parameters (data members only - callbacks emitted later for proper aggregate init order)
    for (auto &param : params)
    {
        ss << "    " << convert_type(param->type);
        if (param->is_reference)
        {
            ss << "* " << param->name << " = nullptr";
        }
        else
        {
            ss << " " << param->name;
            if (param->default_value)
            {
                ss << " = " << param->default_value->to_webcc();
            }
        }
        ss << ";\n";
    }

    // State variables (data members only - callbacks emitted later)
    for (auto &var : state)
    {
        // Special handling for array literals
        if (auto arr_lit = dynamic_cast<ArrayLiteral *>(var->initializer.get()))
        {
            if (var->type.ends_with("[]"))
            {
                std::string elem_type = var->type.substr(0, var->type.length() - 2);
                size_t count = arr_lit->elements.size();

                if (var->is_mutable)
                {
                    // Mutable: use vector with brace init (variadic constructor)
                    std::string vec_type = "webcc::vector<" + convert_type(elem_type) + ">";
                    ss << "    " << vec_type;
                    if (var->is_reference)
                        ss << "&";
                    ss << " " << var->name << " = " << arr_lit->to_webcc() << ";\n";
                }
                else
                {
                    // Const: use fixed-size array (more efficient, size known at compile time)
                    std::string arr_type = "webcc::array<" + convert_type(elem_type) + ", " + std::to_string(count) + ">";
                    ss << "    const " << arr_type << " " << var->name << " = " << arr_lit->to_webcc() << ";\n";
                }
                continue;
            }
        }

        ss << "    " << (var->is_mutable ? "" : "const ") << convert_type(var->type);
        if (var->is_reference)
            ss << "&";
        ss << " " << var->name;
        if (var->initializer)
        {
            if (DefSchema::instance().is_handle(var->type))
            {
                ss << "{" << var->initializer->to_webcc() << "}";
            }
            else
            {
                ss << " = " << var->initializer->to_webcc();
            }
        }
        ss << ";\n";
    }

    // Reactivity callbacks for params (emitted after all data members for proper aggregate init)
    for (auto &param : params)
    {
        // Generate callback for reference mut params
        if (param->is_reference && param->is_mutable)
        {
            ss << "    webcc::function<void()> " << make_callback_name(param->name) << ";\n";
        }
        // Generate callback for pub mut params (for parent-child reactivity)
        else if (param->is_public && param->is_mutable)
        {
            ss << "    webcc::function<void()> " << make_callback_name(param->name) << ";\n";
        }
    }

    // Reactivity callbacks for state variables
    for (auto &var : state)
    {
        // Skip array literals that were already handled
        if (auto arr_lit = dynamic_cast<ArrayLiteral *>(var->initializer.get()))
        {
            if (var->type.ends_with("[]"))
            {
                if (var->is_mutable && var->is_public)
                {
                    ss << "    webcc::function<void()> " << make_callback_name(var->name) << ";\n";
                }
                continue;
            }
        }

        if (var->is_public && var->is_mutable)
        {
            ss << "    webcc::function<void()> " << make_callback_name(var->name) << ";\n";
        }
    }

    // Element handles
    if (element_count > 0)
    {
        ss << "    webcc::handle el[" << element_count << "];\n";
    }

    // Event handler bitmasks
    EventMasks masks = compute_event_masks(event_handlers);
    emit_event_mask_constants(ss, masks);

    // Child component members
    emit_component_members(ss, component_members);

    // Vector members for components in loops
    emit_loop_vector_members(ss, loop_component_types);

    // Loop region tracking
    emit_loop_region_members(ss, loop_regions);

    // If region tracking
    emit_if_region_members(ss, if_regions);

    // Build update entries map
    struct UpdateEntry
    {
        std::string code;
        int if_region_id;
        bool in_then_branch;
    };
    std::map<std::string, std::vector<UpdateEntry>> var_update_entries;

    // Group bindings by element+attribute to generate shared update methods
    struct ElementAttrKey
    {
        int element_id;
        std::string type;  // "attr" or "text"
        std::string name;  // attribute name (or "" for text)
        int if_region_id;
        bool in_then_branch;

        bool operator<(const ElementAttrKey &other) const
        {
            if (element_id != other.element_id) return element_id < other.element_id;
            if (type != other.type) return type < other.type;
            if (name != other.name) return name < other.name;
            if (if_region_id != other.if_region_id) return if_region_id < other.if_region_id;
            return in_then_branch < other.in_then_branch;
        }
    };

    struct ElementAttrBinding
    {
        std::string update_code;
        std::set<std::string> dependencies;
        std::string method_name;
    };

    std::map<ElementAttrKey, ElementAttrBinding> element_attr_bindings;

    // Collect bindings grouped by element+attribute
    for (const auto &binding : bindings)
    {
        ElementAttrKey key;
        key.element_id = binding.element_id;
        key.type = binding.type;
        key.name = binding.name;
        key.if_region_id = binding.if_region_id;
        key.in_then_branch = binding.in_then_branch;

        std::string el_var = "el[" + std::to_string(binding.element_id) + "]";
        std::string update_line;
        std::string dom_call = (binding.type == "attr")
                                   ? "webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", "
                                   : "webcc::dom::set_inner_text(" + el_var + ", ";

        bool optimized = false;
        if (binding.expr)
        {
            if (auto strLit = dynamic_cast<StringLiteral *>(binding.expr))
            {
                update_line = generate_formatter_block_from_string_literal(strLit, dom_call);
                optimized = true;
            }
        }

        if (!optimized && binding.value_code.find("webcc::string::concat(") == 0)
        {
            std::string args_str = binding.value_code.substr(22);
            if (!args_str.empty() && args_str.back() == ')')
                args_str.pop_back();

            std::vector<std::string> args = parse_concat_args(args_str);
            update_line = generate_formatter_block(args, dom_call);
            optimized = true;
        }

        if (!optimized)
        {
            bool is_string_literal = !binding.value_code.empty() && binding.value_code.front() == '"';
            if (is_string_literal)
            {
                update_line = dom_call + binding.value_code + ");";
            }
            else
            {
                update_line = generate_formatter_block({binding.value_code}, dom_call);
            }
        }

        if (!update_line.empty())
        {
            element_attr_bindings[key].update_code = update_line;
            for (const auto &dep : binding.dependencies)
            {
                element_attr_bindings[key].dependencies.insert(dep);
            }
        }
    }

    // Generate shared element+attribute update methods
    int shared_update_counter = 0;
    for (auto &[key, binding] : element_attr_bindings)
    {
        std::string method_name;
        if (key.type == "attr" && !key.name.empty())
        {
            method_name = "_update_el" + std::to_string(key.element_id) + "_" + key.name;
        }
        else if (key.type == "text")
        {
            method_name = "_update_el" + std::to_string(key.element_id) + "_text";
        }
        else
        {
            method_name = "_update_shared_" + std::to_string(shared_update_counter++);
        }

        binding.method_name = method_name;

        // Add this shared method to each dependency's update list
        for (const auto &dep : binding.dependencies)
        {
            UpdateEntry entry;
            entry.code = method_name + "();";
            entry.if_region_id = key.if_region_id;
            entry.in_then_branch = key.in_then_branch;
            var_update_entries[dep].push_back(entry);
        }
    }

    // Generate shared element+attribute update methods first
    for (const auto &[key, binding] : element_attr_bindings)
    {
        ss << "    void " << binding.method_name << "() {\n";
        if (key.if_region_id < 0)
        {
            ss << "        " << binding.update_code << "\n";
        }
        else
        {
            if (key.in_then_branch)
            {
                ss << "        if (_if_" << key.if_region_id << "_state) {\n";
                ss << "            " << binding.update_code << "\n";
                ss << "        }\n";
            }
            else
            {
                ss << "        if (!_if_" << key.if_region_id << "_state) {\n";
                ss << "            " << binding.update_code << "\n";
                ss << "        }\n";
            }
        }
        ss << "    }\n";
    }

    // Generate _update_{varname}() methods
    std::set<std::string> generated_updaters;
    for (const auto &[var_name, entries] : var_update_entries)
    {
        if (!entries.empty())
        {
            ss << "    void _update_" << var_name << "() {\n";

            // Deduplicate entries outside if regions
            std::set<std::string> non_if_calls;
            for (const auto &entry : entries)
            {
                if (entry.if_region_id < 0)
                {
                    non_if_calls.insert(entry.code);
                }
            }
            for (const auto &code : non_if_calls)
            {
                ss << "        " << code << "\n";
            }

            std::map<int, std::pair<std::set<std::string>, std::set<std::string>>> if_grouped;
            for (const auto &entry : entries)
            {
                if (entry.if_region_id >= 0)
                {
                    if (entry.in_then_branch)
                    {
                        if_grouped[entry.if_region_id].first.insert(entry.code);
                    }
                    else
                    {
                        if_grouped[entry.if_region_id].second.insert(entry.code);
                    }
                }
            }

            for (const auto &[if_id, branches] : if_grouped)
            {
                const auto &then_codes = branches.first;
                const auto &else_codes = branches.second;

                if (!then_codes.empty() && !else_codes.empty())
                {
                    ss << "        if (_if_" << if_id << "_state) {\n";
                    for (const auto &code : then_codes)
                    {
                        ss << "            " << code << "\n";
                    }
                    ss << "        } else {\n";
                    for (const auto &code : else_codes)
                    {
                        ss << "            " << code << "\n";
                    }
                    ss << "        }\n";
                }
                else if (!then_codes.empty())
                {
                    ss << "        if (_if_" << if_id << "_state) {\n";
                    for (const auto &code : then_codes)
                    {
                        ss << "            " << code << "\n";
                    }
                    ss << "        }\n";
                }
                else if (!else_codes.empty())
                {
                    ss << "        if (!_if_" << if_id << "_state) {\n";
                    for (const auto &code : else_codes)
                    {
                        ss << "            " << code << "\n";
                    }
                    ss << "        }\n";
                }
            }

            // Call callback for pub mut state vars
            if (pub_mut_vars.count(var_name))
            {
                std::string callback_name = make_callback_name(var_name);
                ss << "        if(" << callback_name << ") " << callback_name << "();\n";
            }
            // Call callback for pub mut params
            if (pub_mut_params.count(var_name))
            {
                std::string callback_name = make_callback_name(var_name);
                ss << "        if(" << callback_name << ") " << callback_name << "();\n";
            }
            ss << "    }\n";
            generated_updaters.insert(var_name);
        }
    }

    // Generate _update methods for pub mut variables without UI bindings
    for (const auto &var_name : pub_mut_vars)
    {
        if (generated_updaters.find(var_name) == generated_updaters.end())
        {
            std::string callback_name = make_callback_name(var_name);
            ss << "    void _update_" << var_name << "() {\n";
            ss << "        if(" << callback_name << ") " << callback_name << "();\n";
            ss << "    }\n";
            generated_updaters.insert(var_name);
        }
    }

    // Generate _update methods for pub mut params without UI bindings
    for (const auto &var_name : pub_mut_params)
    {
        if (generated_updaters.find(var_name) == generated_updaters.end())
        {
            std::string callback_name = make_callback_name(var_name);
            ss << "    void _update_" << var_name << "() {\n";
            ss << "        if(" << callback_name << ") " << callback_name << "();\n";
            ss << "    }\n";
            generated_updaters.insert(var_name);
        }
    }

    // Ensure all params have update method
    for (const auto &param : params)
    {
        if (generated_updaters.find(param->name) == generated_updaters.end())
        {
            ss << "    void _update_" << param->name << "() {}\n";
            generated_updaters.insert(param->name);
        }
    }

    // Map from variable to loop IDs
    std::map<std::string, std::vector<int>> var_to_loop_ids;
    for (const auto &region : loop_regions)
    {
        for (const auto &dep : region.dependencies)
        {
            var_to_loop_ids[dep].push_back(region.loop_id);
        }
    }

    // Generate _sync_loop_X() methods
    for (const auto &region : loop_regions)
    {
        ss << "    void _sync_loop_" << region.loop_id << "() {\n";

        if (region.is_keyed)
        {
            std::string vec_name = region.is_member_ref_loop ? region.iterable_expr : ("_loop_" + region.component_type + "s");
            std::string count_var = "_loop_" + std::to_string(region.loop_id) + "_count";
            std::string parent_var = "_loop_" + std::to_string(region.loop_id) + "_parent";

            ss << "        int _new_count = (int)" << vec_name << ".size();\n";

            // Clear existing views - MUST call _remove_view() to unregister event handlers from dispatchers
            ss << "        if (" << count_var << " > 0) {\n";
            ss << "            for (int _i = 0; _i < " << count_var << "; _i++) {\n";
            ss << "                " << vec_name << "[_i]._remove_view();\n";
            ss << "            }\n";
            ss << "        }\n";
            ss << "        \n";

            // Recreate all items in current array order with fresh views
            ss << "        g_view_depth++;\n";
            ss << "        for (auto& " << region.var_name << " : " << region.iterable_expr << ") {\n";

            std::string item_code = region.item_creation_code;
            std::stringstream indented;
            std::istringstream iss(item_code);
            std::string line;
            while (std::getline(iss, line))
            {
                if (!line.empty())
                {
                    indented << "        " << line << "\n";
                }
            }
            ss << indented.str();

            ss << "        }\n";
            ss << "        if (--g_view_depth == 0) webcc::flush();\n";
            ss << "        " << count_var << " = _new_count;\n";
        }
        else
        {
            ss << "        int new_count = " << region.end_expr << " - " << region.start_expr << ";\n";
            ss << "        int old_count = _loop_" << region.loop_id << "_count;\n";
            ss << "        if (new_count == old_count) return;\n";
            ss << "        \n";

            if (!region.component_type.empty())
            {
                std::string vec_name = "_loop_" + region.component_type + "s";

                ss << "        if (new_count > old_count) {\n";
                ss << "            for (int " << region.var_name << " = old_count; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";

                std::string item_code = region.item_creation_code;
                std::stringstream indented;
                std::istringstream iss(item_code);
                std::string line;
                while (std::getline(iss, line))
                {
                    if (!line.empty())
                    {
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

                if (!region.item_update_code.empty())
                {
                    ss << "            for (int " << region.var_name << " = 0; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";
                    ss << region.item_update_code;
                    ss << "            }\n";
                }
                ss << "        }\n";
            }
            else if (region.is_html_loop)
            {
                std::string vec_name = "_loop_" + std::to_string(region.loop_id) + "_elements";

                ss << "        if (new_count > old_count) {\n";
                ss << "            for (int " << region.var_name << " = old_count; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";

                std::string item_code = region.item_creation_code;
                std::stringstream indented;
                std::istringstream iss(item_code);
                std::string line;
                while (std::getline(iss, line))
                {
                    if (!line.empty())
                    {
                        indented << "    " << line << "\n";
                    }
                }
                ss << indented.str();

                if (!region.root_element_var.empty())
                {
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
    for (const auto &region : if_regions)
    {
        for (const auto &dep : region.dependencies)
        {
            var_to_if_ids[dep].push_back(region.if_id);
        }
    }

    // Generate _sync_if_X() methods
    for (const auto &region : if_regions)
    {
        ss << "    void _sync_if_" << region.if_id << "() {\n";
        ss << "        bool new_state = " << region.condition_code << ";\n";
        ss << "        if (new_state == _if_" << region.if_id << "_state) return;\n";
        ss << "        _if_" << region.if_id << "_state = new_state;\n";
        ss << "        \n";

        std::set<int> click_els = get_elements_for_event(event_handlers, "click");
        std::set<int> input_els = get_elements_for_event(event_handlers, "input");
        std::set<int> change_els = get_elements_for_event(event_handlers, "change");
        std::set<int> keydown_els = get_elements_for_event(event_handlers, "keydown");

        ss << "        if (new_state) {\n";
        for (int el_id : region.else_element_ids)
        {
            if (click_els.count(el_id))
                ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
            if (input_els.count(el_id))
                ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
            if (change_els.count(el_id))
                ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
            if (keydown_els.count(el_id))
                ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
        }
        for (int el_id : region.else_element_ids)
        {
            ss << "            webcc::dom::remove_element(el[" << el_id << "]);\n";
        }
        for (const auto &[comp_name, inst_id] : region.else_components)
        {
            ss << "            " << comp_name << "_" << inst_id << "._destroy();\n";
        }
        // Remove view from member references (keeps component state, just removes DOM)
        for (const auto &member_name : region.else_member_refs)
        {
            ss << "            " << member_name << "._remove_view();\n";
        }
        for (int loop_id : region.else_loop_ids)
        {
            for (const auto &lr : loop_regions)
            {
                if (lr.loop_id == loop_id)
                {
                    if (!lr.component_type.empty())
                    {
                        std::string vec_name = "_loop_" + lr.component_type + "s";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    }
                    else if (lr.is_html_loop)
                    {
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
        for (int nested_if_id : region.else_if_ids)
        {
            for (const auto &nested_region : if_regions)
            {
                if (nested_region.if_id == nested_if_id)
                {
                    for (int el_id : nested_region.then_element_ids)
                    {
                        if (click_els.count(el_id))
                            ss << "            if (_if_" << nested_if_id << "_state) g_dispatcher.remove(el[" << el_id << "]);\n";
                        if (input_els.count(el_id))
                            ss << "            if (_if_" << nested_if_id << "_state) g_input_dispatcher.remove(el[" << el_id << "]);\n";
                        if (change_els.count(el_id))
                            ss << "            if (_if_" << nested_if_id << "_state) g_change_dispatcher.remove(el[" << el_id << "]);\n";
                        if (keydown_els.count(el_id))
                            ss << "            if (_if_" << nested_if_id << "_state) g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
                        ss << "            if (_if_" << nested_if_id << "_state) webcc::dom::remove_element(el[" << el_id << "]);\n";
                    }
                    for (int el_id : nested_region.else_element_ids)
                    {
                        if (click_els.count(el_id))
                            ss << "            if (!_if_" << nested_if_id << "_state) g_dispatcher.remove(el[" << el_id << "]);\n";
                        if (input_els.count(el_id))
                            ss << "            if (!_if_" << nested_if_id << "_state) g_input_dispatcher.remove(el[" << el_id << "]);\n";
                        if (change_els.count(el_id))
                            ss << "            if (!_if_" << nested_if_id << "_state) g_change_dispatcher.remove(el[" << el_id << "]);\n";
                        if (keydown_els.count(el_id))
                            ss << "            if (!_if_" << nested_if_id << "_state) g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
                        ss << "            if (!_if_" << nested_if_id << "_state) webcc::dom::remove_element(el[" << el_id << "]);\n";
                    }
                }
            }
        }
        ss << region.then_creation_code;

        ss << "        } else {\n";
        for (int el_id : region.then_element_ids)
        {
            if (click_els.count(el_id))
                ss << "            g_dispatcher.remove(el[" << el_id << "]);\n";
            if (input_els.count(el_id))
                ss << "            g_input_dispatcher.remove(el[" << el_id << "]);\n";
            if (change_els.count(el_id))
                ss << "            g_change_dispatcher.remove(el[" << el_id << "]);\n";
            if (keydown_els.count(el_id))
                ss << "            g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
        }
        for (int el_id : region.then_element_ids)
        {
            ss << "            webcc::dom::remove_element(el[" << el_id << "]);\n";
        }
        for (const auto &[comp_name, inst_id] : region.then_components)
        {
            ss << "            " << comp_name << "_" << inst_id << "._destroy();\n";
        }
        // Remove view from member references (keeps component state, just removes DOM)
        for (const auto &member_name : region.then_member_refs)
        {
            ss << "            " << member_name << "._remove_view();\n";
        }
        for (int loop_id : region.then_loop_ids)
        {
            for (const auto &lr : loop_regions)
            {
                if (lr.loop_id == loop_id)
                {
                    if (!lr.component_type.empty())
                    {
                        std::string vec_name = "_loop_" + lr.component_type + "s";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    }
                    else if (lr.is_html_loop)
                    {
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
        for (int nested_if_id : region.then_if_ids)
        {
            for (const auto &nested_region : if_regions)
            {
                if (nested_region.if_id == nested_if_id)
                {
                    for (int el_id : nested_region.then_element_ids)
                    {
                        if (click_els.count(el_id))
                            ss << "            if (_if_" << nested_if_id << "_state) g_dispatcher.remove(el[" << el_id << "]);\n";
                        if (input_els.count(el_id))
                            ss << "            if (_if_" << nested_if_id << "_state) g_input_dispatcher.remove(el[" << el_id << "]);\n";
                        if (change_els.count(el_id))
                            ss << "            if (_if_" << nested_if_id << "_state) g_change_dispatcher.remove(el[" << el_id << "]);\n";
                        if (keydown_els.count(el_id))
                            ss << "            if (_if_" << nested_if_id << "_state) g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
                        ss << "            if (_if_" << nested_if_id << "_state) webcc::dom::remove_element(el[" << el_id << "]);\n";
                    }
                    for (int el_id : nested_region.else_element_ids)
                    {
                        if (click_els.count(el_id))
                            ss << "            if (!_if_" << nested_if_id << "_state) g_dispatcher.remove(el[" << el_id << "]);\n";
                        if (input_els.count(el_id))
                            ss << "            if (!_if_" << nested_if_id << "_state) g_input_dispatcher.remove(el[" << el_id << "]);\n";
                        if (change_els.count(el_id))
                            ss << "            if (!_if_" << nested_if_id << "_state) g_change_dispatcher.remove(el[" << el_id << "]);\n";
                        if (keydown_els.count(el_id))
                            ss << "            if (!_if_" << nested_if_id << "_state) g_keydown_dispatcher.remove(el[" << el_id << "]);\n";
                        ss << "            if (!_if_" << nested_if_id << "_state) webcc::dom::remove_element(el[" << el_id << "]);\n";
                    }
                }
            }
        }
        if (!region.else_creation_code.empty())
        {
            ss << region.else_creation_code;
        }

        ss << "        }\n";
        if (!event_handlers.empty())
        {
            ss << "        _rebind();\n";
        }
        ss << "    }\n";
    }

    // Build child updates map
    std::map<std::string, std::vector<std::string>> child_updates;
    std::map<std::string, int> update_counters;
    for (auto &root : render_roots)
    {
        collect_child_updates(root.get(), child_updates, update_counters);
    }

    // Helper lambda for method generation
    auto generate_method = [&](FunctionDef &method)
    {
        std::set<std::string> modified_vars;
        method.collect_modifications(modified_vars);

        std::string updates;
        bool is_init_method = (method.name == "init");
        for (const auto &mod : modified_vars)
        {
            if (generated_updaters.count(mod) && !is_init_method)
            {
                updates += "        _update_" + mod + "();\n";
            }
            if (child_updates.count(mod) && !is_init_method)
            {
                for (const auto &call : child_updates[mod])
                {
                    updates += call;
                }
            }
            if (var_to_if_ids.count(mod) && !is_init_method)
            {
                for (int if_id : var_to_if_ids[mod])
                {
                    updates += "        _sync_if_" + std::to_string(if_id) + "();\n";
                }
            }
            if (var_to_loop_ids.count(mod) && !is_init_method)
            {
                // Skip _sync_loop for component arrays with inline operations
                // Those are handled inline in statements (push/pop/clear) or in Assignment (full reassignment)
                if (g_component_array_loops.find(mod) == g_component_array_loops.end())
                {
                    for (int loop_id : var_to_loop_ids[mod])
                    {
                        updates += "        _sync_loop_" + std::to_string(loop_id) + "();\n";
                    }
                }
            }
        }

        for (const auto &mod : modified_vars)
        {
            if (g_ref_props.count(mod))
            {
                std::string callback_name = make_callback_name(mod);
                updates += "        if(" + callback_name + ") " + callback_name + "();\n";
            }
        }

        std::string original_name = method.name;
        if (method.name == "tick")
        {
            method.name = "_user_tick";
        }
        else if (method.name == "init")
        {
            method.name = "_user_init";
        }
        else if (method.name == "mount")
        {
            method.name = "_user_mount";
        }
        ss << "    " << method.to_webcc(updates);
        if (original_name == "tick" || original_name == "init" || original_name == "mount")
        {
            method.name = original_name;
        }
    };

    // All methods
    for (auto &method : methods)
    {
        generate_method(method);
    }

    // Event handlers
    for (auto &handler : event_handlers)
    {
        if (handler.event_type == "click")
        {
            ss << "    void _handler_" << handler.element_id << "_click() {\n";
            if (handler.is_function_call)
            {
                ss << "        " << handler.handler_code << ";\n";
            }
            else
            {
                ss << "        " << handler.handler_code << "();\n";
            }
            ss << "    }\n";
        }
        else if (handler.event_type == "input" || handler.event_type == "change")
        {
            ss << "    void _handler_" << handler.element_id << "_" << handler.event_type << "(const webcc::string& _value) {\n";
            if (handler.is_function_call)
            {
                ss << "        " << handler.handler_code << ";\n";
            }
            else
            {
                ss << "        " << handler.handler_code << "(_value);\n";
            }
            ss << "    }\n";
        }
        else if (handler.event_type == "keydown")
        {
            ss << "    void _handler_" << handler.element_id << "_keydown(int _keycode) {\n";
            if (handler.is_function_call)
            {
                ss << "        " << handler.handler_code << ";\n";
            }
            else
            {
                ss << "        " << handler.handler_code << "(_keycode);\n";
            }
            ss << "    }\n";
        }
    }

    // View method
    ss << "    void view(webcc::handle parent = webcc::dom::get_body()) {\n";
    ss << "        g_view_depth++;\n";

    bool has_init = false;
    bool has_mount = false;
    for (auto &m : methods)
    {
        if (m.name == "init")
            has_init = true;
        if (m.name == "mount")
            has_mount = true;
    }
    if (has_init)
        ss << "        _user_init();\n";
    if (!render_roots.empty())
    {
        ss << ss_render.str();
    }
    // End view - flushes only at outermost level, then register event handlers
    ss << "        if (--g_view_depth == 0) webcc::flush();\n";
    // Register event handlers
    if (masks.click)
    {
        emit_event_registration(ss, element_count, event_handlers, "click", "_click_mask", "g_dispatcher", "", "");
    }
    if (masks.input)
    {
        emit_event_registration(ss, element_count, event_handlers, "input", "_input_mask", "g_input_dispatcher", "const webcc::string& v", "v");
    }
    if (masks.change)
    {
        emit_event_registration(ss, element_count, event_handlers, "change", "_change_mask", "g_change_dispatcher", "const webcc::string& v", "v");
    }
    if (masks.keydown)
    {
        emit_event_registration(ss, element_count, event_handlers, "keydown", "_keydown_mask", "g_keydown_dispatcher", "int k", "k");
    }

    // Wire up onChange callbacks for child component pub mut members
    for (const auto &region : if_regions)
    {
        for (const auto &mem_dep : region.member_dependencies)
        {
            std::string callback_name = make_callback_name(mem_dep.member);
            ss << "        " << mem_dep.object << "." << callback_name << " = [this]() { _sync_if_" << region.if_id << "(); };\n";
        }
    }

    // Wire up nested component reactivity (e.g., Vector.x/y -> Ball._update_x/y)
    for (const auto &param : params)
    {
        // Check if this param is a component type with pub_mut_members
        auto it = session.component_info.find(param->type);
        if (it != session.component_info.end() && !it->second.pub_mut_members.empty())
        {
            // Wire each pub_mut_member's onChange to our _update_{member}() method
            for (const auto &member : it->second.pub_mut_members)
            {
                std::string callback_name = make_callback_name(member);
                ss << "        " << param->name << "." << callback_name << " = [this]() { _update_" << member << "(); };\n";
            }
        }
    }

    if (has_mount)
        ss << "        _user_mount();\n";
    ss << "    }\n";

    // Rebind method (always generated, even if empty, for component array reallocation)
    ss << "    void _rebind() {\n";
    if (!event_handlers.empty())
    {
        if (masks.click)
        {
            emit_event_registration(ss, element_count, event_handlers, "click", "_click_mask", "g_dispatcher", "", "");
        }
        if (masks.input)
        {
            emit_event_registration(ss, element_count, event_handlers, "input", "_input_mask", "g_input_dispatcher", "const webcc::string& v", "v");
        }
        if (masks.change)
        {
            emit_event_registration(ss, element_count, event_handlers, "change", "_change_mask", "g_change_dispatcher", "const webcc::string& v", "v");
        }
        if (masks.keydown)
        {
            emit_event_registration(ss, element_count, event_handlers, "keydown", "_keydown_mask", "g_keydown_dispatcher", "int k", "k");
        }
    }

    // Re-wire nested component reactivity after reallocation
    for (const auto &param : params)
    {
        auto it = session.component_info.find(param->type);
        if (it != session.component_info.end() && !it->second.pub_mut_members.empty())
        {
            for (const auto &member : it->second.pub_mut_members)
            {
                std::string callback_name = make_callback_name(member);
                ss << "        " << param->name << "." << callback_name << " = [this]() { _update_" << member << "(); };\n";
            }
        }
    }

    ss << "    }\n";

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
    for (auto &m : methods)
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
        session.components_with_tick.insert(name);
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

    ss << "};\n";

    g_ref_props.clear();

    return ss.str();
}
