#include "statements.h"
#include "../defs/def_parser.h"
#include "../codegen/codegen_utils.h"

// Reference to per-component context for reference props
extern std::set<std::string> g_ref_props;

// Current assignment target for WebSocket lifetime tracking
extern std::string g_ws_assignment_target;

// Info for inlining DOM operations on component arrays used in for-each loops
struct ComponentArrayLoopInfo
{
    int loop_id;
    std::string component_type;
    std::string parent_var;
    std::string var_name;
    std::string item_creation_code;
    bool is_member_ref_loop;
    bool is_only_child;
};
extern std::map<std::string, ComponentArrayLoopInfo> g_component_array_loops;

// Info for inlining DOM operations on keyed HTML loops over non-component arrays
struct ArrayLoopInfo
{
    int loop_id;
    std::string parent_var;
    std::string anchor_var;
    std::string elements_vec_name;
    std::string var_name;
    std::string item_creation_code;
    std::string root_element_var;
    bool is_only_child;
};
extern std::map<std::string, ArrayLoopInfo> g_array_loops;

struct HtmlLoopVarInfo
{
    int loop_id;
    std::string iterable_expr;
};
extern std::map<std::string, HtmlLoopVarInfo> g_html_loop_var_infos;

std::string VarDeclaration::to_webcc()
{
    ComponentTypeContext::instance().set_method_symbol_type(name, type);

    // Special handling for ArrayRepeatLiteral
    if (auto repeat = dynamic_cast<ArrayRepeatLiteral *>(initializer.get()))
    {
        std::string elem_type = type;
        std::string fixed_size;
        
        // Strip array suffix to get element type
        if (elem_type.ends_with("[]"))
        {
            // Dynamic array type: int[]
            elem_type = elem_type.substr(0, elem_type.length() - 2);
        }
        else
        {
            // Fixed-size array type: int[N] - extract N and strip it
            size_t bracket_pos = elem_type.rfind('[');
            if (bracket_pos != std::string::npos && elem_type.back() == ']')
            {
                fixed_size = elem_type.substr(bracket_pos + 1, elem_type.length() - bracket_pos - 2);
                elem_type = elem_type.substr(0, bracket_pos);
            }
        }
        
        // Use count from repeat initializer (overrides fixed_size if both present)
        std::string count_str = repeat->count->to_webcc();
        std::string arr_type = "webcc::array<" + convert_type(elem_type) + ", " + count_str + ">";
        
        if (is_mutable)
        {
            // Mutable: declare then fill
            std::string result = arr_type + " " + name + "; ";
            result += name + ".fill(" + repeat->value->to_webcc() + ");";
            return result;
        }
        else
        {
            // Immutable: use IIFE to initialize const array
            std::string result = "const " + arr_type + " " + name + " = []{ ";
            result += arr_type + " _tmp; _tmp.fill(" + repeat->value->to_webcc() + "); return _tmp; }();";
            return result;
        }
    }

    // Dynamic array literal initializer
    if (auto arr_lit = dynamic_cast<ArrayLiteral *>(initializer.get()))
    {
        if (type.ends_with("[]"))
        {
            std::string elem_type = type.substr(0, type.length() - 2);
            
            // Propagate element type to anonymous struct literals
            arr_lit->propagate_element_type(elem_type);
            
            // Optimization: If immutable and initialized with literal, use fixed-size array
            // No need for dynamic allocation if we know the size at compile time and can't modify
            if (!is_mutable)
            {
                size_t count = arr_lit->elements.size();
                std::string arr_type = "webcc::array<" + convert_type(elem_type) + ", " + std::to_string(count) + ">";
                std::string result = "const " + arr_type;
                if (is_reference)
                    result += "&";
                result += " " + name + " = " + arr_lit->to_webcc() + ";";
                return result;
            }
            
            // Mutable dynamic array - use vector with brace init (variadic constructor)
            std::string vec_type = "webcc::vector<" + convert_type(elem_type) + ">";

            std::string result = vec_type;
            if (is_reference)
                result += "&";
            result += " " + name + " = " + arr_lit->to_webcc() + ";";
            return result;
        }
    }

    std::string result = (is_mutable ? "" : "const ") + convert_type(type);
    if (is_reference)
        result += "&";
    result += " " + name;
    if (initializer)
    {
        // Set WebSocket assignment target for lifetime tracking (auto-invalidate on close/error)
        if (type == "WebSocket") {
            g_ws_assignment_target = name;
        }
        
        std::string init_code = initializer->to_webcc();
        
        // Clear the target after generating the initializer
        g_ws_assignment_target.clear();
        
        // Wrap in webcc::move() if this is a move assignment (:=)
        if (is_move)
        {
            init_code = "webcc::move(" + init_code + ")";
        }
        
        if (DefSchema::instance().is_handle(type))
        {
            result += "{" + init_code + "}";
        }
        else
        {
            result += " = " + init_code;
        }
    }
    result += ";";
    return result;
}

