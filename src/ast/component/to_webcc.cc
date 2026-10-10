#include <functional>
#include "component.h"
#include "../codegen_state.h"
#include "../formatter.h"
#include "../../defs/def_parser.h"
#include "../../codegen/codegen_utils.h"
#include <cctype>
#include <algorithm>
#include <sstream>

// ============================================================================
// Utility Functions
// ============================================================================

// Generate callback name from variable name (e.g., "count" -> "onCountChange")
static std::string make_callback_name(const std::string &var_name)
{
    return "on" + std::string(1, std::toupper(var_name[0])) + var_name.substr(1) + "Change";
}

// Transform append_child calls to insert_before for anchor-based regions
// Transforms: webcc::dom::append_child(parent_var, _el[N]);
// To:         webcc::dom::insert_before(parent_var, _el[N], anchor_var);
// Also rewrites child component renders (which append their roots internally)
// to the anchor-aware form: X._view(parent_var); -> X._view(parent_var, anchor_var);
// every node a loop body puts straight into the loop's parent goes on the row list, so the next
// sync removes all of them: a body with several top-level nodes, or an <if> around one, makes more
// than the one root element
static std::string track_top_level_inserts(const std::string &code, const std::string &parent_var, const std::string &vec_name)
{
    std::string result;
    const std::string patterns[2] = {"webcc::dom::append_child(" + parent_var + ", ", "webcc::dom::insert_before(" + parent_var + ", "};
    size_t last_pos = 0;
    while (true)
    {
        size_t pos = std::string::npos;
        size_t plen = 0;
        for (const auto &pat : patterns)
        {
            size_t p = code.find(pat, last_pos);
            if (p != std::string::npos && p < pos) { pos = p; plen = pat.length(); }
        }
        if (pos == std::string::npos) break;
        size_t end_pos = code.find(");", pos);
        if (end_pos == std::string::npos) break;
        size_t elem_start = pos + plen;
        size_t elem_end = code.find_first_of(",)", elem_start);
        std::string elem = code.substr(elem_start, elem_end - elem_start);
        result += code.substr(last_pos, end_pos + 2 - last_pos);
        result += " " + vec_name + ".push_back(" + elem + ");";
        last_pos = end_pos + 2;
    }
    result += code.substr(last_pos);
    return result;
}

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

    // Child components attach their own roots inside view(); pass the anchor
    // through so they keep their position too (an invalid anchor appends).
    std::string view_pattern = "._view(" + parent_var + ");";
    std::string view_replacement = "._view(" + parent_var + ", " + anchor_var + ");";
    size_t vpos = 0;
    while ((vpos = result.find(view_pattern, vpos)) != std::string::npos)
    {
        result.replace(vpos, view_pattern.length(), view_replacement);
        vpos += view_replacement.length();
    }

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
// Code Generation Helpers
// ============================================================================

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
        ss << "    coi::vector<" << comp_name << "> _loop_" << comp_name << "s;\n";
    }
    ss << "    void _forget_row(webcc::handle row) {\n";
    for (const auto &comp_name : loop_component_types)
        ss << "        coi_drop_row_children(_loop_" << comp_name << "s, row);\n";
    ss << "        coi_forget_handle(row);\n";
    ss << "    }\n";
}

