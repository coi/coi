#include "view.h"
#include "formatter.h"
#include "../codegen/codegen_utils.h"

// Global set of components with scoped CSS (populated in main.cc before code generation)
std::set<std::string> g_components_with_scoped_css;

// Helper to map Coi types to C++ types for lambda params
static std::string coi_type_to_cpp(const std::string& type) {
    if (type == "int" || type == "int32") return "int32_t";
    if (type == "float" || type == "float64") return "double";
    if (type == "float32") return "float";
    if (type == "bool") return "bool";
    if (type == "string") return "webcc::string";
    return "int32_t";  // default
}

// Helper to build lambda parameter list from callback param types
static std::string build_lambda_params_from_types(const std::vector<std::string>& param_types)
{
    std::string params;
    for (size_t i = 0; i < param_types.size(); i++)
    {
        if (i > 0)
            params += ", ";
        params += coi_type_to_cpp(param_types[i]) + " _arg" + std::to_string(i);
    }
    return params;
}

// Helper to build function call args list for forwarding
static std::string build_forward_args(size_t count)
{
    std::string args;
    for (size_t i = 0; i < count; i++)
    {
        if (i > 0)
            args += ", ";
        args += "_arg" + std::to_string(i);
    }
    return args;
}

// Helper to transform append_child calls to insert_before for anchor-based if regions
// Transforms: webcc::dom::append_child(_if_X_parent, el[N]);
// To:         webcc::dom::insert_before(_if_X_parent, el[N], _if_X_anchor);
static std::string transform_to_insert_before(const std::string& code, const std::string& if_parent, const std::string& if_anchor) {
    std::string result;
    std::string search_pattern = "webcc::dom::append_child(" + if_parent + ", ";
    size_t pos = 0;
    size_t last_pos = 0;
    
    while ((pos = code.find(search_pattern, last_pos)) != std::string::npos) {
        // Copy everything up to this point
        result += code.substr(last_pos, pos - last_pos);
        
        // Find the closing ");
        size_t end_pos = code.find(");", pos);
        if (end_pos == std::string::npos) {
            // Malformed, just copy the rest
            result += code.substr(pos);
            return result;
        }
        
        // Extract the element being appended
        size_t elem_start = pos + search_pattern.length();
        std::string elem = code.substr(elem_start, end_pos - elem_start);
        
        // Generate insert_before call
        result += "webcc::dom::insert_before(" + if_parent + ", " + elem + ", " + if_anchor + ");";
        
        last_pos = end_pos + 2; // Skip past ");"
    }
    
    // Copy remaining content
    result += code.substr(last_pos);
    return result;
}

// Helper to build lambda parameter list from function call args
static std::string build_lambda_params(FunctionCall *func_call)
{
    std::string params;
    for (size_t i = 0; i < func_call->args.size(); i++)
    {
        if (i > 0)
            params += ", ";
        if (auto *id = dynamic_cast<Identifier *>(func_call->args[i].value.get()))
        {
            params += "int32_t " + id->name;
        }
        else
        {
            params += "int32_t _arg" + std::to_string(i);
        }
    }
    return params;
}

// Helper to build function call using lambda params instead of original args
// This ensures callbacks use the passed-in values rather than captured loop variables
static std::string build_lambda_call(FunctionCall *func_call)
{
    // Extract function name (before the arguments)
    std::string name = func_call->name;
    std::string result = name + "(";
    for (size_t i = 0; i < func_call->args.size(); i++)
    {
        if (i > 0)
            result += ", ";
        if (auto *id = dynamic_cast<Identifier *>(func_call->args[i].value.get()))
        {
            result += id->name;
        }
        else
        {
            result += "_arg" + std::to_string(i);
        }
    }
    result += ")";
    return result;
}

// Build minimal lambda capture: [this] outside loops, [this, var] inside loops.
static std::string build_lambda_capture(const std::string &loop_var_name)
{
    if (loop_var_name.empty())  // Not in a loop - just capture this
    {
        return "[this]";
    }
    // In loop: capture loop var by value so lambda survives loop iteration
    return "[this, " + loop_var_name + "]";
}

