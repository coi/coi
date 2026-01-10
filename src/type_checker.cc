#include "type_checker.h"
#include "schema_loader.h"
#include <iostream>
#include <algorithm>
#include <set>
#include <functional>

// Global set of known enum type names (populated during validation)
static std::set<std::string> g_enum_types;

// Check if a type is a known enum type
static bool is_enum_type(const std::string &t) {
    // Check direct match
    if (g_enum_types.count(t))
        return true;
    // Check qualified name (Component.EnumName) - extract enum name
    size_t dot_pos = t.find('.');
    if (dot_pos != std::string::npos) {
        std::string enum_name = t.substr(dot_pos + 1);
        return g_enum_types.count(enum_name) > 0;
    }
    return false;
}

std::string normalize_type(const std::string &type)
{
    // Handle Component.EnumName type syntax - extract just the enum name for comparison
    // App.Mode and Mode should both normalize to the same thing when in the same scope
    if (type.find('.') != std::string::npos)
    {
        // Keep the full qualified name for type checking
        return type;
    }
    // Handle dynamic array types: T[]
    if (type.ends_with("[]"))
    {
        std::string elem_type = type.substr(0, type.length() - 2);
        return normalize_type(elem_type) + "[]";
    }
    // Handle fixed-size array types: T[N]
    size_t bracket_pos = type.rfind('[');
    if (bracket_pos != std::string::npos && type.back() == ']')
    {
        std::string size_str = type.substr(bracket_pos + 1, type.length() - bracket_pos - 2);
        bool is_number = !size_str.empty() && std::all_of(size_str.begin(), size_str.end(), ::isdigit);
        if (is_number)
        {
            std::string elem_type = type.substr(0, bracket_pos);
            return normalize_type(elem_type) + "[" + size_str + "]";
        }
    }
    if (type == "int")
        return "int32";
    if (type == "float")
        return "float32";
    if (type == "bool")
        return "bool";
    if (type == "string")
        return "string";
    return type;
}

bool is_compatible_type(const std::string &source, const std::string &target)
{
    if (source == target)
        return true;
    if (source == "unknown" || target == "unknown")
        return true;

    // Handle Component.EnumName type compatibility
    // App.Mode should be compatible with Mode (when Mode is from App's shared enum)
    auto extract_enum_name = [](const std::string &t) -> std::string {
        size_t dot_pos = t.find('.');
        if (dot_pos != std::string::npos) {
            return t.substr(dot_pos + 1);
        }
        return t;
    };
    
    // If either is a qualified enum type, compare the enum names
    if (source.find('.') != std::string::npos || target.find('.') != std::string::npos) {
        std::string src_enum = extract_enum_name(source);
        std::string tgt_enum = extract_enum_name(target);
        if (src_enum == tgt_enum)
            return true;
    }

    // Handle dynamic array type compatibility: T[]
    if (source.ends_with("[]") && target.ends_with("[]"))
    {
        std::string src_elem = source.substr(0, source.length() - 2);
        std::string tgt_elem = target.substr(0, target.length() - 2);
        return is_compatible_type(src_elem, tgt_elem);
    }
    // Allow unknown[] to match any array type (for empty array literals)
    if (source == "unknown[]" && target.ends_with("[]"))
        return true;

    // Handle fixed-size array type compatibility: T[N]
    // Extract element type and size from both source and target
    auto extract_fixed_array = [](const std::string &t) -> std::pair<std::string, std::string>
    {
        size_t bracket_pos = t.rfind('[');
        if (bracket_pos != std::string::npos && t.back() == ']' && !t.ends_with("[]"))
        {
            std::string size_str = t.substr(bracket_pos + 1, t.length() - bracket_pos - 2);
            std::string elem = t.substr(0, bracket_pos);
            return {elem, size_str};
        }
        return {"", ""};
    };

    auto [src_elem, src_size] = extract_fixed_array(source);
    auto [tgt_elem, tgt_size] = extract_fixed_array(target);

    if (!src_elem.empty() && !tgt_elem.empty())
    {
        // Both are fixed-size arrays - check element type and size match
        return src_size == tgt_size && is_compatible_type(src_elem, tgt_elem);
    }

    // Allow fixed-size array T[N] to be assigned to T[] declaration
    // (the actual type will be determined by VarDeclaration::to_webcc)
    if (!src_elem.empty() && target.ends_with("[]"))
    {
        std::string tgt_elem = target.substr(0, target.length() - 2);
        return is_compatible_type(src_elem, tgt_elem);
    }

    // Allow upcast (derived -> base), e.g., Canvas -> DOMElement
    if (SchemaLoader::instance().is_assignable_to(source, target))
        return true;
    // Allow downcast from base to derived types (e.g., DOMElement -> Canvas)
    // This is needed for getElementById which returns DOMElement but you know it's a Canvas/etc
    // Uses the HANDLE_INHERITANCE table to check if target derives from source
    if (SchemaLoader::instance().is_assignable_to(target, source))
        return true;
    // Numeric conversions
    if (source == "int32" && (target == "float32" || target == "uint8"))
        return true;
    // int32 can be used as handle (for raw handle values)
    if (source == "int32" && SchemaLoader::instance().is_handle(target))
        return true;
    
    // Enum <-> int implicit conversions (only for known enum types)
    // Allow int -> enum (cast int to enum)
    if (source == "int32" && is_enum_type(target))
        return true;
    // Allow enum -> int (cast enum to int)
    if (is_enum_type(source) && target == "int32")
        return true;
    
    return false;
}