std::string ComponentParam::to_webcc()
{
    return "";
}

std::string Assignment::to_webcc()
{
    std::string lhs = name;
    if (g_ref_props.count(name))
    {
        lhs = "(*" + name + ")";
    }

    // Set WebSocket assignment target for lifetime tracking (auto-invalidate on close/error)
    if (target_type == "WebSocket") {
        g_ws_assignment_target = name;
    }

    std::string rhs = value->to_webcc();
    
    // Clear the target after generating the RHS
    g_ws_assignment_target.clear();

    // Wrap in webcc::move() for move assignments
    if (is_move)
    {
        rhs = "webcc::move(" + rhs + ")";
    }

    if (!target_type.empty() && DefSchema::instance().is_handle(target_type))
    {
        rhs = convert_type(target_type) + "((int32_t)" + rhs + ")";
    }

    // For component array FULL REASSIGNMENT (arr = newArr), we need to:
    // 1. Remove old items' views BEFORE assignment (unregister event handlers + remove DOM)
    // 2. Do the assignment
    // 3. Re-render all items in the new array with fresh handles
    auto it = g_component_array_loops.find(name);
    if (it != g_component_array_loops.end() && it->second.is_member_ref_loop)
    {
        const auto &info = it->second;
        std::string var = info.var_name;
        std::string parent_var = "_loop_" + std::to_string(info.loop_id) + "_parent";
        std::string count_var = "_loop_" + std::to_string(info.loop_id) + "_count";
        std::string result;

        // Remove old views BEFORE assignment (unregisters event handlers from dispatchers)
        result += "if (" + count_var + " > 0) {\n";
        if (info.is_only_child)
        {
            // Bulk optimization: unregister handlers only, then clear parent's innerHTML
            result += "    for (auto& " + var + " : " + name + ") { " + var + "._remove_view(true); }\n";
            result += "    webcc::dom::set_inner_html(" + parent_var + ", \"\");\n";
        }
        else
        {
            // Normal path: each _remove_view removes its own DOM element
            result += "    for (auto& " + var + " : " + name + ") { " + var + "._remove_view(); }\n";
        }
        result += "}\n";

        // Do the assignment
        result += lhs + " = " + rhs + ";\n";

        // Re-render all items in the new array with fresh handles
        result += count_var + " = (int)" + name + ".size();\n";
        result += "g_view_depth++;\n";
        result += "for (auto& " + var + " : " + name + ") {\n";
        result += info.item_creation_code;
        result += "}\n";
        result += "if (--g_view_depth == 0) webcc::flush();";
        return result;
    }

    return lhs + " = " + rhs + ";";
}

void Assignment::collect_dependencies(std::set<std::string> &deps)
{
    value->collect_dependencies(deps);
}

std::string IndexAssignment::to_webcc()
{
    std::string val = value->to_webcc();
    
    // Wrap in webcc::move() for move assignments
    if (is_move)
    {
        val = "webcc::move(" + val + ")";
    }

    // Check if this is an index assignment on a component array with inline loop
    if (auto id = dynamic_cast<Identifier *>(array.get()))
    {
        auto it = g_component_array_loops.find(id->name);
        if (it != g_component_array_loops.end() && it->second.is_member_ref_loop)
        {
            // For component arrays, we need to:
            // 1. Do the data swap/assignment
            // 2. Move the DOM node to its correct position
            std::string arr = array->to_webcc();
            std::string idx = index->to_webcc();
            std::string parent_var = it->second.parent_var;

            std::string result;
            if (compound_op.empty())
            {
                result = arr + "[" + idx + "] = " + val + ";\n";
            }
            else
            {
                result = arr + "[" + idx + "] = " + arr + "[" + idx + "] " + compound_op + " " + val + ";\n";
            }
            // Move the DOM node to correct position
            // Get the element that should be after this one (or null if at end)
            result += "{ int _idx = " + idx + ";\n";
            result += "  webcc::handle _node = " + arr + "[_idx]._get_root_element();\n";
            result += "  webcc::handle _ref = (_idx + 1 < (int)" + arr + ".size()) ? " + arr + "[_idx + 1]._get_root_element() : webcc::handle{0};\n";
            result += "  webcc::dom::move_before(" + parent_var + ", _node, _ref);\n";
            result += "}";
            return result;
        }
    }

    if (compound_op.empty())
    {
        return array->to_webcc() + "[" + index->to_webcc() + "] = " + val + ";";
    }
    else
    {
        std::string arr = array->to_webcc();
        std::string idx = index->to_webcc();
        return arr + "[" + idx + "] = " + arr + "[" + idx + "] " + compound_op + " " + val + ";";
    }
}