// Collect member reference names from view children (recursive)
static void collect_member_refs(ASTNode* node, std::vector<std::string>& refs) {
    if (auto comp = dynamic_cast<ComponentInstantiation*>(node)) {
        if (comp->is_member_reference) {
            refs.push_back(comp->member_name);
        }
    }
    if (auto el = dynamic_cast<HTMLElement*>(node)) {
        for (auto& child : el->children) {
            collect_member_refs(child.get(), refs);
        }
    }
    if (auto viewIf = dynamic_cast<ViewIfStatement*>(node)) {
        for (auto& child : viewIf->then_children) {
            collect_member_refs(child.get(), refs);
        }
        for (auto& child : viewIf->else_children) {
            collect_member_refs(child.get(), refs);
        }
    }
}

std::string TextNode::to_webcc() { return "\"" + text + "\""; }

std::string ComponentInstantiation::to_webcc() { return ""; }

void ComponentInstantiation::generate_code(std::stringstream &ss, const std::string &parent, int &counter,
                                           std::vector<EventHandler> &event_handlers,
                                           std::vector<Binding> &bindings,
                                           std::map<std::string, int> &component_counters,
                                           const std::set<std::string> &method_names,
                                           const std::string &parent_component_name,
                                           bool in_loop,
                                           std::vector<LoopRegion> *loop_regions,
                                           int *loop_counter,
                                           std::vector<IfRegion> *if_regions,
                                           int *if_counter,
                                           const std::string &loop_var_name)
{
    std::string instance_name;

    // Handle member reference (e.g., <a/> where "a" is a member variable of component type)
    if (is_member_reference)
    {
        instance_name = member_name;
        
        // Set props on the existing member
        for (auto &prop : props)
        {
            std::string val = prop.value->to_webcc();
            if (prop.is_callback && !prop.callback_param_types.empty())
            {
                // Callback with params: generate lambda that forwards args
                std::string lambda_params = build_lambda_params_from_types(prop.callback_param_types);
                std::string forward_args = build_forward_args(prop.callback_param_types.size());
                ss << "        " << instance_name << "." << prop.name << " = [this](" << lambda_params << ") { this->" << val << "(" << forward_args << "); };\n";
            }
            else if (method_names.count(val) || prop.is_callback)
            {
                // No-param callback or method reference
                ss << "        " << instance_name << "." << prop.name << " = [this]() { this->" << val << "(); };\n";
            }
            else if (prop.is_reference)
            {
                // Actual reference: pointer to variable
                ss << "        " << instance_name << "." << prop.name << " = &(" << val << ");\n";
            }
            else
            {
                ss << "        " << instance_name << "." << prop.name << " = " << val << ";\n";
            }
        }

        // Call view on the existing member (component persists, only view is re-rendered)
        if (!parent.empty())
        {
            ss << "        " << instance_name << ".view(" << parent << ");\n";
        }
        else
        {
            ss << "        " << instance_name << ".view();\n";
        }
        return;
    }

    int id = component_counters[component_name]++;

    if (in_loop)
    {
        std::string vector_name = "_loop_" + component_name + "s";
        instance_name = vector_name + "[" + vector_name + ".size() - 1]";
        ss << "        " << vector_name << ".push_back(" << component_name << "());\n";
        ss << "        auto& _inst = " << instance_name << ";\n";
        instance_name = "_inst";
    }
    else
    {
        instance_name = component_name + "_" + std::to_string(id);
    }

    // Set props
    for (auto &prop : props)
    {
        std::string val = prop.value->to_webcc();
        if (prop.is_callback && !prop.callback_param_types.empty())
        {
            // Callback with params: generate lambda that forwards args
            std::string lambda_params = build_lambda_params_from_types(prop.callback_param_types);
            std::string forward_args = build_forward_args(prop.callback_param_types.size());
            ss << "        " << instance_name << "." << prop.name << " = [this](" << lambda_params << ") { this->" << val << "(" << forward_args << "); };\n";
        }
        else if (method_names.count(val) || prop.is_callback)
        {
            // No-param callback or method reference
            ss << "        " << instance_name << "." << prop.name << " = [this]() { this->" << val << "(); };\n";
        }
        else if (prop.is_reference)
        {
            // Actual reference: pointer to variable
            ss << "        " << instance_name << "." << prop.name << " = &(" << val << ");\n";
        }
        else
        {
            ss << "        " << instance_name << "." << prop.name << " = " << val << ";\n";
        }
    }

    // For reference props, set up onChange callback
    if (!in_loop)
    {
        for (auto &prop : props)
        {
            if (prop.is_reference && prop.is_mutable_def)
            {
                std::string callback_name = "on" + std::string(1, std::toupper(prop.name[0])) + prop.name.substr(1) + "Change";

                std::set<std::string> prop_deps;
                prop.value->collect_dependencies(prop_deps);

                std::string update_calls;
                for (const auto &dep : prop_deps)
                {
                    bool has_dependent_binding = false;
                    for (const auto &binding : bindings)
                    {
                        if (binding.dependencies.count(dep))
                        {
                            has_dependent_binding = true;
                            break;
                        }
                    }
                    if (has_dependent_binding)
                    {
                        update_calls += "_update_" + dep + "(); ";
                    }
                }

                if (!update_calls.empty())
                {
                    ss << "        " << instance_name << "." << callback_name << " = [this]() { " << update_calls << "};\n";
                }
            }
        }
    }

    // Call view
    if (!parent.empty())
    {
        ss << "        " << instance_name << ".view(" << parent << ");\n";
    }
    else
    {
        ss << "        " << instance_name << ".view();\n";
    }
}