std::string infer_expression_type(Expression *expr, const std::map<std::string, std::string> &scope)
{
    if (dynamic_cast<IntLiteral *>(expr))
        return "int32";
    if (dynamic_cast<FloatLiteral *>(expr))
        return "float32";
    if (dynamic_cast<StringLiteral *>(expr))
        return "string";
    if (dynamic_cast<BoolLiteral *>(expr))
        return "bool";
    
    // Enum access type inference
    if (auto enum_access = dynamic_cast<EnumAccess *>(expr))
    {
        // Return the enum type name
        return enum_access->enum_name;
    }

    // Array literal type inference (dynamic array)
    if (auto arr = dynamic_cast<ArrayLiteral *>(expr))
    {
        if (arr->elements.empty())
            return "unknown[]";
        // Infer type from first element
        std::string elem_type = infer_expression_type(arr->elements[0].get(), scope);
        return elem_type + "[]";
    }

    // Array repeat literal type inference: [value; count] -> fixed-size array
    if (auto arr = dynamic_cast<ArrayRepeatLiteral *>(expr))
    {
        std::string elem_type = infer_expression_type(arr->value.get(), scope);
        return elem_type + "[" + std::to_string(arr->count) + "]";
    }

    // Index access type inference
    if (auto idx = dynamic_cast<IndexAccess *>(expr))
    {
        std::string arr_type = infer_expression_type(idx->array.get(), scope);
        // If it's a dynamic array type (e.g., int[]), return the element type
        if (arr_type.ends_with("[]"))
        {
            return arr_type.substr(0, arr_type.length() - 2);
        }
        // If it's a fixed-size array type (e.g., int[100]), return the element type
        size_t bracket_pos = arr_type.rfind('[');
        if (bracket_pos != std::string::npos && arr_type.back() == ']')
        {
            return arr_type.substr(0, bracket_pos);
        }
        return "unknown";
    }

    if (auto id = dynamic_cast<Identifier *>(expr))
    {
        if (scope.count(id->name))
            return scope.at(id->name);
        if (SchemaLoader::instance().is_handle(id->name))
            return id->name;
        return "unknown";
    }

    // Member access type inference (e.g., obj.field)
    if (auto member = dynamic_cast<MemberAccess *>(expr))
    {
        std::string obj_type = infer_expression_type(member->object.get(), scope);
        if (obj_type == "unknown")
            return "unknown";

        // Check if it's a schema type with known fields/properties
        // For now, return unknown - could be extended to check schema for field types
        return "unknown";
    }

    // Unary operator type inference (e.g., -x, !x)
    if (auto unary = dynamic_cast<UnaryOp *>(expr))
    {
        std::string operand_type = infer_expression_type(unary->operand.get(), scope);
        if (unary->op == "!")
        {
            return "bool";
        }
        // Unary +/- only makes sense on numeric types
        if (unary->op == "-" || unary->op == "+")
        {
            if (operand_type == "int32" || operand_type == "float32")
            {
                return operand_type;
            }
            if (operand_type != "unknown")
            {
                std::cerr << "Error: Unary '" << unary->op << "' operator requires numeric type, got '"
                          << operand_type << "' at line " << unary->line << std::endl;
                exit(1);
            }
            return "unknown";
        }
        return "unknown";
    }

    // Postfix operator type inference (e.g., i++, i--)
    if (auto postfix = dynamic_cast<PostfixOp *>(expr))
    {
        return infer_expression_type(postfix->operand.get(), scope);
    }

    // Ternary operator type inference (cond ? true_expr : false_expr)
    if (auto ternary = dynamic_cast<TernaryOp *>(expr))
    {
        // The result type is the type of the true/false branches (they should match)
        std::string true_type = infer_expression_type(ternary->true_expr.get(), scope);
        std::string false_type = infer_expression_type(ternary->false_expr.get(), scope);
        
        // If one side is unknown, return the other
        if (true_type == "unknown") return false_type;
        if (false_type == "unknown") return true_type;
        
        // Both sides should have compatible types
        if (!is_compatible_type(true_type, false_type) && !is_compatible_type(false_type, true_type))
        {
            std::cerr << "Type error: Ternary operator branches have incompatible types '"
                      << true_type << "' and '" << false_type << "'" << std::endl;
            exit(1);
        }
        
        return true_type;
    }

    if (auto func = dynamic_cast<FunctionCall *>(expr))
    {
        std::string full_name = func->name;
        std::string obj_name;
        std::string method_name = full_name;

        size_t dot_pos = full_name.rfind('.');
        
        // Handle EnumName.size() - returns int32
        if (dot_pos != std::string::npos)
        {
            std::string potential_enum = full_name.substr(0, dot_pos);
            std::string method = full_name.substr(dot_pos + 1);
            if (method == "size" && is_enum_type(potential_enum))
            {
                return "int32";
            }
        }
        if (dot_pos != std::string::npos)
        {
            obj_name = full_name.substr(0, dot_pos);
            method_name = full_name.substr(dot_pos + 1);
        }

        std::string snake_method = SchemaLoader::to_snake_case(method_name);
        const auto *entry = SchemaLoader::instance().lookup(snake_method);

        if (entry)
        {
            size_t expected_args = entry->params.size();
            size_t actual_args = func->args.size();
            size_t param_offset = 0;

            bool implicit_obj = false;
            if (!obj_name.empty())
            {
                if (scope.count(obj_name))
                {
                    // Only treat as implicit object if function actually expects a handle as first arg
                    if (!entry->params.empty())
                    {
                        std::string first_param_type = entry->params[0].type;
                        if (SchemaLoader::instance().is_handle(first_param_type))
                        {
                            std::string obj_type = scope.at(obj_name);
                            if (is_compatible_type(obj_type, first_param_type))
                            {
                                implicit_obj = true;
                            }
                        }
                    }
                }
            }

            if (implicit_obj)
            {
                param_offset = 1;
            }

            if (actual_args != (expected_args - param_offset))
            {
                std::cerr << "Error: Function '" << full_name << "' expects " << (expected_args - param_offset)
                          << " arguments but got " << actual_args << " line " << func->line << std::endl;
                exit(1);
            }

            for (size_t i = 0; i < actual_args; ++i)
            {
                std::string arg_type = infer_expression_type(func->args[i].get(), scope);
                std::string expected_type = entry->params[i + param_offset].type;

                if (!is_compatible_type(arg_type, expected_type))
                {
                    std::cerr << "Error: Argument " << (i + 1) << " of '" << full_name << "' expects '" << expected_type
                              << "' but got '" << arg_type << "' line " << func->line << std::endl;
                    exit(1);
                }
            }

            return entry->return_type.empty() ? "void" : entry->return_type;
        }
        else
        {
            if (!obj_name.empty() && scope.count(obj_name))
            {
                std::string type = scope.at(obj_name);
                if (SchemaLoader::instance().is_handle(type))
                {
                    std::cerr << "Error: Method '" << method_name << "' not found for type '" << type << "' line " << func->line << std::endl;
                    exit(1);
                }
            }
        }
        return "unknown";
    }

    if (auto bin = dynamic_cast<BinaryOp *>(expr))
    {
        std::string op = bin->op;
        std::string l = infer_expression_type(bin->left.get(), scope);
        std::string r = infer_expression_type(bin->right.get(), scope);

        // Comparison operators return bool
        if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=")
        {
            return "bool";
        }
        // Logical operators return bool
        if (op == "&&" || op == "||")
        {
            return "bool";
        }
        // Arithmetic operators
        if (l == r)
            return l;
        if (l == "int32" && r == "float32")
            return "float32";
        if (l == "float32" && r == "int32")
            return "float32";
        return "unknown";
    }

    return "unknown";
}