void IndexAssignment::collect_dependencies(std::set<std::string> &deps)
{
    array->collect_dependencies(deps);
    index->collect_dependencies(deps);
    value->collect_dependencies(deps);
}

std::string MemberAssignment::to_webcc()
{
    std::string val = value->to_webcc();
    
    // Wrap in webcc::move() for move assignments
    if (is_move)
    {
        val = "webcc::move(" + val + ")";
    }

    std::string obj = object->to_webcc();
    std::string result;
    if (compound_op.empty())
    {
        result = obj + "." + member + " = " + val + ";";
    }
    else
    {
        result = obj + "." + member + " = " + obj + "." + member + " " + compound_op + " " + val + ";";
    }

    // Fast path: if assigning to a keyed HTML loop item member (e.g., task.status = ...),
    // patch only that loop item instead of re-syncing the whole loop.
    Expression *root = object.get();
    while (auto member_acc = dynamic_cast<MemberAccess *>(root))
    {
        root = member_acc->object.get();
    }
    if (auto id = dynamic_cast<Identifier *>(root))
    {
        auto it = g_html_loop_var_infos.find(id->name);
        if (it != g_html_loop_var_infos.end())
        {
            const auto &info = it->second;
            std::string idx_var = "__coi_loop_idx_" + id->name;
            result += "\n{\n";
            result += "    int " + idx_var + " = -1;\n";
            result += "    for (int __i = 0; __i < (int)" + info.iterable_expr + ".size(); __i++) {\n";
            result += "        if (&" + info.iterable_expr + "[__i] == &" + id->name + ") { " + idx_var + " = __i; break; }\n";
            result += "    }\n";
            result += "    if (" + idx_var + " >= 0) _sync_loop_" + std::to_string(info.loop_id) + "_item(" + idx_var + ");\n";
            result += "}";
        }
    }

    return result;
}

void MemberAssignment::collect_dependencies(std::set<std::string> &deps)
{
    object->collect_dependencies(deps);
    value->collect_dependencies(deps);
}

std::string ReturnStatement::to_webcc()
{
    if (returns_tuple()) {
        // Return tuple using aggregate initialization: return {a, b};
        std::string result = "return {";
        for (size_t i = 0; i < tuple_values.size(); i++) {
            if (i > 0) result += ", ";
            result += tuple_values[i]->to_webcc();
        }
        result += "};";
        return result;
    }
    if (value)
        return "return " + value->to_webcc() + ";";
    return "return;";
}

void ReturnStatement::collect_dependencies(std::set<std::string> &deps)
{
    if (value)
        value->collect_dependencies(deps);
    for (const auto& val : tuple_values)
        val->collect_dependencies(deps);
}

std::string TupleDestructuring::to_webcc()
{
    // Generate internal structured binding, then user variables with per-element mutability.
    // This avoids making all bindings mutable by default.
    static size_t tuple_bind_counter = 0;
    size_t bind_id = tuple_bind_counter++;

    std::string result;
    result += "auto [";
    for (size_t i = 0; i < elements.size(); i++) {
        if (i > 0) result += ", ";
        result += "__coi_tuple_bind_" + std::to_string(bind_id) + "_" + std::to_string(i);
    }
    result += "] = " + value->to_webcc() + "; ";

    for (size_t i = 0; i < elements.size(); i++) {
        const auto& elem = elements[i];
        // Ignore internal placeholder bindings generated for unnamed tuple elements.
        if (elem.name.rfind("__coi_ignore_tuple_", 0) == 0) {
            continue;
        }
        result += (elem.is_mutable ? "auto " : "const auto ");
        result += elem.name + " = __coi_tuple_bind_" + std::to_string(bind_id) + "_" + std::to_string(i) + "; ";
    }

    return result;
}