void ComponentInstantiation::collect_dependencies(std::set<std::string> &deps)
{
    for (auto &prop : props)
    {
        prop.value->collect_dependencies(deps);
    }
}

std::string HTMLElement::to_webcc() { return ""; }

// Helper to generate code for a view child node
// loop_var_name: empty at top-level, set to iterator name (e.g. "row") inside for-loops
static void generate_view_child(ASTNode *child, std::stringstream &ss, const std::string &parent, int &counter,
                                std::vector<EventHandler> &event_handlers,
                                std::vector<Binding> &bindings,
                                std::map<std::string, int> &component_counters,
                                const std::set<std::string> &method_names,
                                const std::string &parent_component_name,
                                bool in_loop = false,
                                std::vector<LoopRegion> *loop_regions = nullptr,
                                int *loop_counter = nullptr,
                                std::vector<IfRegion> *if_regions = nullptr,
                                int *if_counter = nullptr,
                                const std::string &loop_var_name = "")  // Propagated for lambda captures
{
    if (auto el = dynamic_cast<HTMLElement *>(child))
    {
        el->generate_code(ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
    }
    else if (auto comp = dynamic_cast<ComponentInstantiation *>(child))
    {
        comp->generate_code(ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
    }
    else if (auto viewIf = dynamic_cast<ViewIfStatement *>(child))
    {
        viewIf->generate_code(ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
    }
    else if (auto viewFor = dynamic_cast<ViewForRangeStatement *>(child))
    {
        viewFor->generate_code(ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
    }
    else if (auto viewForEach = dynamic_cast<ViewForEachStatement *>(child))
    {
        viewForEach->generate_code(ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
    }
    else if (auto routePlaceholder = dynamic_cast<RoutePlaceholder *>(child))
    {
        // Route placeholder - create anchor comment for inserting routed components
        ss << "        _route_parent = " << parent << ";\n";
        ss << "        _route_anchor = webcc::DOMElement(webcc::next_deferred_handle());\n";
        ss << "        webcc::dom::create_comment_deferred(_route_anchor, \"coi-route\");\n";
        ss << "        webcc::dom::append_child(" << parent << ", _route_anchor);\n";
    }
}

void HTMLElement::generate_code(std::stringstream &ss, const std::string &parent, int &counter,
                                std::vector<EventHandler> &event_handlers,
                                std::vector<Binding> &bindings,
                                std::map<std::string, int> &component_counters,
                                const std::set<std::string> &method_names,
                                const std::string &parent_component_name,
                                bool in_loop,
                                std::vector<LoopRegion> *loop_regions,
                                int *loop_counter,
                                std::vector<IfRegion> *if_regions,
                                int *if_counter,
                                const std::string &loop_var_name)
{
    int my_id = counter++;
    std::string var;

    bool has_scoped_css = g_components_with_scoped_css.count(parent_component_name) > 0;

    if (in_loop)
    {
        // In loops, use local variable but still deferred creation
        var = "_el_" + std::to_string(my_id);
        ss << "        webcc::handle " << var << " = webcc::handle(webcc::next_deferred_handle());\n";
        if (has_scoped_css) {
            ss << "        webcc::dom::create_element_deferred_scoped(" << var << ", \"" << tag << "\", \"" << parent_component_name << "\");\n";
        } else {
            ss << "        webcc::dom::create_element_deferred(" << var << ", \"" << tag << "\");\n";
        }
    }
    else
    {
        // Outside loops, store in el[] array with deferred creation
        var = "el[" + std::to_string(my_id) + "]";
        ss << "        " << var << " = webcc::DOMElement(webcc::next_deferred_handle());\n";
        if (has_scoped_css) {
            ss << "        webcc::dom::create_element_deferred_scoped(" << var << ", \"" << tag << "\", \"" << parent_component_name << "\");\n";
        } else {
            ss << "        webcc::dom::create_element_deferred(" << var << ", \"" << tag << "\");\n";
        }
    }

    if (!ref_binding.empty())
    {
        ss << "        " << ref_binding << " = " << var << ";\n";
    }

    // Attributes
    for (auto &attr : attributes)
    {
        if (attr.name == "onclick")
        {
            ss << "        webcc::dom::add_click_listener(" << var << ");\n";
            bool is_call = dynamic_cast<FunctionCall *>(attr.value.get()) != nullptr;
            event_handlers.push_back({my_id, "click", attr.value->to_webcc(), is_call});
        }
        else if (attr.name == "oninput")
        {
            ss << "        webcc::dom::add_input_listener(" << var << ");\n";
            bool is_call = dynamic_cast<FunctionCall *>(attr.value.get()) != nullptr;
            event_handlers.push_back({my_id, "input", attr.value->to_webcc(), is_call});
        }
        else if (attr.name == "onchange")
        {
            ss << "        webcc::dom::add_change_listener(" << var << ");\n";
            bool is_call = dynamic_cast<FunctionCall *>(attr.value.get()) != nullptr;
            event_handlers.push_back({my_id, "change", attr.value->to_webcc(), is_call});
        }
        else if (attr.name == "onkeydown")
        {
            ss << "        webcc::dom::add_keydown_listener(" << var << ");\n";
            bool is_call = dynamic_cast<FunctionCall *>(attr.value.get()) != nullptr;
            event_handlers.push_back({my_id, "keydown", attr.value->to_webcc(), is_call});
        }
        else
        {
            std::string val = attr.value->to_webcc();
            ss << "        webcc::dom::set_attribute(" << var << ", \"" << attr.name << "\", " << val << ");\n";

            if (!attr.value->is_static() && !in_loop)
            {
                Binding b;
                b.element_id = my_id;
                b.type = "attr";
                b.name = attr.name;
                b.value_code = val;
                b.expr = attr.value.get();
                attr.value->collect_dependencies(b.dependencies);
                attr.value->collect_member_dependencies(b.member_dependencies);
                bindings.push_back(b);
            }
        }
    }

    // Append to parent
    if (!parent.empty())
    {
        ss << "        webcc::dom::append_child(" << parent << ", " << var << ");\n";
    }

    // Children
    bool has_elements = false;
    for (auto &child : children)
    {
        if (dynamic_cast<HTMLElement *>(child.get()) || dynamic_cast<ComponentInstantiation *>(child.get()) ||
            dynamic_cast<ViewIfStatement *>(child.get()) || dynamic_cast<ViewForRangeStatement *>(child.get()) ||
            dynamic_cast<ViewForEachStatement *>(child.get()))
            has_elements = true;
    }

    if (has_elements)
    {
        // Check if there's exactly one child and it's a for-each loop
        if (children.size() == 1) {
            if (auto forEach = dynamic_cast<ViewForEachStatement *>(children[0].get())) {
                forEach->is_only_child = true;
            }
        }
        for (auto &child : children)
        {
            generate_view_child(child.get(), ss, var, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
        }
    }
    else
    {
        // Text content
        std::string code;
        bool all_static = true;
        bool generated_inline = false;

        for (auto &child : children)
        {
            std::string c = child->to_webcc();
            if (!(c.size() >= 2 && c.front() == '"' && c.back() == '"'))
            {
                all_static = false;
                break;
            }
        }

        if (children.size() == 1 && all_static)
        {
            code = children[0]->to_webcc();
        }
        else if (children.size() == 1 && !all_static)
        {
            generated_inline = true;
            std::vector<std::string> parts = {children[0]->to_webcc()};
            ss << "        " << generate_formatter_block(parts, "webcc::dom::set_inner_text(" + var + ", ") << "\n";
        }
        else if (children.size() > 1)
        {
            if (all_static)
            {
                std::string args;
                bool first = true;
                for (auto &child : children)
                {
                    if (!first)
                        args += ", ";
                    args += child->to_webcc();
                    first = false;
                }
                code = "webcc::string::concat(" + args + ")";
            }
            else
            {
                generated_inline = true;
                std::vector<std::string> parts;
                for (auto &child : children)
                {
                    parts.push_back(child->to_webcc());
                }
                ss << "        " << generate_formatter_block(parts, "webcc::dom::set_inner_text(" + var + ", ") << "\n";
            }
        }

        if (!code.empty())
        {
            ss << "        webcc::dom::set_inner_text(" << var << ", " << code << ");\n";
        }

        if (!all_static && !in_loop)
        {
            Binding b;
            b.element_id = my_id;
            b.type = "text";
            if (children.size() == 1)
            {
                b.expr = dynamic_cast<Expression *>(children[0].get());
            }
            std::string args;
            bool first = true;
            for (auto &child : children)
            {
                if (!first)
                    args += ", ";
                args += child->to_webcc();
                first = false;
            }
            b.value_code = (children.size() == 1) ? children[0]->to_webcc() : "webcc::string::concat(" + args + ")";
            for (auto &child : children) {
                child->collect_dependencies(b.dependencies);
                child->collect_member_dependencies(b.member_dependencies);
            }
            bindings.push_back(b);
        }
    }
}

void HTMLElement::collect_dependencies(std::set<std::string> &deps)
{
    for (auto &attr : attributes)
    {
        if (attr.value)
            attr.value->collect_dependencies(deps);
    }
    for (auto &child : children)
    {
        child->collect_dependencies(deps);
    }
}

// Generate prop assignments for components inside loops (used in sync functions)
// loop_var_name passed to capture loop iterator in callback lambdas
static void generate_prop_update_code(std::stringstream &ss, ComponentInstantiation *comp,
                                      const std::string &inst_ref,
                                      const std::set<std::string> &method_names,
                                      const std::string &loop_var_name = "")
{
    for (auto &prop : comp->props)
    {
        std::string val = prop.value->to_webcc();
        std::string prefix = "            " + inst_ref + "." + prop.name + " = ";

        if (prop.is_callback && !prop.callback_param_types.empty())
        {
            // Callback with params: generate lambda that forwards args
            std::string lambda_params = build_lambda_params_from_types(prop.callback_param_types);
            std::string forward_args = build_forward_args(prop.callback_param_types.size());
            ss << prefix << "[this](" << lambda_params << ") { this->" << val << "(" << forward_args << "); };\n";
        }
        else if (method_names.count(val) || prop.is_callback)
        {
            // No-param callback or method reference
            ss << prefix << "[this]() { this->" << val << "(); };\n";
        }
        else if (prop.is_reference)
        {
            // Actual reference: pointer to variable
            ss << prefix << "&(" << val << ");\n";
            ss << "            " << inst_ref << "._update_" << prop.name << "();\n";
        }
        else
        {
            ss << prefix << val << ";\n";
            ss << "            " << inst_ref << "._update_" << prop.name << "();\n";
        }
    }
}

// ViewIfStatement
void ViewIfStatement::generate_code(std::stringstream &ss, const std::string &parent, int &counter,
                                    std::vector<EventHandler> &event_handlers,
                                    std::vector<Binding> &bindings,
                                    std::map<std::string, int> &component_counters,
                                    const std::set<std::string> &method_names,
                                    const std::string &parent_component_name,
                                    bool in_loop,
                                    std::vector<LoopRegion> *loop_regions,
                                    int *loop_counter,
                                    std::vector<IfRegion> *if_regions,
                                    int *if_counter,
                                    const std::string &loop_var_name)
{

    // Simple static if for nested loops
    if (in_loop || !if_regions || !if_counter)
    {
        int loop_id_before = loop_counter ? *loop_counter : 0;

        ss << "        if (" << strip_outer_parens(condition->to_webcc()) << ") {\n";
        for (auto &child : then_children)
        {
            generate_view_child(child.get(), ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
        }
        if (!else_children.empty())
        {
            ss << "        } else {\n";
            for (auto &child : else_children)
            {
                generate_view_child(child.get(), ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
            }
        }
        ss << "        }\n";

        if (loop_counter && loop_regions)
        {
            int loop_id_after = *loop_counter;
            for (int lid = loop_id_before; lid < loop_id_after; lid++)
            {
                ss << "        _loop_" << lid << "_parent = " << parent << ";\n";
            }
        }
        return;
    }

    // Reactive if/else
    int my_if_id = (*if_counter)++;
    if_id = my_if_id;

    IfRegion region;
    region.if_id = my_if_id;
    region.condition_code = condition->to_webcc();
    condition->collect_dependencies(region.dependencies);
    condition->collect_member_dependencies(region.member_dependencies);

    std::string if_parent = "_if_" + std::to_string(my_if_id) + "_parent";

    int counter_before_then = counter;
    int loop_id_before = loop_counter ? *loop_counter : 0;
    int if_id_before = *if_counter;
    std::map<std::string, int> comp_counters_before_then = component_counters;

    std::stringstream then_ss;
    std::vector<Binding> then_bindings;
    for (auto &child : then_children)
    {
        generate_view_child(child.get(), then_ss, if_parent, counter, event_handlers, then_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
    }
    int counter_after_then = counter;
    int loop_id_after_then = loop_counter ? *loop_counter : 0;
    int if_id_after_then = *if_counter;

    for (int i = counter_before_then; i < counter_after_then; i++)
    {
        region.then_element_ids.push_back(i);
    }
    for (int i = loop_id_before; i < loop_id_after_then; i++)
    {
        region.then_loop_ids.push_back(i);
    }
    for (int i = if_id_before; i < if_id_after_then; i++)
    {
        region.then_if_ids.push_back(i);
    }
    for (auto &[comp_name, count] : component_counters)
    {
        int before = comp_counters_before_then.count(comp_name) ? comp_counters_before_then[comp_name] : 0;
        for (int i = before; i < count; i++)
        {
            region.then_components.push_back({comp_name, i});
        }
    }
    
    // Collect member references in then branch
    for (auto &child : then_children) {
        collect_member_refs(child.get(), region.then_member_refs);
    }

    region.then_creation_code = then_ss.str();

    int counter_before_else = counter;
    int loop_id_before_else = loop_counter ? *loop_counter : 0;
    int if_id_before_else = *if_counter;
    std::map<std::string, int> comp_counters_before_else = component_counters;

    std::stringstream else_ss;
    std::vector<Binding> else_bindings;
    if (!else_children.empty())
    {
        for (auto &child : else_children)
        {
            generate_view_child(child.get(), else_ss, if_parent, counter, event_handlers, else_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter, loop_var_name);
        }
    }
    int counter_after_else = counter;
    int loop_id_after_else = loop_counter ? *loop_counter : 0;
    int if_id_after_else = *if_counter;

    for (int i = counter_before_else; i < counter_after_else; i++)
    {
        region.else_element_ids.push_back(i);
    }
    for (int i = loop_id_before_else; i < loop_id_after_else; i++)
    {
        region.else_loop_ids.push_back(i);
    }
    for (int i = if_id_before_else; i < if_id_after_else; i++)
    {
        region.else_if_ids.push_back(i);
    }
    for (auto &[comp_name, count] : component_counters)
    {
        int before = comp_counters_before_else.count(comp_name) ? comp_counters_before_else[comp_name] : 0;
        for (int i = before; i < count; i++)
        {
            region.else_components.push_back({comp_name, i});
        }
    }
    
    // Collect member references in else branch
    for (auto &child : else_children) {
        collect_member_refs(child.get(), region.else_member_refs);
    }

    // Transform creation code to use insert_before with anchor for _sync operations
    std::string if_anchor = "_if_" + std::to_string(my_if_id) + "_anchor";
    region.then_creation_code = transform_to_insert_before(then_ss.str(), if_parent, if_anchor);
    region.else_creation_code = transform_to_insert_before(else_ss.str(), if_parent, if_anchor);

    for (auto &b : then_bindings)
    {
        b.if_region_id = my_if_id;
        b.in_then_branch = true;
        bindings.push_back(b);
    }
    for (auto &b : else_bindings)
    {
        b.if_region_id = my_if_id;
        b.in_then_branch = false;
        bindings.push_back(b);
    }

    // Create anchor comment and append to parent
    ss << "        _if_" << my_if_id << "_parent = " << parent << ";\n";
    // Use deferred creation for comment anchors
    ss << "        _if_" << my_if_id << "_anchor = webcc::DOMElement(webcc::next_deferred_handle());\n";
    ss << "        webcc::dom::create_comment_deferred(_if_" << my_if_id << "_anchor, \"coi-⚓\");\n";
    ss << "        if (" << strip_outer_parens(region.condition_code) << ") {\n";
    ss << "        _if_" << my_if_id << "_state = true;\n";
    // Use original append_child for initial render (before anchor is in DOM)
    ss << then_ss.str();
    ss << "        } else {\n";
    ss << "        _if_" << my_if_id << "_state = false;\n";
    ss << else_ss.str();
    ss << "        }\n";
    // Append anchor after the conditional content
    ss << "        webcc::dom::append_child(" << parent << ", _if_" << my_if_id << "_anchor);\n";

    if (loop_counter && loop_regions)
    {
        for (int lid = loop_id_before; lid < loop_id_after_else; lid++)
        {
            ss << "        _loop_" << lid << "_parent = " << parent << ";\n";
        }
    }

    if_regions->push_back(region);
}

void ViewIfStatement::collect_dependencies(std::set<std::string> &deps)
{
    condition->collect_dependencies(deps);
    for (auto &child : then_children)
        child->collect_dependencies(deps);
    for (auto &child : else_children)
        child->collect_dependencies(deps);
}

// ViewForRangeStatement
void ViewForRangeStatement::generate_code(std::stringstream &ss, const std::string &parent, int &counter,
                                          std::vector<EventHandler> &event_handlers,
                                          std::vector<Binding> &bindings,
                                          std::map<std::string, int> &component_counters,
                                          const std::set<std::string> &method_names,
                                          const std::string &parent_component_name,
                                          bool in_loop,
                                          std::vector<LoopRegion> *loop_regions,
                                          int *loop_counter,
                                          std::vector<IfRegion> *if_regions,
                                          int *if_counter,
                                          const std::string &loop_var_name)
{

    if (in_loop || !loop_regions || !loop_counter)
    {
        ss << "        for (int " << var_name << " = " << start->to_webcc() << "; "
           << var_name << " < " << end->to_webcc() << "; " << var_name << "++) {\n";
        for (auto &child : children)
        {
            generate_view_child(child.get(), ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, true, nullptr, nullptr, nullptr, nullptr, var_name);
        }
        ss << "        }\n";
        return;
    }

    int my_loop_id = (*loop_counter)++;
    loop_id = my_loop_id;

    LoopRegion region;
    region.loop_id = my_loop_id;
    region.parent_element = parent;
    region.start_expr = start->to_webcc();
    region.end_expr = end->to_webcc();
    region.var_name = var_name;

    start->collect_dependencies(region.dependencies);
    end->collect_dependencies(region.dependencies);

    ComponentInstantiation *loop_component = nullptr;
    HTMLElement *loop_html_element = nullptr;
    for (auto &child : children)
    {
        if (auto comp = dynamic_cast<ComponentInstantiation *>(child.get()))
        {
            region.component_type = comp->component_name;
            loop_component = comp;
            break;
        }
        if (auto el = dynamic_cast<HTMLElement *>(child.get()))
        {
            loop_html_element = el;
            region.is_html_loop = true;
            break;
        }
    }

    std::string loop_parent_var = "_loop_" + std::to_string(my_loop_id) + "_parent";
    std::stringstream item_ss;
    int temp_counter = counter;
    std::map<std::string, int> temp_comp_counters = component_counters;
    int root_element_id = temp_counter;

    for (auto &child : children)
    {
        generate_view_child(child.get(), item_ss, loop_parent_var, temp_counter, event_handlers, bindings, temp_comp_counters, method_names, parent_component_name, true, nullptr, nullptr, nullptr, nullptr, var_name);
    }
    region.item_creation_code = item_ss.str();

    if (region.is_html_loop && loop_html_element)
    {
        region.root_element_var = "_el_" + std::to_string(root_element_id);
    }

    // Generate item update code
    if (loop_component && !region.component_type.empty())
    {
        std::stringstream update_ss;
        std::string vec_name = "_loop_" + region.component_type + "s";
        std::string inst_ref = vec_name + "[" + var_name + "]";
        generate_prop_update_code(update_ss, loop_component, inst_ref, method_names, var_name);
        region.item_update_code = update_ss.str();
    }

    loop_regions->push_back(region);

    ss << "        _loop_" << my_loop_id << "_parent = " << parent << ";\n";
    ss << "        _sync_loop_" << my_loop_id << "();\n";
}

void ViewForRangeStatement::collect_dependencies(std::set<std::string> &deps)
{
    start->collect_dependencies(deps);
    end->collect_dependencies(deps);
    for (auto &child : children)
        child->collect_dependencies(deps);
}

// ViewForEachStatement
void ViewForEachStatement::generate_code(std::stringstream &ss, const std::string &parent, int &counter,
                                         std::vector<EventHandler> &event_handlers,
                                         std::vector<Binding> &bindings,
                                         std::map<std::string, int> &component_counters,
                                         const std::set<std::string> &method_names,
                                         const std::string &parent_component_name,
                                         bool in_loop,
                                         std::vector<LoopRegion> *loop_regions,
                                         int *loop_counter,
                                         std::vector<IfRegion> *if_regions,
                                         int *if_counter,
                                         const std::string &loop_var_name)
{

    if (in_loop || !key_expr || !loop_regions || !loop_counter)
    {
        ss << "        for (auto& " << var_name << " : " << iterable->to_webcc() << ") {\n";
        for (auto &child : children)
        {
            generate_view_child(child.get(), ss, parent, counter, event_handlers, bindings, component_counters, method_names, parent_component_name, true, nullptr, nullptr, nullptr, nullptr, var_name);
        }
        ss << "        }\n";
        return;
    }

    int my_loop_id = (*loop_counter)++;
    loop_id = my_loop_id;

    LoopRegion region;
    region.loop_id = my_loop_id;
    region.parent_element = parent;
    region.is_keyed = true;
    region.is_only_child = is_only_child;
    region.key_expr = key_expr->to_webcc();
    region.var_name = var_name;
    region.iterable_expr = iterable->to_webcc();

    iterable->collect_dependencies(region.dependencies);

    ComponentInstantiation *loop_component = nullptr;
    HTMLElement *loop_html_element = nullptr;
    for (auto &child : children)
    {
        if (auto comp = dynamic_cast<ComponentInstantiation *>(child.get()))
        {
            region.component_type = comp->component_name;
            // Check if this is a member reference loop (e.g., <row/> where row is loop var)
            if (comp->is_member_reference && comp->member_name == var_name)
            {
                region.is_member_ref_loop = true;
            }
            loop_component = comp;
            break;
        }
        if (auto el = dynamic_cast<HTMLElement *>(child.get()))
        {
            loop_html_element = el;
            region.is_html_loop = true;
            break;
        }
    }

    std::string loop_parent_var = "_loop_" + std::to_string(my_loop_id) + "_parent";
    std::stringstream item_ss;
    int temp_counter = counter;
    std::map<std::string, int> temp_comp_counters = component_counters;
    int root_element_id = temp_counter;

    for (auto &child : children)
    {
        generate_view_child(child.get(), item_ss, loop_parent_var, temp_counter, event_handlers, bindings, temp_comp_counters, method_names, parent_component_name, true, nullptr, nullptr, nullptr, nullptr, var_name);
    }
    region.item_creation_code = item_ss.str();

    if (region.is_html_loop && loop_html_element)
    {
        region.root_element_var = "_el_" + std::to_string(root_element_id);
    }

    // Generate item update code
    if (loop_component && !region.component_type.empty())
    {
        std::stringstream update_ss;
        generate_prop_update_code(update_ss, loop_component, var_name, method_names, var_name);
        region.item_update_code = update_ss.str();
    }

    region.key_type = "int";

    loop_regions->push_back(region);

    ss << "        _loop_" << my_loop_id << "_parent = " << parent << ";\n";
    ss << "        _sync_loop_" << my_loop_id << "();\n";
}

void ViewForEachStatement::collect_dependencies(std::set<std::string> &deps)
{
    iterable->collect_dependencies(deps);
    if (key_expr)
        key_expr->collect_dependencies(deps);
    for (auto &child : children)
        child->collect_dependencies(deps);
}