void validate_types(const std::vector<Component> &components, const std::vector<std::unique_ptr<EnumDef>> &global_enums)
{
    std::set<std::string> component_names;
    for (const auto &c : components)
        component_names.insert(c.name);

    // Collect all enum type names (for enum <-> int conversion checking)
    g_enum_types.clear();
    
    // Add global enums
    for (const auto &e : global_enums)
    {
        g_enum_types.insert(e->name);
    }
    
    // Add component enums
    for (const auto &comp : components)
    {
        for (const auto &e : comp.enums)
        {
            g_enum_types.insert(e->name);
            // Also add qualified name for shared enums
            if (e->is_shared)
            {
                g_enum_types.insert(comp.name + "." + e->name);
            }
        }
    }

    for (const auto &comp : components)
    {
        std::map<std::string, std::string> scope;

        // Check component parameter types and their default values
        for (const auto &param : comp.params)
        {
            std::string type = normalize_type(param->type);

            // Disallow pub on reference parameters - references point to parent's data
            // and should never be exposed to third parties
            if (param->is_public && param->is_reference)
            {
                std::cerr << "Error: Reference parameter '" << param->name << "' cannot be public. "
                          << "References point to the parent's data and exposing them would break encapsulation." << std::endl;
                exit(1);
            }

            if (param->default_value)
            {
                std::string init = infer_expression_type(param->default_value.get(), scope);
                if (init != "unknown" && !is_compatible_type(init, type))
                {
                    std::cerr << "Error: Parameter '" << param->name << "' expects '" << type << "' but initialized with '" << init << "'" << std::endl;
                    exit(1);
                }
            }
            scope[param->name] = type;
        }

        for (const auto &var : comp.state)
        {
            std::string type = normalize_type(var->type);

            // Disallow pub on reference state variables for the same reason
            if (var->is_public && var->is_reference)
            {
                std::cerr << "Error: Reference variable '" << var->name << "' cannot be public. "
                          << "References point to other data and exposing them would break encapsulation." << std::endl;
                exit(1);
            }

            // Disallow uninitialized references (they must be bound immediately)
            if (var->is_reference && !var->initializer)
            {
                std::cerr << "Error: Reference variable '" << var->name << "' must be initialized. References cannot be left unbound." << std::endl;
                exit(1);
            }

            // Disallow storing references to child component properties (upward references)
            if (var->is_reference && var->initializer)
            {
                if (auto member = dynamic_cast<MemberAccess *>(var->initializer.get()))
                {
                    if (auto id = dynamic_cast<Identifier *>(member->object.get()))
                    {
                        auto it = scope.find(id->name);
                        if (it != scope.end())
                        {
                            std::string owner_type = it->second;
                            if (component_names.count(owner_type))
                            {
                                std::cerr << "Error: Storing reference to child component property is not allowed (upward reference): "
                                          << var->name << " = " << id->name << "." << member->member << std::endl;
                                exit(1);
                            }
                        }
                    }
                }
            }

            if (var->initializer)
            {
                std::string init = infer_expression_type(var->initializer.get(), scope);
                if (init != "unknown" && !is_compatible_type(init, type))
                {
                    std::cerr << "Error: Variable '" << var->name << "' expects '" << type << "' but initialized with '" << init << "'" << std::endl;
                    exit(1);
                }
            }
            scope[var->name] = type;
        }

        for (const auto &method : comp.methods)
        {
            std::map<std::string, std::string> method_scope = scope;
            for (const auto &param : method.params)
            {
                method_scope[param.name] = normalize_type(param.type);
            }

            // Get expected return type for this method
            std::string expected_return = method.return_type.empty() ? "void" : normalize_type(method.return_type);

            std::function<void(const std::unique_ptr<Statement> &, std::map<std::string, std::string> &)> check_stmt;
            check_stmt = [&](const std::unique_ptr<Statement> &stmt, std::map<std::string, std::string> &current_scope)
            {
                if (auto block = dynamic_cast<BlockStatement *>(stmt.get()))
                {
                    for (const auto &s : block->statements)
                        check_stmt(s, current_scope);
                }
                else if (auto decl = dynamic_cast<VarDeclaration *>(stmt.get()))
                {
                    std::string type = normalize_type(decl->type);
                    if (decl->initializer)
                    {
                        std::string init = infer_expression_type(decl->initializer.get(), current_scope);
                        if (init != "unknown" && !is_compatible_type(init, type))
                        {
                            std::cerr << "Error: Variable '" << decl->name << "' expects '" << type << "' but got '" << init << "' line " << decl->line << std::endl;
                            exit(1);
                        }
                    }
                    current_scope[decl->name] = type;
                }
                else if (auto assign = dynamic_cast<Assignment *>(stmt.get()))
                {
                    std::string var_type = current_scope.count(assign->name) ? current_scope.at(assign->name) : "unknown";
                    std::string val_type = infer_expression_type(assign->value.get(), current_scope);

                    // Store the target type for code generation (needed for handle casts)
                    assign->target_type = var_type;

                    if (var_type != "unknown" && val_type != "unknown")
                    {
                        if (!is_compatible_type(val_type, var_type))
                        {
                            std::cerr << "Error: Assigning '" << val_type << "' to '" << assign->name << "' of type '" << var_type << "' line " << assign->line << std::endl;
                            exit(1);
                        }
                    }
                }
                else if (auto expr_stmt = dynamic_cast<ExpressionStatement *>(stmt.get()))
                {
                    infer_expression_type(expr_stmt->expression.get(), current_scope);
                }
                else if (auto if_stmt = dynamic_cast<IfStatement *>(stmt.get()))
                {
                    check_stmt(if_stmt->then_branch, current_scope);
                    if (if_stmt->else_branch)
                        check_stmt(if_stmt->else_branch, current_scope);
                }
                else if (auto for_range = dynamic_cast<ForRangeStatement *>(stmt.get()))
                {
                    // Validate range expressions
                    infer_expression_type(for_range->start.get(), current_scope);
                    infer_expression_type(for_range->end.get(), current_scope);
                    // Create new scope with loop variable
                    std::map<std::string, std::string> loop_scope = current_scope;
                    loop_scope[for_range->var_name] = "int32";
                    check_stmt(for_range->body, loop_scope);
                }
                else if (auto for_each = dynamic_cast<ForEachStatement *>(stmt.get()))
                {
                    // Validate iterable and infer element type
                    std::string iterable_type = infer_expression_type(for_each->iterable.get(), current_scope);
                    std::map<std::string, std::string> loop_scope = current_scope;
                    if (iterable_type.ends_with("[]"))
                    {
                        loop_scope[for_each->var_name] = iterable_type.substr(0, iterable_type.length() - 2);
                    }
                    else
                    {
                        loop_scope[for_each->var_name] = "unknown";
                    }
                    check_stmt(for_each->body, loop_scope);
                }
                else if (auto idx_assign = dynamic_cast<IndexAssignment *>(stmt.get()))
                {
                    // Type check index assignment: arr[i] = value
                    std::string array_type = infer_expression_type(idx_assign->array.get(), current_scope);
                    std::string element_type = "unknown";
                    
                    // Extract element type from array type
                    if (array_type.ends_with("[]"))
                    {
                        element_type = array_type.substr(0, array_type.length() - 2);
                    }
                    else if (array_type.back() == ']')
                    {
                        // Fixed-size array like int32[10]
                        size_t bracket_pos = array_type.find('[');
                        if (bracket_pos != std::string::npos)
                        {
                            element_type = array_type.substr(0, bracket_pos);
                        }
                    }
                    
                    std::string value_type = infer_expression_type(idx_assign->value.get(), current_scope);
                    
                    if (element_type != "unknown" && !is_compatible_type(element_type, value_type))
                    {
                        std::cerr << "Type error: Cannot assign '" << value_type 
                                  << "' to array element of type '" << element_type << "'" << std::endl;
                        exit(1);
                    }
                    
                    // Also validate index is numeric
                    std::string index_type = infer_expression_type(idx_assign->index.get(), current_scope);
                    if (index_type != "int32" && index_type != "float32" && index_type != "unknown")
                    {
                        std::cerr << "Type error: Array index must be numeric, got '" << index_type << "'" << std::endl;
                        exit(1);
                    }
                }
                else if (auto ret_stmt = dynamic_cast<ReturnStatement *>(stmt.get()))
                {
                    // Validate return type matches method's declared return type
                    if (ret_stmt->value)
                    {
                        // Has a return value
                        if (expected_return == "void")
                        {
                            std::cerr << "Type error: Cannot return a value from void function '" 
                                      << method.name << "'" << std::endl;
                            exit(1);
                        }
                        std::string actual_return = infer_expression_type(ret_stmt->value.get(), current_scope);
                        if (actual_return != "unknown" && !is_compatible_type(actual_return, expected_return))
                        {
                            std::cerr << "Type error: Function '" << method.name << "' expects return type '" 
                                      << expected_return << "' but got '" << actual_return << "'" << std::endl;
                            exit(1);
                        }
                    }
                    else
                    {
                        // No return value (bare 'return;')
                        if (expected_return != "void")
                        {
                            std::cerr << "Type error: Function '" << method.name 
                                      << "' must return a value of type '" << expected_return << "'" << std::endl;
                            exit(1);
                        }
                    }
                }
            };

            for (const auto &stmt : method.body)
            {
                check_stmt(stmt, method_scope);
            }
        }
    }
}