void TupleDestructuring::collect_dependencies(std::set<std::string> &deps)
{
    value->collect_dependencies(deps);
}

std::string ExpressionStatement::to_webcc()
{
    // Check for array method calls on component arrays used in loops
    if (auto call = dynamic_cast<FunctionCall *>(expression.get()))
    {
        size_t dot_pos = call->name.rfind('.');
        if (dot_pos != std::string::npos)
        {
            std::string obj_expr = call->name.substr(0, dot_pos);
            std::string method = call->name.substr(dot_pos + 1);
            
            // Check if this is a mutating method call on a component field
            // e.g., rows[i].label.append() -> need to call rows[i]._update_label()
            // Pattern: arrayName[index].fieldName.mutatingMethod()
            size_t second_dot = obj_expr.rfind('.');
            if (second_dot != std::string::npos)
            {
                std::string component_expr = obj_expr.substr(0, second_dot); // e.g., "rows[i]"
                std::string field_name = obj_expr.substr(second_dot + 1);     // e.g., "label"
                
                // Check if this is a mutating method (returns void)
                std::vector<std::string> types_to_check = {"string", "array", "int", "float", "bool"};
                bool is_mutating = false;
                for (const auto& type : types_to_check)
                {
                    auto* method_def = DefSchema::instance().lookup_method(type, method);
                    if (method_def && method_def->return_type == "void")
                    {
                        is_mutating = true;
                        break;
                    }
                }
                
                if (is_mutating)
                {
                    // Generate: field mutation + component update call
                    std::string result = call->to_webcc() + ";\n";
                    result += component_expr + "._update_" + field_name + "();\n";
                    return result;
                }
            }

            std::string arr_name = obj_expr;
            auto it = g_component_array_loops.find(arr_name);
            if (it != g_component_array_loops.end() && it->second.is_member_ref_loop)
            {
                const auto &info = it->second;
                std::string var = info.var_name; // Use original loop variable name
                std::string result;

                if (method == "push" && call->args.size() == 1)
                {
                    // arr.push(item) -> add to array, bind callbacks, render view if parent exists
                    // IMPORTANT: push_back may reallocate the vector, invalidating all existing
                    // items' `this` pointers in their registered event handlers. We must rebind
                    // all existing items after push_back to update their handler lambdas.
                    std::string item_expr = call->args[0].value->to_webcc();
                    std::string parent_var = "_loop_" + std::to_string(info.loop_id) + "_parent";
                    std::string count_var = "_loop_" + std::to_string(info.loop_id) + "_count";
                    result = "{\n";
                    result += "int _old_count = (int)" + arr_name + ".size();\n";
                    result += arr_name + ".push_back(" + item_expr + ");\n";
                    // Only render view if parent container exists (not during init)
                    result += "if (" + parent_var + ".is_valid()) {\n";
                    // (IMPORTANT!!!) Rebind all existing items in case vector reallocated
                    result += "    for (int _i = 0; _i < _old_count; _i++) " + arr_name + "[_i]._rebind();\n";
                    result += "    auto& " + var + " = " + arr_name + "[" + arr_name + ".size() - 1];\n";
                    // Inject the item creation code (callback bindings + view call)
                    result += info.item_creation_code;
                    result += "    " + count_var + "++;\n";
                    result += "}\n";
                    result += "}\n";
                    return result;
                }
                else if (method == "pop" && call->args.empty())
                {
                    // arr.pop() -> remove view then pop from array
                    result = "if (!" + arr_name + ".empty()) {\n";
                    result += "    " + arr_name + ".back()._remove_view();\n";
                    result += "    " + arr_name + ".pop_back();\n";
                    result += "}\n";
                    return result;
                }
                else if (method == "clear" && call->args.empty())
                {
                    // arr.clear() -> unregister handlers then clear DOM and array
                    std::string parent_var = "_loop_" + std::to_string(info.loop_id) + "_parent";
                    std::string count_var = "_loop_" + std::to_string(info.loop_id) + "_count";
                    if (info.is_only_child)
                    {
                        // Bulk optimization: unregister handlers only, then clear parent's innerHTML
                        result = "for (auto& " + var + " : " + arr_name + ") { " + var + "._remove_view(true); }\n";
                        result += "webcc::dom::set_inner_html(" + parent_var + ", \"\");\n";
                    }
                    else
                    {
                        // Normal path: each _remove_view removes its own DOM element
                        result = "for (auto& " + var + " : " + arr_name + ") { " + var + "._remove_view(); }\n";
                    }
                    result += count_var + " = 0;\n";
                    result += arr_name + ".clear();\n";
                    return result;
                }
            }

            auto html_loop_it = g_array_loops.find(arr_name);
            if (html_loop_it != g_array_loops.end())
            {
                const auto &info = html_loop_it->second;
                std::string var = info.var_name;
                std::string result;

                if (method == "push" && call->args.size() == 1)
                {
                    std::string item_expr = call->args[0].value->to_webcc();
                    std::string parent_var = info.parent_var;
                    std::string count_var = "_loop_" + std::to_string(info.loop_id) + "_count";
                    result = "{\n";
                    result += arr_name + ".push_back(" + item_expr + ");\n";
                    result += "if (" + parent_var + ".is_valid()) {\n";
                    result += "    auto& " + var + " = " + arr_name + "[" + arr_name + ".size() - 1];\n";
                    result += info.item_creation_code;
                    if (!info.root_element_var.empty())
                    {
                        result += "    " + info.elements_vec_name + ".push_back(" + info.root_element_var + ");\n";
                    }
                    result += "    " + count_var + " = (int)" + arr_name + ".size();\n";
                    result += "}\n";
                    result += "}\n";
                    return result;
                }
                else if (method == "pop" && call->args.empty())
                {
                    std::string count_var = "_loop_" + std::to_string(info.loop_id) + "_count";
                    result = "if (!" + arr_name + ".empty()) {\n";
                    result += "    if (!" + info.elements_vec_name + ".empty()) {\n";
                    result += "        webcc::dom::remove_element(" + info.elements_vec_name + ".back());\n";
                    result += "        " + info.elements_vec_name + ".pop_back();\n";
                    result += "    }\n";
                    result += "    " + arr_name + ".pop_back();\n";
                    result += "    " + count_var + " = (int)" + arr_name + ".size();\n";
                    result += "}\n";
                    return result;
                }
                else if (method == "clear" && call->args.empty())
                {
                    std::string count_var = "_loop_" + std::to_string(info.loop_id) + "_count";
                    result = "for (auto& _el : " + info.elements_vec_name + ") { webcc::dom::remove_element(_el); }\n";
                    result += info.elements_vec_name + ".clear();\n";
                    result += arr_name + ".clear();\n";
                    result += count_var + " = 0;\n";
                    return result;
                }
            }
        }
    }
    return expression->to_webcc() + ";\n";
}