static void emit_loop_region_members(std::stringstream &ss, const std::vector<LoopRegion> &loop_regions)
{
    for (const auto &region : loop_regions)
    {
        ss << "    webcc::handle _loop_" << region.loop_id << "_parent;\n";
        ss << "    webcc::handle _loop_" << region.loop_id << "_anchor;\n";
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
            ss << "    coi::vector<webcc::handle> _loop_" << region.loop_id << "_elements;\n";
            if (region.is_keyed)
                ss << "    coi::vector<coi::string> _loop_" << region.loop_id << "_keys;\n";
        }
        else if (region.is_keyed && !region.component_type.empty() && !region.is_member_ref_loop)
        {
            // keyed component rows: the keys of the last sync, to update props in place
            ss << "    coi::vector<coi::string> _loop_" << region.loop_id << "_keys;\n";
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


// ============================================================================
// Tree Traversal Functions
// ============================================================================

// Collect component types used inside for loops
static void collect_loop_components(ASTNode *node, std::set<std::string> &loop_components, bool in_loop = false)
{
    if (auto comp = dynamic_cast<ComponentInstantiation *>(node))
    {
        // Don't collect member references - they're already declared as member variables
        if (in_loop && !comp->is_member_reference)
        {
            loop_components.insert(qualified_name(comp->module_prefix, comp->component_name));
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

    auto resolve_component_type = [&](const std::string &type_name) -> std::string {
        if (session.component_info.find(type_name) != session.component_info.end())
        {
            return type_name;
        }
        if (session.data_type_names.count(type_name))
        {
            return type_name;
        }
        if (type_name.find("::") != std::string::npos)
        {
            return type_name;
        }
        std::string same_module = qualified_name(module_name, type_name);
        if (session.component_info.find(same_module) != session.component_info.end())
        {
            return same_module;
        }
        if (session.data_type_names.count(same_module))
        {
            return same_module;
        }
        return type_name;
    };

    // Set up component-local type context for convert_type() to use
    std::set<std::string> local_data_names;
    std::set<std::string> local_enum_names;
    for (const auto &d : data)
    {
        local_data_names.insert(d->name);
    }
    for (const auto &e : enums)
    {
        local_enum_names.insert(e->name);
    }
    ComponentTypeContext::instance().set(qualified_name(module_name, name), local_data_names, local_enum_names);
    ComponentTypeContext::instance().set_module_scope(module_name, session.data_type_names);
    
    // Register method signatures for member function reference lambda generation
    for (const auto &m : methods)
    {
        std::vector<std::string> param_types;
        for (const auto &p : m.params) {
            param_types.push_back(p.type);
        }
        ComponentTypeContext::instance().register_method_signature(m.name, m.return_type, param_types);
    }

    // what each method reads of the component's state, closed over the methods it calls
    g_method_reads.clear();
    std::set<std::string> member_names;
    for (const auto &v : state) member_names.insert(v->name);
    for (const auto &p : params) member_names.insert(p->name);
    std::map<std::string, std::set<std::string>> direct;
    g_method_reads_building = true;
    for (const auto &m : methods)
    {
        std::set<std::string> reads, kept;
        for (const auto &stmt : m.body)
            stmt->collect_dependencies(reads);
        for (const auto &r : reads)
            if (member_names.count(r) || std::any_of(methods.begin(), methods.end(), [&](const FunctionDef &o) { return o.name == r; }))
                kept.insert(r);
        direct[m.name] = kept;
    }
    g_method_reads_building = false;
    for (const auto &m : methods)
    {
        std::set<std::string> seen, out;
        std::vector<std::string> todo{m.name};
        while (!todo.empty())
        {
            std::string cur = todo.back();
            todo.pop_back();
            if (!seen.insert(cur).second || !direct.count(cur))
                continue;
            for (const auto &d : direct[cur])
            {
                if (direct.count(d)) todo.push_back(d);
                else out.insert(d);
            }
        }
        g_method_reads[m.name] = out;
    }

    // Populate global context for reference params
    g_ref_props.clear();
    for (auto &param : params)
    {
        if (param->is_reference)
        {
            g_ref_props.insert(param->name);
        }
        ComponentTypeContext::instance().set_component_symbol_type(param->name, param->type);
    }

    for (auto &var : state)
    {
        ComponentTypeContext::instance().set_component_symbol_type(var->name, var->type);
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

    std::string qname = qualified_name(module_name, name);
    std::stringstream ss_render;
    HandleBlock render_handles;
    ViewCodegenContext view_ctx{ss_render, "parent", element_count, event_handlers, bindings,
        component_counters, method_names, qname, false, &loop_regions, &loop_counter, &if_regions, &if_counter, "", &render_handles};
    for (auto &root : render_roots)
    {
        if (auto el = dynamic_cast<HTMLElement *>(root.get()))
        {
            el->generate_code(view_ctx);
        }
        else if (auto comp = dynamic_cast<ComponentInstantiation *>(root.get()))
        {
            comp->generate_code(view_ctx);
        }
        else if (auto viewIf = dynamic_cast<ViewIfStatement *>(root.get()))
        {
            viewIf->generate_code(view_ctx);
        }
        else if (auto viewFor = dynamic_cast<ViewForRangeStatement *>(root.get()))
        {
            viewFor->generate_code(view_ctx);
        }
        else if (auto viewForEach = dynamic_cast<ViewForEachStatement *>(root.get()))
        {
            viewForEach->generate_code(view_ctx);
        }
        else if (auto routePlaceholder = dynamic_cast<RoutePlaceholder *>(root.get()))
        {
            // Route placeholder - create anchor comment for inserting routed components
            ss_render << "        _route_parent = parent;\n";
            ss_render << "        _route_anchor = webcc::DOMElement(" << render_handles.next() << ");\n";
            ss_render << "        webcc::dom::create_comment_deferred(_route_anchor, \"coi-route\");\n";
            ss_render << "        webcc::dom::append_child(parent, _route_anchor);\n";
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
            g_component_array_loops[region.iterable_raw.empty() ? region.iterable_expr : region.iterable_raw] = info;
        }
    }

    // Populate global context for keyed HTML loops over non-component arrays
    g_array_loops.clear();
    g_html_loop_var_infos.clear();
    for (const auto &region : loop_regions)
    {
        if (region.is_keyed && region.is_html_loop)
        {
            ArrayLoopInfo info;
            info.loop_id = region.loop_id;
            info.parent_var = "_loop_" + std::to_string(region.loop_id) + "_parent";
            info.anchor_var = "_loop_" + std::to_string(region.loop_id) + "_anchor";
            info.elements_vec_name = "_loop_" + std::to_string(region.loop_id) + "_elements";
            info.var_name = region.var_name;
            info.item_creation_code = transform_to_insert_before(region.item_creation_code, info.parent_var, info.anchor_var);
            info.root_element_var = region.root_element_var;
            info.key_expr = region.key_expr;
            info.is_only_child = region.is_only_child;
            g_array_loops[region.iterable_raw.empty() ? region.iterable_expr : region.iterable_raw].push_back(info);

            HtmlLoopVarInfo var_info;
            var_info.loop_id = region.loop_id;
            var_info.iterable_expr = region.iterable_expr;
            g_html_loop_var_infos[region.var_name] = var_info;
        }
    }

    // Generate component as a struct
    // Note: Data types and enums are now flattened to global scope with ComponentName_ prefix
    ss << "struct " << qualified_name(module_name, name) << " {\n";

    // Component parameters (data members only - callbacks emitted later for proper aggregate init order)
    for (auto &param : params)
    {
        ss << "    " << convert_type(resolve_component_type(param->type));
        if (param->is_reference)
        {
            ss << "* " << cpp_name(param->name) << " = nullptr";
        }
        else
        {
            ss << " " << cpp_name(param->name);
            if (param->default_value)
            {
                ss << " = " << param->default_value->to_webcc();
            }
        }
        ss << ";\n";
    }

    // State whose initializer reads a param can't be a field initializer: the parent sets
    // params after construction. Those are left unset here and seeded once at the top of _view
    std::set<std::string> param_names;
    for (auto &param : params)
        param_names.insert(param->name);
    std::vector<std::pair<std::string, std::string>> seeded_state;
    auto reads_param = [&](Expression *init) {
        if (!init)
            return false;
        std::set<std::string> deps;
        init->collect_dependencies(deps);
        for (auto &d : deps)
            if (param_names.count(d))
                return true;
        return false;
    };

    // State variables (data members only - callbacks emitted later)
    for (auto &var : state)
    {
        bool seed = !var->is_reference && reads_param(var->initializer.get());
        // Special handling for array literals
        if (auto arr_lit = dynamic_cast<ArrayLiteral *>(var->initializer.get()))
        {
            if (var->type.ends_with("[]"))
            {
                std::string elem_type = var->type.substr(0, var->type.length() - 2);
                
                // Propagate element type to anonymous struct literals
                arr_lit->propagate_element_type(elem_type);
                
                // Component state arrays with T[] type: always use coi::vector (even if not mut).
                //
                // WHY NOT USE FIXED ARRAYS HERE?
                // When we have `string[] items = ["a", "b", "c"]`, the array size is known
                // at compile time (3 elements). However, if this state is passed to a child
                // component's prop declared as `string[] items`, that prop compiles to
                // coi::vector<string> because the child doesn't know what size array it will
                // receive. Using coi::array<T, N> here would cause a type mismatch.
          
                std::string vec_type = "coi::vector<" + convert_type(resolve_component_type(elem_type)) + ">";
                if (seed)
                {
                    ss << "    " << vec_type << " " << cpp_name(var->name) << ";\n";
                    seeded_state.push_back({cpp_name(var->name), arr_lit->to_webcc()});
                    continue;
                }
                ss << "    " << (var->is_mutable ? "" : "const ") << vec_type;
                if (var->is_reference)
                    ss << "&";
                ss << " " << cpp_name(var->name) << " = " << arr_lit->to_webcc() << ";\n";
                continue;
            }
        }

        // never const, the parent calls _destroy etc. on it
        bool component_member = !var->is_reference && session.component_info.count(resolve_component_qname(session, module_name, var->type));
        bool plain_init = var->initializer && !DefSchema::instance().is_handle(var->type) && var->type.find("coi::function<") != 0;
        if (seed && plain_init && !component_member)
        {
            // not const: it's assigned once in _view; the checker still keeps Coi code from writing it
            ss << "    " << convert_type(resolve_component_type(var->type)) << " " << cpp_name(var->name) << "{};\n";
            seeded_state.push_back({cpp_name(var->name), var->initializer->to_webcc()});
            continue;
        }
        ss << "    " << (var->is_mutable || component_member ? "" : "const ") << convert_type(resolve_component_type(var->type));
        if (var->is_reference)
            ss << "&";
        ss << " " << cpp_name(var->name);
        if (var->initializer)
        {
            if (DefSchema::instance().is_handle(var->type))
            {
                ss << "{" << var->initializer->to_webcc() << "}";
            }
            // Handle member function reference assigned to coi::function type
            else if (var->type.find("coi::function<") == 0)
            {
                if (auto *ref_expr = dynamic_cast<ReferenceExpression *>(var->initializer.get()))
                {
                    // Get the method name from the reference operand
                    std::string method_name = ref_expr->operand->to_webcc();
                    ss << " = " << generate_member_function_lambda(var->type, method_name);
                }
                else
                {
                    ss << " = " << var->initializer->to_webcc();
                }
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
            ss << "    coi::function<void()> " << make_callback_name(param->name) << ";\n";
        }
        // Generate callback for pub mut params (for parent-child reactivity)
        else if (param->is_public && param->is_mutable)
        {
            ss << "    coi::function<void()> " << make_callback_name(param->name) << ";\n";
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
                    ss << "    coi::function<void()> " << make_callback_name(var->name) << ";\n";
                }
                continue;
            }
        }

        if (var->is_public && var->is_mutable)
        {
            ss << "    coi::function<void()> " << make_callback_name(var->name) << ";\n";
        }
    }

    // Signal listener lists and emit helpers
    for (const auto &signal : signals)
    {
        std::string param_types;
        std::string callback_type;
        std::string param_decl;
        std::string arg_list;
        std::vector<std::string> converted_param_types;
        std::vector<std::string> param_names;
        for (size_t i = 0; i < signal.params.size(); ++i)
        {
            const auto &param = signal.params[i];
            if (i > 0)
            {
                param_types += ", ";
                param_decl += ", ";
                arg_list += ", ";
            }
            std::string converted = convert_type(resolve_component_type(param.type));
            param_types += converted;
            param_decl += converted + " " + cpp_name(param.name);
            arg_list += cpp_name(param.name);
            converted_param_types.push_back(converted);
            param_names.push_back(param.name);
        }

        callback_type = "coi::function<void(" + param_types + ")>";

        ss << "    int _next_listener_id_" << signal.name << " = 1;\n";
        ss << "    coi::vector<int> _listener_ids_" << signal.name << ";\n";
        ss << "    coi::vector<" << callback_type << "> _listeners_" << signal.name << ";\n";
        ss << "    int _listener_dispatch_depth_" << signal.name << " = 0;\n";
        ss << "    bool _listener_needs_compact_" << signal.name << " = false;\n";

        ss << "    int _add_listener_" << signal.name << "(" << callback_type << " cb) {\n";
        ss << "        int id = _next_listener_id_" << signal.name << "++;\n";
        ss << "        _listener_ids_" << signal.name << ".push_back(id);\n";
        ss << "        _listeners_" << signal.name << ".push_back(cb);\n";
        ss << "        return id;\n";
        ss << "    }\n";

        // Zero-arg listener adapter (ignore all signal payload fields).
        ss << "    int _add_listener_" << signal.name << "_0(coi::function<void()> cb) {\n";
        ss << "        return _add_listener_" << signal.name << "([cb](" << param_decl << ") {\n";
        ss << "            if (cb) cb();\n";
        ss << "        });\n";
        ss << "    }\n";

        // Allow callback-style listeners that consume only a prefix of signal args.
        for (size_t prefix_count = 0; prefix_count < signal.params.size(); ++prefix_count)
        {
            std::string prefix_types;
            std::string prefix_params;
            std::string full_wrapper_params;
            std::string forward_args;
            for (size_t i = 0; i < signal.params.size(); ++i)
            {
                if (i > 0)
                {
                    full_wrapper_params += ", ";
                }
                full_wrapper_params += converted_param_types[i] + " _arg" + std::to_string(i);

                if (i <= prefix_count)
                {
                    if (!prefix_types.empty())
                    {
                        prefix_types += ", ";
                        prefix_params += ", ";
                        forward_args += ", ";
                    }
                    prefix_types += converted_param_types[i];
                    prefix_params += converted_param_types[i] + " _arg" + std::to_string(i);
                    forward_args += "_arg" + std::to_string(i);
                }
            }

                ss << "    int _add_listener_" << signal.name << "_" << (prefix_count + 1)
               << "(coi::function<void(" << prefix_types << ")> cb) {\n";
            ss << "        return _add_listener_" << signal.name << "([cb](" << full_wrapper_params << ") {\n";
            ss << "            if (cb) cb(" << forward_args << ");\n";
            ss << "        });\n";
            ss << "    }\n";
        }

        ss << "    void _remove_listener_" << signal.name << "(int id) {\n";
        ss << "        for (int i = 0; i < (int)_listener_ids_" << signal.name << ".size(); ++i) {\n";
        ss << "            if (_listener_ids_" << signal.name << "[i] == id) {\n";
        ss << "                if (_listener_dispatch_depth_" << signal.name << " > 0) {\n";
        ss << "                    _listener_ids_" << signal.name << "[i] = 0;\n";
        ss << "                    _listeners_" << signal.name << "[i] = nullptr;\n";
        ss << "                    _listener_needs_compact_" << signal.name << " = true;\n";
        ss << "                } else {\n";
        ss << "                    _listener_ids_" << signal.name << ".remove(i);\n";
        ss << "                    _listeners_" << signal.name << ".remove(i);\n";
        ss << "                }\n";
        ss << "                return;\n";
        ss << "            }\n";
        ss << "        }\n";
        ss << "    }\n";

        ss << "    void _emit_" << signal.name << "(" << param_decl << ") {\n";
        ss << "        _listener_dispatch_depth_" << signal.name << "++;\n";
        ss << "        int _emit_count = (int)_listeners_" << signal.name << ".size();\n";
        ss << "        for (int i = 0; i < _emit_count; ++i) {\n";
        ss << "            auto cb = _listeners_" << signal.name << "[i];\n";
        ss << "            if(cb) cb(" << arg_list << ");\n";
        ss << "        }\n";
        ss << "        _listener_dispatch_depth_" << signal.name << "--;\n";
        ss << "        if (_listener_dispatch_depth_" << signal.name << " == 0 && _listener_needs_compact_" << signal.name << ") {\n";
        ss << "            for (int i = (int)_listener_ids_" << signal.name << ".size() - 1; i >= 0; --i) {\n";
        ss << "                if (_listener_ids_" << signal.name << "[i] == 0) {\n";
        ss << "                    _listener_ids_" << signal.name << ".remove(i);\n";
        ss << "                    _listeners_" << signal.name << ".remove(i);\n";
        ss << "                }\n";
        ss << "            }\n";
        ss << "            _listener_needs_compact_" << signal.name << " = false;\n";
        ss << "        }\n";
        ss << "    }\n";
    }

    // Element handles
    if (element_count > 0)
    {
        ss << "    webcc::handle _el[" << element_count << "];\n";
    }

    // Event handler bitmasks
    EventMasks masks = compute_event_masks(event_handlers);
    emit_event_mask_constants(ss, masks, element_count);

    // Child component members
    emit_component_members(ss, component_members);
    ss << "    bool _coi_alive = false; // view() mounted and _destroy() not yet run\n";
    if (!seeded_state.empty())
        ss << "    bool _coi_seeded = false; // state that reads params, set on the first _view\n";

    // Vector members for components in loops
    emit_loop_vector_members(ss, loop_component_types);

    // Loop region tracking
    emit_loop_region_members(ss, loop_regions);

    // If region tracking
    emit_if_region_members(ss, if_regions);

    // Router state (if router block defined)
    if (router)
    {
        ss << "    coi::string _current_route;\n";
        ss << "    bool _route_dirty = false;\n";
        ss << "    webcc::handle _route_parent;\n";
        ss << "    webcc::handle _route_anchor;\n";
        // Generate component pointers for each route
        for (size_t i = 0; i < router->routes.size(); ++i)
        {
            const auto& route = router->routes[i];
            ss << "    " << qualified_name(route.module_name, route.component_name) << "* _route_" << i << " = nullptr;\n";
        }
    }

    // Listener registration tokens for listen { ... } bindings
    for (size_t i = 0; i < listen_entries.size(); ++i)
    {
        ss << "    int _listen_reg_" << i << " = 0;\n";
    }

    // Build update entries map
    struct UpdateEntry
    {
        std::string code;
        int if_region_id;
        bool in_then_branch;
        std::string guard;  // "" outside <if> regions
    };
    // A binding inside nested <if>s must only update while every enclosing
    // region is in the branch that owns it; the outer ones don't reset the
    // inner state when they close.
    auto guard_for = [](const Binding &b) {
        std::string g;
        for (const auto &[id, then_branch] : b.if_chain)
        {
            if (!g.empty()) g += " && ";
            g += (then_branch ? "_if_" : "!_if_") + std::to_string(id) + "_state";
        }
        if (g.empty() && b.if_region_id >= 0)
            g = (b.in_then_branch ? "_if_" : "!_if_") + std::to_string(b.if_region_id) + "_state";
        return g;
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
        std::set<MemberDependency> member_dependencies;
        std::string method_name;
        std::string guard;
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

        std::string el_var = "_el[" + std::to_string(binding.element_id) + "]";
        std::string update_line;
        std::string dom_call;
        if (binding.type == "attr") {
            // Use set_property for properties that need to be set on the DOM object, not as attributes
            // - value: for input/textarea/select current value (attribute only sets default)
            // - checked: for checkbox/radio current checked state
            // - selected: for option current selected state
            if (binding.name == "value" || binding.name == "checked" || binding.name == "selected") {
                dom_call = "webcc::dom::set_property(" + el_var + ", \"" + binding.name + "\", ";
            } else {
                dom_call = "webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", ";
            }
        } else if (binding.type == "html") {
            // Raw HTML injection via <raw> element
            dom_call = "webcc::dom::set_inner_html(" + el_var + ", ";
        } else if (binding.type == "textnode") {
            // A bare interpolation that sits next to sibling elements is created as
            // a real DOM Text node (create_text_node), not an element. Text nodes
            // have no settable innerText, so set_inner_text is a silent no-op and
            // the value freezes at its initial render. Update via nodeValue.
            dom_call = "webcc::dom::set_node_value(" + el_var + ", ";
        } else {
            dom_call = "webcc::dom::set_inner_text(" + el_var + ", ";
        }

        bool optimized = false;
        if (binding.expr)
        {
            if (auto strLit = dynamic_cast<StringLiteral *>(binding.expr))
            {
                update_line = generate_formatter_block_from_string_literal(strLit, dom_call);
                optimized = true;
            }
        }

        if (!optimized && binding.value_code.find("coi::string::concat(") == 0)
        {
            std::string args_str = binding.value_code.substr(20);
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
            element_attr_bindings[key].guard = guard_for(binding);
            for (const auto &dep : binding.dependencies)
            {
                element_attr_bindings[key].dependencies.insert(dep);
            }
            for (const auto &mem_dep : binding.member_dependencies)
            {
                element_attr_bindings[key].member_dependencies.insert(mem_dep);
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
            // attribute names can hold '-' and ':' (data-id, xlink:href), identifiers can't
            std::string attr = key.name;
            for (char &c : attr)
                if (!isalnum((unsigned char)c)) c = '_';
            method_name = "_update_el" + std::to_string(key.element_id) + "_" + attr;
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
            entry.guard = binding.guard;
            var_update_entries[dep].push_back(entry);
        }
    }

    // Build map from member dependencies to update method names
    std::map<MemberDependency, std::set<std::string>> member_dep_update_methods;
    for (const auto &[key, binding] : element_attr_bindings)
    {
        for (const auto &mem_dep : binding.member_dependencies)
        {
            member_dep_update_methods[mem_dep].insert(binding.method_name);
        }
    }

    // Generate shared element+attribute update methods first
    for (const auto &[key, binding] : element_attr_bindings)
    {
        ss << "    void " << binding.method_name << "() {\n";
        if (binding.guard.empty())
        {
            ss << "        " << binding.update_code << "\n";
        }
        else
        {
            ss << "        if (" << binding.guard << ") {\n";
            ss << "            " << binding.update_code << "\n";
            ss << "        }\n";
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

            std::map<std::string, std::set<std::string>> guarded;
            for (const auto &entry : entries)
            {
                if (entry.if_region_id >= 0)
                    guarded[entry.guard].insert(entry.code);
            }
            for (const auto &[guard, codes] : guarded)
            {
                ss << "        if (" << guard << ") {\n";
                for (const auto &code : codes)
                {
                    ss << "            " << code << "\n";
                }
                ss << "        }\n";
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
        // Skip syncing while the loop's region is unmounted: its parent handle is
        // invalid (never rendered, or reset on teardown), so there is nowhere to
        // insert. The loop is rendered in full when its region is (re)mounted.
        ss << "        if (!_loop_" << region.loop_id << "_parent.is_valid()) return;\n";

        if (region.is_keyed)
        {
            std::string count_var = "_loop_" + std::to_string(region.loop_id) + "_count";
            std::string parent_var = "_loop_" + std::to_string(region.loop_id) + "_parent";

            if (region.is_html_loop)
            {
                // Keyed HTML element loop (e.g., <for msg in messages key={msg}><div>{msg}</div></for>).
                // Every row is built fresh; a row whose key was there before is morphed into the
                // live one, so its nodes (and their pointer capture, focus, hover) survive. Then
                // rows are put in order, moving only those out of place
                std::string id = std::to_string(region.loop_id);
                std::string elements_vec = "_loop_" + id + "_elements";
                std::string keys_vec = "_loop_" + id + "_keys";
                std::string anchor_var = "_loop_" + id + "_anchor";
                bool temp = region.iterable_expr.find('(') != std::string::npos;
                std::string item_ref = std::string(temp ? "auto " : "auto& ") + cpp_name(region.var_name) + " = _items[_idx];\n";

                ss << "        auto&& _items = " << region.iterable_expr << ";\n";
                ss << "        int _new_count = (int)_items.size();\n";
                ss << "        coi::vector<webcc::handle> _old = " << elements_vec << ";\n";
                ss << "        bool _keyed = " << keys_vec << ".size() == _old.size();\n";
                ss << "        coi::vector<int> _taken;\n";
                ss << "        for (int _j = 0; _j < (int)_old.size(); _j++) _taken.push_back(0);\n";
                ss << "        coi::vector<int> _take;\n";
                ss << "        coi::vector<coi::string> _new_keys;\n";
                ss << "        for (int _idx = 0; _idx < _new_count; _idx++) {\n";
                ss << "            " << item_ref;
                ss << "            coi::string _k = coi_loop_key(" << region.key_expr << ");\n";
                ss << "            int _m = -1;\n";
                ss << "            if (_keyed) {\n";
                ss << "                if (_idx < (int)_old.size() && !_taken[_idx] && " << keys_vec << "[_idx] == _k) _m = _idx;\n";
                ss << "                else for (int _j = 0; _j < (int)_old.size(); _j++) { if (!_taken[_j] && " << keys_vec << "[_j] == _k) { _m = _j; break; } }\n";
                ss << "            }\n";
                ss << "            if (_m >= 0) _taken[_m] = 1;\n";
                ss << "            _take.push_back(_m);\n";
                ss << "            _new_keys.push_back(_k);\n";
                ss << "        }\n";
                ss << "        for (int _j = 0; _j < (int)_old.size(); _j++) {\n";
                ss << "            if (!_taken[_j]) { _forget_row(_old[_j]); webcc::dom::remove_element(_old[_j]); }\n";
                ss << "        }\n";
                ss << "        " << elements_vec << ".clear();\n";
                ss << "        g_view_depth++;\n";
                ss << "        for (int _idx = 0; _idx < _new_count; _idx++) {\n";
                ss << "            " << item_ref;
                ss << indent_code(transform_to_insert_before(region.item_creation_code, "_loop_" + id + "_parent", anchor_var), "        ");
                ss << "            if (_take[_idx] >= 0) { _forget_row(_old[_take[_idx]]); webcc::dom::morph(_old[_take[_idx]], " << region.root_element_var << "); }\n";
                ss << "            else webcc::dom::detach(" << region.root_element_var << ");\n";
                ss << "            " << elements_vec << ".push_back(" << region.root_element_var << ");\n";
                ss << "        }\n";
                ss << "        for (int _idx = _new_count - 1; _idx >= 0; _idx--)\n";
                ss << "            webcc::dom::place(" << parent_var << ", " << elements_vec << "[_idx], _idx + 1 < _new_count ? " << elements_vec << "[_idx + 1] : " << anchor_var << ");\n";
                ss << "        " << keys_vec << " = _new_keys;\n";
                ss << "        if (--g_view_depth == 0) webcc::flush();\n";
                ss << "        " << count_var << " = _new_count;\n";
            }
            else
            {
                // Keyed component loop
                std::string vec_name = region.is_member_ref_loop ? region.iterable_expr : ("_loop_" + region.component_type + "s");

                if (region.is_member_ref_loop)
                {
                    ss << "        int _new_count = (int)" << vec_name << ".size();\n";

                    // Clear existing views - MUST call _remove_view() to unregister event handlers from dispatchers
                    ss << "        if (" << count_var << " > 0) {\n";
                    ss << "            for (int _i = 0; _i < " << count_var << "; _i++) {\n";
                    ss << "                " << vec_name << "[_i]._remove_view();\n";
                    ss << "            }\n";
                    ss << "        }\n";
                    ss << "        \n";
                }
                else
                {
                    // same keys in the same order: the rows stay and only their props are set.
                    // Rebuilding them here would replace the element under a pointer between
                    // its pointerdown and its click
                    std::string keys_vec = "_loop_" + std::to_string(region.loop_id) + "_keys";
                    ss << "        coi::vector<coi::string> _new_keys;\n";
                    ss << "        for (auto& " << cpp_name(region.var_name) << " : " << region.iterable_expr << ") _new_keys.push_back(coi_loop_key(" << region.key_expr << "));\n";
                    if (!region.item_update_code.empty())
                    {
                        ss << "        bool _same = " << count_var << " > 0 && (int)" << vec_name << ".size() == (int)_new_keys.size() && " << keys_vec << ".size() == _new_keys.size();\n";
                        ss << "        for (int _k = 0; _same && _k < (int)_new_keys.size(); _k++) if (!(" << keys_vec << "[_k] == _new_keys[_k])) _same = false;\n";
                        ss << "        if (_same) {\n";
                        ss << "            int _i = 0;\n";
                        ss << "            for (auto& " << cpp_name(region.var_name) << " : " << region.iterable_expr << ") {\n";
                        ss << "            auto& _inst = " << vec_name << "[_i++];\n";
                        ss << region.item_update_code;
                        ss << "            }\n";
                        ss << "            return;\n";
                        ss << "        }\n";
                    }
                    ss << "        " << keys_vec << " = _new_keys;\n";
                    // the rows are rebuilt below, so the old instances go entirely; leaving them
                    // in the vector made every sync tear down stale rows and keep the live ones
                    ss << "        int _new_count = (int)" << region.iterable_expr << ".size();\n";
                    ss << "        for (auto& _c : " << vec_name << ") _c._destroy();\n";
                    ss << "        " << vec_name << ".clear();\n";
                    // rows hand out `this` (handlers, child callbacks), so the vector must not move under them
                    ss << "        " << vec_name << ".reserve(_new_count);\n";
                }

                // Recreate all items in current array order with fresh views using insert_before for proper DOM ordering
                std::string anchor_var = "_loop_" + std::to_string(region.loop_id) + "_anchor";
                ss << "        g_view_depth++;\n";
                ss << "        for (auto& " << cpp_name(region.var_name) << " : " << region.iterable_expr << ") {\n";

                std::string item_code = region.item_creation_code;
                item_code = transform_to_insert_before(item_code, parent_var, anchor_var);
                ss << indent_code(item_code, "        ");

                ss << "        }\n";
                ss << "        if (--g_view_depth == 0) webcc::flush();\n";
                ss << "        " << count_var << " = _new_count;\n";
            }
        }
        else
        {
            ss << "        int new_count = " << region.end_expr << " - " << region.start_expr << ";\n";
            ss << "        int old_count = _loop_" << region.loop_id << "_count;\n";
            if (!(region.is_html_loop && region.component_type.empty()))
                ss << "        if (new_count == old_count) return;\n";
            ss << "        (void)old_count;\n";

            if (!region.component_type.empty())
            {
                std::string vec_name = "_loop_" + region.component_type + "s";
                std::string anchor_var = "_loop_" + std::to_string(region.loop_id) + "_anchor";

                ss << "        if (new_count > old_count) {\n";
                ss << "            for (int " << cpp_name(region.var_name) << " = old_count; " << cpp_name(region.var_name) << " < new_count; " << cpp_name(region.var_name) << "++) {\n";

                std::string item_code = region.item_creation_code;
                item_code = transform_to_insert_before(item_code, "_loop_" + std::to_string(region.loop_id) + "_parent", anchor_var);
                ss << indent_code(item_code, "    ");
                ss << "            }\n";

                ss << "            for (int _i = 0; _i < old_count; _i++) " << vec_name << "[_i]._rebind();\n";

                ss << "        } else {\n";
                ss << "            while ((int)" << vec_name << ".size() > new_count) {\n";
                ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
                ss << "                " << vec_name << ".pop_back();\n";
                ss << "            }\n";

                if (!region.item_update_code.empty())
                {
                    ss << "            for (int " << cpp_name(region.var_name) << " = 0; " << cpp_name(region.var_name) << " < new_count; " << cpp_name(region.var_name) << "++) {\n";
                    ss << region.item_update_code;
                    ss << "            }\n";
                }
                ss << "        }\n";
            }
            else if (region.is_html_loop)
            {
                // elements over a range: a sync runs when the range or anything the body reads
                // changed, so the items are rebuilt in full, like a keyed loop's. The loop variable
                // runs from the start expression, not from zero
                std::string vec_name = "_loop_" + std::to_string(region.loop_id) + "_elements";
                std::string anchor_var = "_loop_" + std::to_string(region.loop_id) + "_anchor";

                ss << "        for (auto& _el : " << vec_name << ") {\n";
                ss << "            _forget_row(_el);\n";
                ss << "            webcc::dom::remove_element(_el);\n";
                ss << "        }\n";
                ss << "        " << vec_name << ".clear();\n";
                ss << "        g_view_depth++;\n";
                ss << "        for (int " << cpp_name(region.var_name) << " = " << region.start_expr << "; " << cpp_name(region.var_name) << " < " << region.end_expr << "; " << cpp_name(region.var_name) << "++) {\n";

                std::string item_code = region.item_creation_code;
                // the body appends to the loop's parent: insert before the anchor instead, or the
                // rows end up after whatever follows the loop
                std::string loop_parent = "_loop_" + std::to_string(region.loop_id) + "_parent";
                item_code = transform_to_insert_before(item_code, loop_parent, anchor_var);
                item_code = track_top_level_inserts(item_code, loop_parent, vec_name);
                ss << indent_code(item_code, "    ");
                ss << "        }\n";
                ss << "        if (--g_view_depth == 0) webcc::flush();\n";
            }
            ss << "        _loop_" << region.loop_id << "_count = new_count;\n";
        }
        ss << "    }\n";
    }

    // Generate _sync_loop_X_item() methods for keyed HTML loops (single-item patch)
    for (const auto &region : loop_regions)
    {
        if (!(region.is_keyed && region.is_html_loop) || region.root_element_var.empty())
            continue;

        std::string elements_vec = "_loop_" + std::to_string(region.loop_id) + "_elements";
        std::string parent_var = "_loop_" + std::to_string(region.loop_id) + "_parent";
        std::string anchor_var = "_loop_" + std::to_string(region.loop_id) + "_anchor";

        ss << "    void _sync_loop_" << region.loop_id << "_item(int _idx) {\n";
        ss << "        if (!" << parent_var << ".is_valid()) return;\n";
        ss << "        if (_idx < 0 || _idx >= (int)" << region.iterable_expr << ".size()) return;\n";
        ss << "        webcc::handle _ref = " << anchor_var << ";\n";
        ss << "        if (_idx < (int)" << elements_vec << ".size()) {\n";
        ss << "            _ref = (_idx + 1 < (int)" << elements_vec << ".size()) ? " << elements_vec << "[_idx + 1] : " << anchor_var << ";\n";
        ss << "        }\n";
        // a call like colors() is a temporary: take the item by value
        bool temp = region.iterable_expr.find('(') != std::string::npos;
        ss << "        " << (temp ? "auto " : "auto& ") << cpp_name(region.var_name) << " = " << region.iterable_expr << "[_idx];\n";

        std::string item_code = transform_to_insert_before(region.item_creation_code, parent_var, "_ref");
        ss << indent_code(item_code, "        ");
        // the live row takes the fresh one's look and handles
        ss << "        if (_idx < (int)" << elements_vec << ".size()) {\n";
        ss << "            _forget_row(" << elements_vec << "[_idx]);\n";
        ss << "            webcc::dom::morph(" << elements_vec << "[_idx], " << region.root_element_var << ");\n";
        ss << "            " << elements_vec << "[_idx] = " << region.root_element_var << ";\n";
        ss << "        } else " << elements_vec << ".push_back(" << region.root_element_var << ");\n";
        ss << "        {\n";
        ss << "            auto& _keys = _loop_" << region.loop_id << "_keys;\n";
        ss << "            coi::string _k = coi_loop_key(" << region.key_expr << ");\n";
        ss << "            if (_idx < (int)_keys.size()) _keys[_idx] = _k;\n";
        ss << "            else if (_idx == (int)_keys.size()) _keys.push_back(_k);\n";
        ss << "        }\n";
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

    // A loop that lives inside a region being hidden: drop its rows and mark it
    // unmounted so a later _sync_loop() (array change while hidden) no-ops
    // instead of inserting against a detached parent/anchor.
    auto emit_loop_unmount = [&](int loop_id) {
        for (const auto &lr : loop_regions)
        {
            if (lr.loop_id != loop_id) continue;
            if (lr.is_member_ref_loop)
            {
                // array owns the components, only remove their views
                ss << "            for (auto& _item : " << lr.iterable_expr << ") _item._remove_view();\n";
                ss << "            _loop_" << loop_id << "_count = 0;\n";
            }
            else if (!lr.component_type.empty())
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
                ss << "                _forget_row(" << vec_name << "[" << vec_name << ".size() - 1]);\n";
                ss << "                webcc::dom::remove_element(" << vec_name << "[" << vec_name << ".size() - 1]);\n";
                ss << "                " << vec_name << ".pop_back();\n";
                ss << "            }\n";
                if (lr.is_keyed)
                    ss << "            _loop_" << loop_id << "_keys.clear();\n";
                ss << "            _loop_" << loop_id << "_count = 0;\n";
            }
            ss << "            _loop_" << loop_id << "_parent = webcc::DOMElement();\n";
            break;
        }
    };
    // Same for a nested <if>: its parent handle points at an element the outer
    // region just removed, so a sync while hidden (its own condition flipping)
    // would insert into a dead parent. Recurses into what it contains.
    std::function<void(int)> emit_if_unmount = [&](int nested_if_id) {
        for (const auto &nr : if_regions)
        {
            if (nr.if_id != nested_if_id) continue;
            for (int lid : nr.then_loop_ids) emit_loop_unmount(lid);
            for (int lid : nr.else_loop_ids) emit_loop_unmount(lid);
            for (int iid : nr.then_if_ids) emit_if_unmount(iid);
            for (int iid : nr.else_if_ids) emit_if_unmount(iid);
            ss << "            _if_" << nested_if_id << "_parent = webcc::DOMElement();\n";
            break;
        }
    };

    // Generate _sync_if_X() methods
    for (const auto &region : if_regions)
    {
        ss << "    void _sync_if_" << region.if_id << "() {\n";
        // Not rendered yet (e.g. state mutated through a pub method before the
        // first view()): nothing to sync; view() renders the live condition
        // inline. Mirrors the _loop_N_parent guard in loop syncs.
        ss << "        if (!_if_" << region.if_id << "_parent.is_valid()) return;\n";
        ss << "        bool new_state = " << region.condition_code << ";\n";
        ss << "        if (new_state == _if_" << region.if_id << "_state) return;\n";
        ss << "        _if_" << region.if_id << "_state = new_state;\n";
        ss << "        \n";

        std::map<std::string, std::set<int>> event_els;
        for (const auto &spec : get_event_specs())
        {
            event_els[spec.type] = get_elements_for_event(event_handlers, spec.type);
        }

        auto emit_remove_handlers_for_element = [&](int el_id, const std::string &condition_prefix) {
            for (const auto &spec : get_event_specs())
            {
                if (!event_els[spec.type].count(el_id))
                {
                    continue;
                }
                ss << "            ";
                if (!condition_prefix.empty())
                {
                    ss << "if (" << condition_prefix << ") ";
                }
                ss << spec.dispatcher_name << ".remove(_el[" << el_id << "]);\n";
            }
        };

        // Build sets of element IDs owned by nested ifs (to exclude from unconditional removal)
        std::set<int> else_nested_if_els;
        for (int nested_if_id : region.else_if_ids)
        {
            for (const auto &nested_region : if_regions)
            {
                if (nested_region.if_id == nested_if_id)
                {
                    else_nested_if_els.insert(nested_region.then_element_ids.begin(), nested_region.then_element_ids.end());
                    else_nested_if_els.insert(nested_region.else_element_ids.begin(), nested_region.else_element_ids.end());
                }
            }
        }
        std::set<int> then_nested_if_els;
        for (int nested_if_id : region.then_if_ids)
        {
            for (const auto &nested_region : if_regions)
            {
                if (nested_region.if_id == nested_if_id)
                {
                    then_nested_if_els.insert(nested_region.then_element_ids.begin(), nested_region.then_element_ids.end());
                    then_nested_if_els.insert(nested_region.else_element_ids.begin(), nested_region.else_element_ids.end());
                }
            }
        }

        ss << "        if (new_state) {\n";
        for (int el_id : region.else_element_ids)
        {
            if (else_nested_if_els.count(el_id))
                continue; // Handled by nested-if conditional removal below
            emit_remove_handlers_for_element(el_id, "");
        }
        for (int el_id : region.else_element_ids)
        {
            if (else_nested_if_els.count(el_id))
                continue; // Handled by nested-if conditional removal below
            ss << "            webcc::dom::remove_element(_el[" << el_id << "]);\n";
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
            emit_loop_unmount(loop_id);
        for (int nested_if_id : region.else_if_ids)
        {
            for (const auto &nested_region : if_regions)
            {
                if (nested_region.if_id == nested_if_id)
                {
                    for (int el_id : nested_region.then_element_ids)
                    {
                        emit_remove_handlers_for_element(el_id, "_if_" + std::to_string(nested_if_id) + "_state");
                        ss << "            if (_if_" << nested_if_id << "_state) webcc::dom::remove_element(_el[" << el_id << "]);\n";
                    }
                    for (int el_id : nested_region.else_element_ids)
                    {
                        emit_remove_handlers_for_element(el_id, "!_if_" + std::to_string(nested_if_id) + "_state");
                        ss << "            if (!_if_" << nested_if_id << "_state) webcc::dom::remove_element(_el[" << el_id << "]);\n";
                    }
                    emit_if_unmount(nested_if_id);
                }
            }
        }
        // a child coming back gets a fresh instance, not the state it had last time
        for (const auto &[comp_name, inst_id] : region.then_components)
            ss << "            " << comp_name << "_" << inst_id << ".~" << comp_name << "(); new (&" << comp_name << "_" << inst_id << ") " << comp_name << "();\n";
        ss << region.then_creation_code;

        ss << "        } else {\n";
        for (int el_id : region.then_element_ids)
        {
            if (then_nested_if_els.count(el_id))
                continue; // Handled by nested-if conditional removal below
            emit_remove_handlers_for_element(el_id, "");
        }
        for (int el_id : region.then_element_ids)
        {
            if (then_nested_if_els.count(el_id))
                continue; // Handled by nested-if conditional removal below
            ss << "            webcc::dom::remove_element(_el[" << el_id << "]);\n";
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
            emit_loop_unmount(loop_id);
        for (int nested_if_id : region.then_if_ids)
        {
            for (const auto &nested_region : if_regions)
            {
                if (nested_region.if_id == nested_if_id)
                {
                    for (int el_id : nested_region.then_element_ids)
                    {
                        emit_remove_handlers_for_element(el_id, "_if_" + std::to_string(nested_if_id) + "_state");
                        ss << "            if (_if_" << nested_if_id << "_state) webcc::dom::remove_element(_el[" << el_id << "]);\n";
                    }
                    for (int el_id : nested_region.else_element_ids)
                    {
                        emit_remove_handlers_for_element(el_id, "!_if_" + std::to_string(nested_if_id) + "_state");
                        ss << "            if (!_if_" << nested_if_id << "_state) webcc::dom::remove_element(_el[" << el_id << "]);\n";
                    }
                    emit_if_unmount(nested_if_id);
                }
            }
        }
        if (!region.else_creation_code.empty())
        {
            for (const auto &[comp_name, inst_id] : region.else_components)
                ss << "            " << comp_name << "_" << inst_id << ".~" << comp_name << "(); new (&" << comp_name << "_" << inst_id << ") " << comp_name << "();\n";
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
    // members built in code: mut Child child = Child(&doc, ...)
    struct MemberRef { std::string member, param, var; };
    std::vector<MemberRef> member_refs;
    for (const auto &var : state)
    {
        auto *ctor = dynamic_cast<ComponentConstruction *>(var->initializer.get());
        if (!ctor)
            continue;
        auto it = session.component_info.find(resolve_component_qname(session, module_name, ctor->component_name));
        if (it == session.component_info.end())
            continue;
        for (size_t i = 0; i < ctor->args.size() && i < it->second.param_names.size(); i++)
        {
            auto *id = dynamic_cast<Identifier *>(ctor->args[i].value.get());
            const std::string &param = it->second.param_names[i];
            if (!ctor->args[i].is_reference || !id || !it->second.ref_params.count(param))
                continue;
            member_refs.push_back({var->name, param, id->name});
            child_updates[id->name].push_back("        " + cpp_name(var->name) + "._refresh_" + param + "();\n");
        }
    }

    // _refresh_<param>: what a parent calls after changing something the param refers to.
    // _update_ only redoes bindings; regions are synced by the method epilogue, which the
    // parent's change never runs here. Children that got the same reference (or a prop
    // computed from it) are refreshed too, so a change two levels up still reaches them.
    for (const auto &param : params)
    {
        const std::string &v = param->name;
        ss << "    void _refresh_" << v << "() { if (!_coi_alive) return;";
        if (generated_updaters.count(v))
            ss << " _update_" << v << "();";
        for (int if_id : var_to_if_ids[v])
            ss << " _sync_if_" << if_id << "();";
        for (int loop_id : var_to_loop_ids[v])
            ss << " _sync_loop_" << loop_id << "();";
        ss << "\n";
        for (const auto &call : child_updates[v])
            ss << call;
        ss << "    }\n";
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
                if (g_component_array_loops.find(mod) == g_component_array_loops.end() &&
                    g_array_loops.find(mod) == g_array_loops.end())
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

    // Only a component's pub mut members expose an onXChange hook. Pods are plain
    // value structs with no hooks, so a fine-grained callback only fits when obj is
    // a component and member is one of its pub mut members. Pod fields rely on the
    // coarse per-object _update_<obj>() that mutations already trigger.
    auto member_dep_is_reactive = [&](const MemberDependency &mem_dep) -> bool {
        std::string obj_type = ComponentTypeContext::instance().get_symbol_type(mem_dep.object);
        if (obj_type.empty())
            return true; // unknown symbol: preserve prior behavior conservatively
        auto it = session.component_info.find(resolve_component_type(obj_type));
        if (it == session.component_info.end())
            return false; // pod / plain data type: no onXChange hook exists on it
        return it->second.pub_mut_members.count(mem_dep.member) > 0;
    };

    // a pub member component counts as changed when one of its own pub members changes, so
    // an owner further up that reads a.b.c hears about it through a.onBChange
    auto relays_child = [&](const std::string &var_name) {
        return pub_mut_vars.count(var_name) && generated_updaters.count(var_name);
    };

    auto emit_member_dependency_callbacks = [&]() {
        for (const auto &[mem_dep, methods] : member_dep_update_methods)
        {
            if (!member_dep_is_reactive(mem_dep))
                continue;
            std::string callback_name = make_callback_name(mem_dep.member);
            ss << "        " << cpp_name(mem_dep.object) << "." << callback_name << " = [this]() {";
            for (const auto &method_name : methods)
            {
                ss << " " << method_name << "();";
            }
            if (relays_child(mem_dep.object))
                ss << " _update_" << mem_dep.object << "();";
            ss << " };\n";
        }
    };

    // pub member components this view doesn't read itself still relay their changes up
    auto emit_pub_member_relays = [&]() {
        for (const auto &var : state)
        {
            if (!relays_child(var->name))
                continue;
            auto it = session.component_info.find(resolve_component_type(var->type));
            if (it == session.component_info.end())
                continue;
            for (const auto &member : it->second.pub_mut_members)
            {
                if (member_dep_update_methods.count(MemberDependency{var->name, member}))
                    continue;
                ss << "        " << cpp_name(var->name) << "." << make_callback_name(member) << " = [this]() { _update_" << var->name << "(); };\n";
            }
        }
    };

    auto emit_nested_component_reactivity = [&]() {
        for (const auto &param : params)
        {
            auto it = session.component_info.find(resolve_component_type(param->type));
            if (it != session.component_info.end() && !it->second.pub_mut_members.empty())
            {
                for (const auto &member : it->second.pub_mut_members)
                {
                    std::string callback_name = make_callback_name(member);
                    ss << "        " << cpp_name(param->name) << "." << callback_name << " = [this]() { _update_" << member << "(); };\n";
                }
            }
        }
    };

    // a child wrote through a reference it was built with: redo this component's bindings,
    // refresh the other children sharing the value, and pass the change up when the value
    // is itself a reference from above
    auto emit_constructed_member_refs = [&]() {
        for (const auto &ref : member_refs)
        {
            ss << "        " << cpp_name(ref.member) << "." << make_callback_name(ref.param) << " = [this]() {";
            if (generated_updaters.count(ref.var))
                ss << " _update_" << ref.var << "();";
            for (int if_id : var_to_if_ids[ref.var])
                ss << " _sync_if_" << if_id << "();";
            if (!g_component_array_loops.count(ref.var) && !g_array_loops.count(ref.var))
                for (int loop_id : var_to_loop_ids[ref.var])
                    ss << " _sync_loop_" << loop_id << "();";
            std::string own = "        " + cpp_name(ref.member) + "._refresh_" + ref.param + "();\n";
            for (const auto &call : child_updates[ref.var])
                if (call != own)
                    ss << " " << call.substr(8, call.size() - 10) << ";";
            if (g_ref_props.count(ref.var) && !pub_mut_vars.count(ref.var))
                ss << " if(" << make_callback_name(ref.var) << ") " << make_callback_name(ref.var) << "();";
            ss << " };\n";
        }
    };

    auto emit_listen_registrations = [&]() {
        for (size_t idx = 0; idx < listen_entries.size(); ++idx)
        {
            const auto &entry = listen_entries[idx];
            std::string target_expr = entry.target_is_reference ? ("(*" + cpp_name(entry.target_name) + ")") : cpp_name(entry.target_name);
            std::string lambda_params;
            std::string lambda_args;
            for (size_t i = 0; i < entry.param_types.size(); ++i)
            {
                if (i > 0)
                {
                    lambda_params += ", ";
                    lambda_args += ", ";
                }
                std::string arg_name = "_arg" + std::to_string(i);
                lambda_params += convert_type(resolve_component_type(entry.param_types[i])) + " " + arg_name;
                lambda_args += arg_name;
            }

                ss << "        if (_listen_reg_" << idx << " == 0) _listen_reg_" << idx << " = "
                    << target_expr << "._add_listener_" << entry.signal_name << "_" << entry.param_types.size()
               << "([this](" << lambda_params << ") { this->"
               << entry.handler_method_name << "(" << lambda_args << "); });\n";
        }
    };

    // Event handlers
    for (auto &handler : event_handlers)
    {
        const EventSpec *spec = find_event_spec(handler.event_type);
        if (!spec)
        {
            continue;
        }

        ss << "    void _handler_" << handler.element_id << "_" << handler.event_type << "(" << spec->handler_param_decl << ") {\n";
        if (handler.is_function_call)
        {
            ss << "        " << handler.handler_code << ";\n";
        }
        else
        {
            ss << "        " << handler.handler_code << "(" << spec->handler_call_arg << ");\n";
        }
        ss << "    }\n";
    }

    // View method. _before is an optional anchor: when valid, the component's
    // roots are inserted before it instead of appended, so components created
    // inside anchor-based regions (<if>/<for> re-syncs) keep their position.
    // An invalid handle appends (see dom INSERT_BEFORE: insertBefore(el, ref || null)).
    // _view, like _destroy: a component may have a field called view
    ss << "    void _view(webcc::handle parent = webcc::dom::get_body(), webcc::handle _before = webcc::handle()) {\n";
    ss << "        g_view_depth++;\n";
    ss << "        _coi_alive = true;\n";
    if (!seeded_state.empty())
    {
        // once: a loop may view the same instance again, and that mustn't reset its state
        ss << "        if (!_coi_seeded) {\n";
        ss << "            _coi_seeded = true;\n";
        for (auto &[name, init] : seeded_state)
            ss << "            " << name << " = " << init << ";\n";
        ss << "        }\n";
    }

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
        // Attach roots (and root-level child components) relative to _before.
        // Only top-level attaches use the literal "parent" var, so nested
        // append_child(_el[N], ...) calls are untouched.
        ss << transform_to_insert_before(render_handles.wrap(ss_render.str()), "parent", "_before");
    }
    // End view - flushes only at outermost level, then register event handlers
    ss << "        if (--g_view_depth == 0) webcc::flush();\n";
    // Register event handlers
    emit_all_event_registrations(ss, element_count, event_handlers, masks);

    // Wire up onChange callbacks for child component pub mut members (in if conditions)
    for (const auto &region : if_regions)
    {
        for (const auto &mem_dep : region.member_dependencies)
        {
            if (!member_dep_is_reactive(mem_dep))
                continue;
            std::string callback_name = make_callback_name(mem_dep.member);
            ss << "        " << cpp_name(mem_dep.object) << "." << callback_name << " = [this]() { _sync_if_" << region.if_id << "(); };\n";
        }
    }

    // Wire up onChange callbacks for child component pub mut members (in view bindings)
    emit_member_dependency_callbacks();

    // Wire up nested component reactivity (e.g., Vector.x/y -> Ball._update_x/y)
    emit_nested_component_reactivity();
    emit_pub_member_relays();
    emit_constructed_member_refs();

    // Wire signal listeners declared in listen { ... }
    emit_listen_registrations();

    if (has_mount)
        ss << "        _user_mount();\n";
    // Initialize router - render the component matching the initial URL.
    // _sync_route() matches statics, dynamic params, and the catch-all itself.
    if (router)
    {
        ss << "        _current_route = webcc::system::get_pathname();\n";
        ss << "        _sync_route();\n";
    }
    ss << "    }\n";

    // Rebind method (always generated, even if empty, for component array reallocation)
    ss << "    void _rebind() {\n";
    if (!event_handlers.empty())
    {
        emit_all_event_registrations(ss, element_count, event_handlers, masks);
    }

    // Re-wire nested component reactivity after reallocation
    emit_nested_component_reactivity();
    emit_pub_member_relays();
    emit_constructed_member_refs();

    // Re-wire listen block signal handlers after reallocation
    emit_listen_registrations();

    // Re-wire member dependency callbacks after reallocation
    emit_member_dependency_callbacks();

    ss << "    }\n";

    emit_component_router_methods(ss, *this);

    emit_component_lifecycle_methods(ss, session, *this, masks, if_regions, element_count, component_members, loop_component_types);

    ss << "};\n";

    g_ref_props.clear();
    ComponentTypeContext::instance().clear();

    return ss.str();
}