void validate_mutability(const std::vector<Component> &components)
{
    for (const auto &comp : components)
    {
        // Build set of mutable state variables
        std::set<std::string> mutable_vars;
        for (const auto &var : comp.state)
        {
            if (var->is_mutable)
            {
                mutable_vars.insert(var->name);
            }
        }
        for (const auto &param : comp.params)
        {
            if (param->is_mutable)
            {
                mutable_vars.insert(param->name);
            }
        }

        // Check all methods for modifications to non-mutable variables
        for (const auto &method : comp.methods)
        {
            std::set<std::string> modified_vars;
            method.collect_modifications(modified_vars);

            for (const auto &var_name : modified_vars)
            {
                // Check if this variable exists in state and is not mutable
                bool is_known_var = false;
                bool is_mutable = false;
                bool is_param = false;

                for (const auto &var : comp.state)
                {
                    if (var->name == var_name)
                    {
                        is_known_var = true;
                        is_mutable = var->is_mutable;
                        break;
                    }
                }

                if (!is_known_var)
                {
                    for (const auto &param : comp.params)
                    {
                        if (param->name == var_name)
                        {
                            is_known_var = true;
                            is_param = true;
                            is_mutable = param->is_mutable;
                            break;
                        }
                    }
                }

                if (is_known_var && !is_mutable)
                {
                    if (is_param)
                    {
                        throw std::runtime_error("Cannot modify parameter '" + var_name + "' in component '" + comp.name +
                                                 "': parameter is not mutable. Add 'mut' keyword to parameter declaration: mut " + var_name);
                    }
                    else
                    {
                        throw std::runtime_error("Cannot modify '" + var_name + "' in component '" + comp.name +
                                                 "': variable is not mutable. Add 'mut' keyword to make it mutable: mut " + var_name);
                    }
                }
            }
        }
    }
}