void ExpressionStatement::collect_dependencies(std::set<std::string> &deps)
{
    expression->collect_dependencies(deps);
}

std::string BlockStatement::to_webcc()
{
    std::string code = "{\n";
    for (auto &stmt : statements)
        code += stmt->to_webcc();
    code += "}\n";
    return code;
}

void BlockStatement::collect_dependencies(std::set<std::string> &deps)
{
    for (auto &stmt : statements)
        stmt->collect_dependencies(deps);
}

std::string IfStatement::to_webcc()
{
    std::string cond = strip_outer_parens(condition->to_webcc());
    std::string code = "if(" + cond + ") ";
    code += then_branch->to_webcc();
    if (else_branch)
    {
        code += " else ";
        code += else_branch->to_webcc();
    }
    return code;
}

void IfStatement::collect_dependencies(std::set<std::string> &deps)
{
    condition->collect_dependencies(deps);
    then_branch->collect_dependencies(deps);
    if (else_branch)
        else_branch->collect_dependencies(deps);
}

std::string ForRangeStatement::to_webcc()
{
    std::string code = "for(int " + var_name + " = " + start->to_webcc() + "; ";
    code += "(" + var_name + " < " + end->to_webcc() + "); ";
    code += var_name + "++) ";
    code += body->to_webcc();
    return code;
}

void ForRangeStatement::collect_dependencies(std::set<std::string> &deps)
{
    start->collect_dependencies(deps);
    end->collect_dependencies(deps);
    body->collect_dependencies(deps);
}

std::string ForEachStatement::to_webcc()
{
    std::string code = "for(auto& " + var_name + " : " + iterable->to_webcc() + ") ";
    code += body->to_webcc();
    return code;
}

void ForEachStatement::collect_dependencies(std::set<std::string> &deps)
{
    iterable->collect_dependencies(deps);
    body->collect_dependencies(deps);
}

void collect_mods_recursive(Statement *stmt, std::set<std::string> &mods)
{
    if (auto assign = dynamic_cast<Assignment *>(stmt))
    {
        mods.insert(assign->name);
    }
    else if (auto idxAssign = dynamic_cast<IndexAssignment *>(stmt))
    {
        if (auto id = dynamic_cast<Identifier *>(idxAssign->array.get()))
        {
            // Don't mark component arrays as modified for index assignment
            // Swapping components doesn't need DOM sync - they're already rendered
            if (g_component_array_loops.find(id->name) == g_component_array_loops.end())
            {
                mods.insert(id->name);
            }
        }
    }
    else if (auto memberAssign = dynamic_cast<MemberAssignment *>(stmt))
    {
        // Track the root object being modified
        Expression *obj = memberAssign->object.get();
        while (auto member = dynamic_cast<MemberAccess *>(obj))
        {
            obj = member->object.get();
        }
        if (auto id = dynamic_cast<Identifier *>(obj))
        {
            mods.insert(id->name);
        }
    }
    else if (auto exprStmt = dynamic_cast<ExpressionStatement *>(stmt))
    {
        if (auto postfix = dynamic_cast<PostfixOp *>(exprStmt->expression.get()))
        {
            if (auto id = dynamic_cast<Identifier *>(postfix->operand.get()))
            {
                mods.insert(id->name);
            }
        }
        else if (auto unary = dynamic_cast<UnaryOp *>(exprStmt->expression.get()))
        {
            if (unary->op == "++" || unary->op == "--")
            {
                if (auto id = dynamic_cast<Identifier *>(unary->operand.get()))
                {
                    mods.insert(id->name);
                }
            }
        }
        else if (auto call = dynamic_cast<FunctionCall *>(exprStmt->expression.get()))
        {
            size_t dot_pos = call->name.rfind('.');
            if (dot_pos != std::string::npos)
            {
                std::string method = call->name.substr(dot_pos + 1);
                std::string obj_expr = call->name.substr(0, dot_pos);
                
                // Check if this is a mutating method (returns void) on ANY type
                // This includes: string.append(), array.push(), etc.
                // We need to check all possible types since we don't have type info here
                bool is_mutating = false;
                std::vector<std::string> types_to_check = {"string", "array", "int", "float", "bool"};
                for (const auto& type : types_to_check)
                {
                    auto* method_def = DefSchema::instance().lookup_method(type, method);
                    if (method_def && method_def->return_type == "void")
                    {
                        is_mutating = true;
                        break;
                    }
                }
                
                if (is_mutating)
                {
                    // For simple identifiers like "label.append()", track "label"
                    // For member access like "rows[i].label.append()", track the root variable
                    // We need to parse obj_expr to find the root identifier
                    
                    // Find the root variable name (leftmost identifier before any . or [)
                    size_t first_dot = obj_expr.find('.');
                    size_t first_bracket = obj_expr.find('[');
                    size_t split_pos = std::string::npos;
                    
                    if (first_dot != std::string::npos && first_bracket != std::string::npos)
                    {
                        split_pos = std::min(first_dot, first_bracket);
                    }
                    else if (first_dot != std::string::npos)
                    {
                        split_pos = first_dot;
                    }
                    else if (first_bracket != std::string::npos)
                    {
                        split_pos = first_bracket;
                    }
                    
                    std::string root_var = (split_pos != std::string::npos) 
                        ? obj_expr.substr(0, split_pos) 
                        : obj_expr;
                    mods.insert(root_var);
                }
            }
        }
    }
    else if (auto block = dynamic_cast<BlockStatement *>(stmt))
    {
        for (auto &s : block->statements)
        {
            collect_mods_recursive(s.get(), mods);
        }
    }
    else if (auto ifStmt = dynamic_cast<IfStatement *>(stmt))
    {
        collect_mods_recursive(ifStmt->then_branch.get(), mods);
        if (ifStmt->else_branch)
        {
            collect_mods_recursive(ifStmt->else_branch.get(), mods);
        }
    }
    else if (auto forRange = dynamic_cast<ForRangeStatement *>(stmt))
    {
        collect_mods_recursive(forRange->body.get(), mods);
    }
    else if (auto forEach = dynamic_cast<ForEachStatement *>(stmt))
    {
        collect_mods_recursive(forEach->body.get(), mods);
        // If the loop item variable was modified (e.g., task.status = ...),
        // treat the iterable as modified too so parent-level reactive updates run.
        if (mods.count(forEach->var_name))
        {
            if (auto id = dynamic_cast<Identifier *>(forEach->iterable.get()))
            {
                mods.insert(id->name);
            }
        }
    }
}