void validate_view_hierarchy(const std::vector<Component> &components)
{
    std::map<std::string, const Component *> component_map;
    for (const auto &comp : components)
    {
        component_map[comp.name] = &comp;
    }

    // Build scope for a component (params + state + methods)
    // Methods are stored as "method(param_types):return_type" for validation
    auto build_scope = [&](const Component *comp) -> std::map<std::string, std::string>
    {
        std::map<std::string, std::string> scope;
        for (const auto &param : comp->params)
        {
            scope[param->name] = normalize_type(param->type);
        }
        for (const auto &var : comp->state)
        {
            scope[var->name] = normalize_type(var->type);
        }
        // Methods are stored with their full signature for callback validation
        for (const auto &method : comp->methods)
        {
            std::string sig = "method(";
            for (size_t i = 0; i < method.params.size(); ++i)
            {
                if (i > 0)
                    sig += ",";
                sig += normalize_type(method.params[i].type);
            }
            sig += "):" + (method.return_type.empty() ? "void" : normalize_type(method.return_type));
            scope[method.name] = sig;
        }
        return scope;
    };

    std::function<void(ASTNode *, const std::string &, std::map<std::string, std::string> &)> validate_node =
        [&](ASTNode *node, const std::string &parent_comp_name, std::map<std::string, std::string> &scope)
    {
        if (!node)
            return;

        if (auto *comp_inst = dynamic_cast<ComponentInstantiation *>(node))
        {
            auto it = component_map.find(comp_inst->component_name);
            if (it != component_map.end())
            {
                if (it->second->render_roots.empty())
                {
                    throw std::runtime_error("Component '" + comp_inst->component_name + "' is used in a view but has no view definition (logic-only component) at line " + std::to_string(comp_inst->line));
                }

                // Validate reference params
                const Component *target_comp = it->second;
                std::set<std::string> passed_param_names;

                for (auto &passed_prop : comp_inst->props)
                {
                    passed_param_names.insert(passed_prop.name);
                    // Find the param declaration in the target component
                    bool param_found = false;
                    for (const auto &declared_param : target_comp->params)
                    {
                        if (declared_param->name == passed_prop.name)
                        {
                            param_found = true;
                            passed_prop.is_mutable_def = declared_param->is_mutable;
                            if (declared_param->is_reference && !passed_prop.is_reference)
                            {
                                throw std::runtime_error(
                                    "Parameter '" + passed_prop.name + "' in component '" + comp_inst->component_name +
                                    "' expects a reference. Use '&" + passed_prop.name + "={...}' syntax at line " +
                                    std::to_string(comp_inst->line));
                            }
                            if (!declared_param->is_reference && passed_prop.is_reference)
                            {
                                // Allow & syntax for function params (webcc::function)
                                if (declared_param->type.find("webcc::function") == 0)
                                {
                                    // OK - but also validate callback argument types
                                }
                                else
                                {
                                    throw std::runtime_error(
                                        "Parameter '" + passed_prop.name + "' in component '" + comp_inst->component_name +
                                        "' does not expect a reference. Remove '&' prefix at line " +
                                        std::to_string(comp_inst->line));
                                }
                            }

                            // Validate callback argument types
                            if (declared_param->is_callback && passed_prop.value)
                            {
                                // The passed value should be a function call
                                if (auto *func_call = dynamic_cast<FunctionCall *>(passed_prop.value.get()))
                                {
                                    // Check argument count
                                    if (func_call->args.size() != declared_param->callback_param_types.size())
                                    {
                                        throw std::runtime_error(
                                            "Callback parameter '" + passed_prop.name + "' in component '" + comp_inst->component_name +
                                            "' expects " + std::to_string(declared_param->callback_param_types.size()) +
                                            " argument(s) but got " + std::to_string(func_call->args.size()) +
                                            " at line " + std::to_string(comp_inst->line));
                                    }

                                    // Check each argument type
                                    for (size_t i = 0; i < func_call->args.size(); ++i)
                                    {
                                        std::string arg_type = infer_expression_type(func_call->args[i].get(), scope);
                                        std::string expected_type = normalize_type(declared_param->callback_param_types[i]);
                                        if (arg_type != "unknown" && !is_compatible_type(arg_type, expected_type))
                                        {
                                            throw std::runtime_error(
                                                "Callback parameter '" + passed_prop.name + "' argument " + std::to_string(i + 1) +
                                                " expects type '" + expected_type + "' but got '" + arg_type +
                                                "' at line " + std::to_string(comp_inst->line));
                                        }
                                    }
                                }
                                else if (declared_param->callback_param_types.empty())
                                {
                                    // No-argument callback, value can be an identifier (method reference)
                                    // or a function call with no args - this is OK
                                }
                                else
                                {
                                    // Callback expects arguments but wasn't given a function call
                                    throw std::runtime_error(
                                        "Callback parameter '" + passed_prop.name + "' in component '" + comp_inst->component_name +
                                        "' expects " + std::to_string(declared_param->callback_param_types.size()) +
                                        " argument(s). Use syntax like '&" + passed_prop.name + "={handler(arg)}' at line " +
                                        std::to_string(comp_inst->line));
                                }
                            }
                            // Validate regular (non-callback) prop types
                            else if (!declared_param->is_callback && passed_prop.value)
                            {
                                std::string passed_type = infer_expression_type(passed_prop.value.get(), scope);
                                std::string expected_type = normalize_type(declared_param->type);
                                if (passed_type != "unknown" && !is_compatible_type(passed_type, expected_type))
                                {
                                    throw std::runtime_error(
                                        "Parameter '" + passed_prop.name + "' in component '" + comp_inst->component_name +
                                        "' expects type '" + expected_type + "' but got '" + passed_type +
                                        "' at line " + std::to_string(comp_inst->line));
                                }
                            }
                            break;
                        }
                    }
                    if (!param_found)
                    {
                        // Warn or error about unknown param?
                        // For now we focus on missing required params.
                    }
                }

                // Check for missing required reference params
                for (const auto &declared_param : target_comp->params)
                {
                    if (declared_param->is_reference && passed_param_names.find(declared_param->name) == passed_param_names.end())
                    {
                        throw std::runtime_error("Missing required reference parameter '&" + declared_param->name + "' for component '" + comp_inst->component_name + "' at line " + std::to_string(comp_inst->line));
                    }
                    // Note: Callbacks without defaults are optional - they may not always be needed
                    // (e.g., Button can use href without onclick)
                }
            }
        }
        else if (auto *el = dynamic_cast<HTMLElement *>(node))
        {
            for (const auto &child : el->children)
            {
                validate_node(child.get(), parent_comp_name, scope);
            }
        }
        else if (auto *viewIf = dynamic_cast<ViewIfStatement *>(node))
        {
            for (const auto &child : viewIf->then_children)
            {
                validate_node(child.get(), parent_comp_name, scope);
            }
            for (const auto &child : viewIf->else_children)
            {
                validate_node(child.get(), parent_comp_name, scope);
            }
        }
        else if (auto *viewFor = dynamic_cast<ViewForRangeStatement *>(node))
        {
            // Add loop variable to scope
            std::map<std::string, std::string> loop_scope = scope;
            loop_scope[viewFor->var_name] = "int32"; // Range loops always use int32
            for (const auto &child : viewFor->children)
            {
                validate_node(child.get(), parent_comp_name, loop_scope);
            }
        }
        else if (auto *viewForEach = dynamic_cast<ViewForEachStatement *>(node))
        {
            // Add loop variable to scope with inferred type from iterable
            std::map<std::string, std::string> loop_scope = scope;
            std::string iterable_type = infer_expression_type(viewForEach->iterable.get(), scope);
            if (iterable_type.ends_with("[]"))
            {
                loop_scope[viewForEach->var_name] = iterable_type.substr(0, iterable_type.size() - 2);
            }
            else
            {
                loop_scope[viewForEach->var_name] = "unknown";
            }
            for (const auto &child : viewForEach->children)
            {
                validate_node(child.get(), parent_comp_name, loop_scope);
            }
        }
    };

    for (const auto &comp : components)
    {
        std::map<std::string, std::string> scope = build_scope(&comp);
        for (const auto &root : comp.render_roots)
        {
            validate_node(root.get(), comp.name, scope);
        }
    }
}
