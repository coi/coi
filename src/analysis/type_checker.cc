#include "type_checker.h"
#include "../defs/def_parser.h"
#include "../cli/error.h"
#include <iostream>
#include <algorithm>
#include <set>
#include <functional>
#include <cctype>

// Global set of known enum type names (populated during validation)
static std::set<std::string> g_enum_types;

// Forward declarations
std::string normalize_type(const std::string &type);
bool is_compatible_type(const std::string &source, const std::string &target);
std::string infer_expression_type(Expression *expr, const std::map<std::string, std::string> &scope);

// Validate positional arguments against component parameters (used by router and could be used for constructor calls)
// Returns error message if validation fails, empty string on success
static std::string validate_component_args(
    const std::vector<CallArg> &args,
    const std::vector<std::unique_ptr<ComponentParam>> &params,
    const std::string &component_name,
    const std::string &context_desc,  // e.g., "Route '/dashboard'" or "Component 'App'"
    int line,
    const std::map<std::string, std::string> &scope = {})
{
    size_t arg_count = args.size();
    size_t param_count = params.size();

    if (arg_count != param_count)
    {
        return context_desc + " passes " + std::to_string(arg_count) + 
            " argument(s) to component '" + component_name + "' but it expects " + 
            std::to_string(param_count) + " parameter(s) at line " + std::to_string(line);
    }

    for (size_t i = 0; i < arg_count; ++i)
    {
        const auto &arg = args[i];
        const auto &param = params[i];

        // With CallArg, is_reference is explicit
        bool is_reference = arg.is_reference;

        // Get the argument name for helpful error messages
        std::string arg_name = "argument";
        if (auto *id = dynamic_cast<Identifier*>(arg.value.get()))
            arg_name = id->name;

        // Callback parameters (def name : returnType) require & prefix
        if (param->is_callback)
        {
            if (!is_reference)
            {
                return context_desc + ": callback parameter '" + param->name + 
                    "' requires '&' prefix. Use '&" + arg_name + "' instead of '" + arg_name + 
                    "' at line " + std::to_string(line);
            }
        }
        // Reference parameters (Type& name) require & prefix
        else if (param->is_reference)
        {
            if (!is_reference)
            {
                return context_desc + ": parameter '" + param->name + 
                    "' is a reference and requires '&' prefix. Use '&" + arg_name + 
                    "' at line " + std::to_string(line);
            }
        }
        // Non-reference, non-callback: validate types if scope provided
        else if (!scope.empty())
        {
            std::string arg_type = infer_expression_type(arg.value.get(), scope);
            std::string expected_type = normalize_type(param->type);
            if (arg_type != "unknown" && !is_compatible_type(arg_type, expected_type))
            {
                return context_desc + ": argument " + std::to_string(i + 1) + " ('" + arg_name + 
                    "') expects type '" + expected_type + "' but got '" + arg_type + 
                    "' at line " + std::to_string(line);
            }
        }
    }

    return "";  // Success
}

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

// Convert normalized type back to user-friendly display name for error messages
static std::string display_type_name(const std::string &normalized_type)
{
    // Check all types to find one that aliases to this normalized type
    for (const auto &[name, type_def] : DefSchema::instance().types())
    {
        if (!type_def.alias_of.empty() && type_def.alias_of == normalized_type)
        {
            return name;  // Return the alias name (e.g., "int" instead of "int32")
        }
    }
    return normalized_type;
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
    
    // Resolve type aliases from schema (e.g., int -> int32, float -> float64)
    std::string resolved = DefSchema::instance().resolve_alias(type);
    
    // Return the resolved type (canonical form)
    return resolved;
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

    // Allow dynamic array literal T[] to be assigned to fixed-size array T[N]
    // (e.g., int[5] x = [1, 2, 3, 4, 5] - the literal infers as int[] but target is int[5])
    // Size validation happens at code generation time
    if (source.ends_with("[]") && !tgt_elem.empty())
    {
        std::string src_elem_type = source.substr(0, source.length() - 2);
        return is_compatible_type(src_elem_type, tgt_elem);
    }

    // Allow upcast (derived -> base), e.g., Canvas -> DOMElement
    if (DefSchema::instance().inherits_from(source, target))
        return true;
    // Allow downcast from base to derived types (e.g., DOMElement -> Canvas)
    // This is needed for getElementById which returns DOMElement but you know it's a Canvas/etc
    // Uses the HANDLE_INHERITANCE table to check if target derives from source
    if (DefSchema::instance().inherits_from(target, source))
        return true;
    // Numeric conversions
    if (source == "int32" && (target == "float64" || target == "float32" || target == "uint8"))
        return true;
    if (source == "float64" && target == "float32")
        return true;  // Allow narrowing from float64 to float32
    if (source == "float32" && target == "float64")
        return true;  // Allow widening from float32 to float64
    // int32 can be used as handle (for raw handle values)
    if (source == "int32" && DefSchema::instance().is_handle(target))
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
        return "float64";  // float literals are 64-bit by default
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
        if (DefSchema::instance().is_handle(id->name))
            return id->name;
        return "unknown";
    }

    // Member access type inference (e.g., obj.field)
    if (auto member = dynamic_cast<MemberAccess *>(expr))
    {
        // First check if the object identifier exists in scope
        if (auto id = dynamic_cast<Identifier *>(member->object.get()))
        {
            if (scope.find(id->name) == scope.end())
            {
                ErrorHandler::type_error("Undefined variable '" + id->name + "' in member access", member->line);
                exit(1);
            }
        }
        
        std::string obj_type = infer_expression_type(member->object.get(), scope);
        if (obj_type == "unknown")
            return "unknown";

        // Check if it's a schema type with known fields/properties
        // For now, return unknown - could be extended to check schema for field types
        return "unknown";
    }

    // Reference expression type inference (&expr) - returns type of operand
    if (auto ref_expr = dynamic_cast<ReferenceExpression *>(expr))
    {
        return infer_expression_type(ref_expr->operand.get(), scope);
    }

    // Move expression type inference (:expr) - returns type of operand
    if (auto move_expr = dynamic_cast<MoveExpression *>(expr))
    {
        return infer_expression_type(move_expr->operand.get(), scope);
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
            if (operand_type == "int32" || operand_type == "float64" || operand_type == "float32")
            {
                return operand_type;
            }
            if (operand_type != "unknown")
            {
                ErrorHandler::type_error("Unary '" + unary->op + "' operator requires numeric type, got '" + operand_type + "'", unary->line);
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
            ErrorHandler::type_error("Ternary operator branches have incompatible types '" + true_type + "' and '" + false_type + "'", -1);
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
            
            // Only validate simple identifiers (not complex expressions like array access)
            // Complex expressions like balls[i] contain brackets, so skip those
            bool is_simple_identifier = (obj_name.find('[') == std::string::npos) && 
                                       (obj_name.find('(') == std::string::npos);
            
            if (is_simple_identifier && !obj_name.empty() && scope.find(obj_name) == scope.end())
            {
                // Check if it's a handle type or enum - those are validated by schema lookup below
                bool is_handle = DefSchema::instance().is_handle(obj_name);
                bool is_enum = is_enum_type(obj_name);
                
                // Check if obj_name is a valid type with a namespace mapping (e.g., DOMElement -> dom, System -> system)
                // Also walk the inheritance chain (e.g., Canvas -> DOMElement means check canvas:: then dom::)
                std::string snake_method = DefSchema::to_snake_case(method_name);
                bool is_valid_schema_call = false;
                
                std::string current_type = obj_name;
                while (!current_type.empty() && !is_valid_schema_call) {
                    std::string type_ns = DefSchema::instance().get_namespace_for_type(current_type);
                    if (!type_ns.empty()) {
                        const auto *entry = DefSchema::instance().lookup_func(snake_method);
                        if (entry && entry->ns == type_ns) {
                            // Found method in this namespace - but is it callable statically?
                            // Instance methods have a handle as first param and can't be called on the type name
                            bool is_instance_method = !entry->method->params.empty() && 
                                                      DefSchema::instance().is_handle(entry->method->params[0].type);
                            
                            if (is_instance_method) {
                                // Instance method called statically - error with helpful message
                                std::cerr << "\033[1;31mError:\033[0m '" << method_name 
                                          << "' is an instance method on '" << entry->method->params[0].type 
                                          << "' and cannot be called on '" << obj_name 
                                          << "'. Use instance." << method_name << "(...) instead at line " 
                                          << func->line << std::endl;
                                exit(1);
                            }
                            is_valid_schema_call = true;
                            break;
                        }
                    }
                    // Walk up inheritance chain (e.g., Canvas -> DOMElement)
                    std::string parent_type;
                    auto* type_def = DefSchema::instance().lookup_type(current_type);
                    if (type_def && !type_def->extends.empty()) {
                        parent_type = type_def->extends;
                    }
                    current_type = parent_type;
                }
                
                // If not in scope and not a handle/enum/schema-namespace, it's undefined
                if (!is_handle && !is_enum && !is_valid_schema_call)
                {
                    std::cerr << "\033[1;31mError:\033[0m Undefined variable '" << obj_name 
                              << "' in method call at line " << func->line << std::endl;
                    exit(1);
                }
            }
        }

        // Handle array/vector/string methods BEFORE schema lookup
        // These are built-in methods that shouldn't be confused with schema functions
        if (!obj_name.empty() && scope.count(obj_name))
        {
            std::string obj_type = scope.at(obj_name);
            
            // Check if it's any array type (dynamic [] or fixed-size [N])
            bool is_dynamic_array = obj_type.ends_with("[]");
            bool is_fixed_array = false;
            if (!is_dynamic_array) {
                size_t bracket_pos = obj_type.rfind('[');
                if (bracket_pos != std::string::npos && obj_type.back() == ']') {
                    std::string size_str = obj_type.substr(bracket_pos + 1, obj_type.length() - bracket_pos - 2);
                    is_fixed_array = !size_str.empty() && std::all_of(size_str.begin(), size_str.end(), ::isdigit);
                }
            }
            
            // Use DefSchema for array method lookups
            if (is_dynamic_array || is_fixed_array)
            {
                if (auto* method_def = DefSchema::instance().lookup_method("array", method_name)) {
                    if (method_def->params.size() == func->args.size()) {
                        return method_def->return_type.empty() ? "void" : normalize_type(method_def->return_type);
                    }
                }
            }
            
            // Use DefSchema for string method lookups
            if (obj_type == "string")
            {
                if (auto* method_def = DefSchema::instance().lookup_method("string", method_name)) {
                    if (method_def->params.size() == func->args.size() ||
                        // Handle overloaded methods like substr(start) and substr(start, len)
                        (method_name == "subStr" && (func->args.size() == 1 || func->args.size() == 2))) {
                        return method_def->return_type.empty() ? "void" : normalize_type(method_def->return_type);
                    }
                }
            }
        }

        std::string snake_method = DefSchema::to_snake_case(method_name);
        const auto *entry = DefSchema::instance().lookup_func(snake_method);

        if (entry)
        {
            size_t expected_args = entry->method->params.size();
            size_t actual_args = func->args.size();
            size_t param_offset = 0;

            bool implicit_obj = false;
            if (!obj_name.empty())
            {
                if (scope.count(obj_name))
                {
                    // Only treat as implicit object if function actually expects a handle as first arg
                    if (!entry->method->params.empty())
                    {
                        std::string first_param_type = entry->method->params[0].type;
                        if (DefSchema::instance().is_handle(first_param_type))
                        {
                            std::string obj_type = scope.at(obj_name);
                            if (is_compatible_type(obj_type, first_param_type))
                            {
                                implicit_obj = true;
                            }
                        }
                    }
                }
                else
                {
                    // obj_name is NOT in scope - it's a type name or namespace
                    // Check if this is a valid static call
                    
                    bool is_valid_call = false;
                    
                    // Check if obj_name is a known handle type
                    bool is_handle_type = DefSchema::instance().is_handle(obj_name);
                    
                    if (!entry->method->params.empty() && DefSchema::instance().is_handle(entry->method->params[0].type))
                    {
                        // Method expects a handle as first param (instance method)
                        // Only allow if obj_name matches the expected handle type
                        if (is_handle_type && is_compatible_type(obj_name, entry->method->params[0].type))
                        {
                            // Valid: DOMElement.createElement() where first param is DOMElement
                            is_valid_call = true;
                        }
                        else
                        {
                            // Invalid: trying to call instance method statically with wrong type
                            std::cerr << "\033[1;31mError:\033[0m '" << method_name << "' is an instance method on '" 
                                      << entry->method->params[0].type << "' and cannot be called on '" << obj_name 
                                      << "'. Use instance." << method_name << "(...) instead at line " << func->line << std::endl;
                            exit(1);
                        }
                    }
                    else
                    {
                        // True static method (no handle as first param)
                        // Two valid cases:
                        // 1. Called via namespace: namespace.method() where obj_name matches entry->ns
                        // 2. Called via handle type: HandleType.method() where return type matches handle type
                        //    This supports "shared def" pattern (static factory methods)
                        std::string expected_ns = obj_name;
                        std::transform(expected_ns.begin(), expected_ns.end(), expected_ns.begin(), ::tolower);
                        
                        if (entry->ns == expected_ns)
                        {
                            // Case 1: namespace.method()
                            is_valid_call = true;
                        }
                        else if (is_handle_type && !entry->method->return_type.empty() && 
                                 is_compatible_type(entry->method->return_type, obj_name))
                        {
                            // Case 2: HandleType.method() where method returns that handle type
                            // This is a "shared def" / static factory method pattern
                            is_valid_call = true;
                        }
                        else
                        {
                            std::cerr << "\033[1;31mError:\033[0m Method '" << method_name << "' does not belong to '" 
                                      << obj_name << "'. It belongs to the '" << entry->ns << "' namespace at line " << func->line << std::endl;
                            exit(1);
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
                std::cerr << "\033[1;31mError:\033[0m Function '" << full_name << "' expects " << (expected_args - param_offset)
                          << " arguments but got " << actual_args << " at line " << func->line << std::endl;
                exit(1);
            }

            for (size_t i = 0; i < actual_args; ++i)
            {
                std::string arg_type = infer_expression_type(func->args[i].value.get(), scope);
                std::string expected_type = entry->method->params[i + param_offset].type;

                // Note: Schema methods (external APIs) don't support reference parameters,
                // so we don't validate &arg/:arg here. That validation happens for component methods.

                if (!is_compatible_type(arg_type, expected_type))
                {
                    std::cerr << "\033[1;31mError:\033[0m Argument " << (i + 1) << " of '" << full_name << "' expects '" << expected_type
                              << "' but got '" << arg_type << "' at line " << func->line << std::endl;
                    exit(1);
                }
            }

            return entry->method->return_type.empty() ? "void" : entry->method->return_type;
        }
        else
        {
            if (!obj_name.empty() && scope.count(obj_name))
            {
                std::string type = scope.at(obj_name);
                if (DefSchema::instance().is_handle(type))
                {
                    std::cerr << "\033[1;31mError:\033[0m Method '" << method_name << "' not found for type '" << type << "' at line " << func->line << std::endl;
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
        if (l == "int32" && r == "float64")
            return "float64";
        if (l == "float64" && r == "int32")
            return "float64";
        if (l == "int32" && r == "float32")
            return "float32";
        if (l == "float32" && r == "int32")
            return "float32";
        return "unknown";
    }

    return "unknown";
}

void validate_types(const std::vector<Component> &components, 
                    const std::vector<std::unique_ptr<EnumDef>> &global_enums,
                    const std::vector<std::unique_ptr<DataDef>> &global_data)
{
    std::set<std::string> component_names;
    std::map<std::string, const Component*> component_map;
    for (const auto &c : components) {
        component_names.insert(c.name);
        component_map[c.name] = &c;
    }

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

    // Validate global data type fields - they cannot contain no-copy types
    for (const auto &data_def : global_data)
    {
        for (const auto &field : data_def->fields)
        {
            std::string field_type = normalize_type(field.type);
            
            // Extract base type from arrays (e.g., "Canvas[]" -> "Canvas")
            std::string base_type = field_type;
            if (field_type.ends_with("[]"))
            {
                base_type = field_type.substr(0, field_type.length() - 2);
            }
            else
            {
                // Check for fixed-size array T[N]
                size_t bracket_pos = field_type.rfind('[');
                if (bracket_pos != std::string::npos && field_type.back() == ']')
                {
                    base_type = field_type.substr(0, bracket_pos);
                }
            }
            
            // Check if the field type is a no-copy type
            if (DefSchema::instance().is_nocopy(base_type))
            {
                std::cerr << "\033[1;31mError:\033[0m Data type '" << data_def->name 
                          << "' cannot contain no-copy field '" << field.name << "' of type '" << field.type << "'. "
                          << "Data types are value types (copyable) and cannot contain no-copy types like Canvas, Audio, WebSocket, etc." << std::endl;
                exit(1);
            }
        }
    }

    for (const auto &comp : components)
    {
        std::map<std::string, std::string> scope;

        // Validate data type fields - they cannot contain no-copy types
        for (const auto &data_def : comp.data)
        {
            for (const auto &field : data_def->fields)
            {
                std::string field_type = normalize_type(field.type);
                
                // Extract base type from arrays (e.g., "Canvas[]" -> "Canvas")
                std::string base_type = field_type;
                if (field_type.ends_with("[]"))
                {
                    base_type = field_type.substr(0, field_type.length() - 2);
                }
                else
                {
                    // Check for fixed-size array T[N]
                    size_t bracket_pos = field_type.rfind('[');
                    if (bracket_pos != std::string::npos && field_type.back() == ']')
                    {
                        base_type = field_type.substr(0, bracket_pos);
                    }
                }
                
                // Check if the field type is a no-copy type
                if (DefSchema::instance().is_nocopy(base_type))
                {
                    std::cerr << "\033[1;31mError:\033[0m Data type '" << data_def->name 
                              << "' cannot contain no-copy field '" << field.name << "' of type '" << field.type << "'. "
                              << "Data types are value types (copyable) and cannot contain no-copy types like Canvas, Audio, WebSocket, etc." << std::endl;
                    exit(1);
                }
            }
        }

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
                // Check for move expression in initializer (:expr)
                if (dynamic_cast<MoveExpression*>(var->initializer.get()))
                {
                    var->is_move = true;
                }
                
                // Error: cannot create a reference to a moved value (Type& name := :expr)
                if (var->is_reference && var->is_move)
                {
                    std::cerr << "\033[1;31mError:\033[0m Cannot create reference to moved value. "
                              << "Use either 'Type& " << var->name << " = expr' (reference) or "
                              << "'Type " << var->name << " := :expr' (move), not both. "
                              << "At line " << var->line << std::endl;
                    exit(1);
                }
                
                // Error: cannot copy a nocopy type (must use := or &)
                // Only applies when copying from another variable, not from function returns
                if (!var->is_move && !var->is_reference && DefSchema::instance().is_nocopy(type)
                    && dynamic_cast<Identifier*>(var->initializer.get()))
                {
                    std::cerr << "\033[1;31mError:\033[0m Cannot copy '" << type << "' - it is a nocopy type. "
                              << "Use '" << var->name << " := :source' (move) or '" << var->name << " = &source' (reference) instead. "
                              << "At line " << var->line << std::endl;
                    exit(1);
                }
                
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
            std::set<std::string> mutable_vars;  // Track which variables are mutable
            
            // Initialize with component's mutable state variables
            for (const auto &var : comp.state)
            {
                if (var->is_mutable) {
                    mutable_vars.insert(var->name);
                }
            }
            // Initialize with component's mutable parameters
            for (const auto &param : comp.params)
            {
                if (param->is_mutable) {
                    mutable_vars.insert(param->name);
                }
            }
            
            for (const auto &param : method.params)
            {
                method_scope[param.name] = normalize_type(param.type);
                if (param.is_mutable) {
                    mutable_vars.insert(param.name);
                }
            }

            // Get expected return type for this method
            std::string expected_return = method.return_type.empty() ? "void" : normalize_type(method.return_type);

            // Track variables that have been moved from (can no longer be used)
            std::set<std::string> moved_vars;
            
            // Helper to extract variable name from expression (for move tracking)
            auto get_var_name = [](Expression* expr) -> std::string {
                if (auto id = dynamic_cast<Identifier*>(expr)) {
                    return id->name;
                }
                return "";
            };
            
            // Helper to check if an expression uses a moved variable, and track moves from :expr
            std::function<void(Expression*, int)> check_moved_use;
            check_moved_use = [&](Expression* expr, int line) {
                if (!expr) return;
                
                if (auto id = dynamic_cast<Identifier*>(expr)) {
                    if (moved_vars.count(id->name)) {
                        std::cerr << "\033[1;31mError:\033[0m Use of moved variable '" << id->name 
                                  << "' at line " << line << ". Variable was moved and can no longer be used." << std::endl;
                        exit(1);
                    }
                }
                else if (auto move_expr = dynamic_cast<MoveExpression*>(expr)) {
                    // First check if the operand uses moved vars
                    check_moved_use(move_expr->operand.get(), line);
                    // Then mark the variable as moved
                    std::string var = get_var_name(move_expr->operand.get());
                    if (!var.empty()) {
                        moved_vars.insert(var);
                    }
                }
                else if (auto ref_expr = dynamic_cast<ReferenceExpression*>(expr)) {
                    check_moved_use(ref_expr->operand.get(), line);
                }
                else if (auto bin = dynamic_cast<BinaryOp*>(expr)) {
                    check_moved_use(bin->left.get(), line);
                    check_moved_use(bin->right.get(), line);
                }
                else if (auto call = dynamic_cast<FunctionCall*>(expr)) {
                    // Find if this is a component method call
                    const FunctionDef* target_method = nullptr;
                    for (const auto& m : comp.methods) {
                        if (m.name == call->name) {
                            target_method = &m;
                            break;
                        }
                    }
                    
                    // Validate arguments and check for moved variables
                    for (size_t i = 0; i < call->args.size(); ++i) {
                        auto& arg = call->args[i];
                        
                        // Check if argument uses moved variables
                        check_moved_use(arg.value.get(), line);
                        
                        // If arg.is_move is set (from :value syntax in CallArg), mark the variable as moved
                        if (arg.is_move) {
                            std::string var = get_var_name(arg.value.get());
                            if (!var.empty()) {
                                moved_vars.insert(var);
                            }
                        }
                        
                        // If we found the method, validate &/: usage
                        if (target_method && i < target_method->params.size()) {
                            bool param_is_ref = target_method->params[i].is_reference;
                            
                            // Check for &arg (reference expression) - either via CallArg.is_reference or ReferenceExpression
                            bool arg_is_ref = arg.is_reference || dynamic_cast<ReferenceExpression*>(arg.value.get());
                            bool arg_is_move = arg.is_move || dynamic_cast<MoveExpression*>(arg.value.get());
                            
                            if (arg_is_ref && !param_is_ref) {
                                std::cerr << "\033[1;31mError:\033[0m Argument " << (i + 1) 
                                          << " of '" << call->name << "' is passed by reference (&) "
                                          << "but parameter '" << target_method->params[i].name 
                                          << "' is not a reference type. Remove '&' or change parameter to '"
                                          << target_method->params[i].type << "&' at line " << line << std::endl;
                                exit(1);
                            }
                            // Check for :arg (move expression)
                            else if (arg_is_move && param_is_ref) {
                                std::cerr << "\033[1;31mError:\033[0m Argument " << (i + 1) 
                                          << " of '" << call->name << "' is passed by move (:) "
                                          << "but parameter '" << target_method->params[i].name 
                                          << "' is a reference. Use '&' for reference or remove ':' at line " 
                                          << line << std::endl;
                                exit(1);
                            }
                        }
                    }
                }
                else if (auto member = dynamic_cast<MemberAccess*>(expr)) {
                    check_moved_use(member->object.get(), line);
                }
                else if (auto idx = dynamic_cast<IndexAccess*>(expr)) {
                    check_moved_use(idx->array.get(), line);
                    check_moved_use(idx->index.get(), line);
                }
                else if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
                    check_moved_use(unary->operand.get(), line);
                }
                else if (auto ternary = dynamic_cast<TernaryOp*>(expr)) {
                    check_moved_use(ternary->condition.get(), line);
                    check_moved_use(ternary->true_expr.get(), line);
                    check_moved_use(ternary->false_expr.get(), line);
                }
                else if (auto postfix = dynamic_cast<PostfixOp*>(expr)) {
                    check_moved_use(postfix->operand.get(), line);
                }
                else if (auto arr = dynamic_cast<ArrayLiteral*>(expr)) {
                    for (auto& elem : arr->elements) check_moved_use(elem.get(), line);
                }
            };

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
                        // Check initializer for use of moved variables
                        check_moved_use(decl->initializer.get(), decl->line);
                        
                        // If this is a move (:=), mark the source variable as moved
                        if (decl->is_move)
                        {
                            std::string moved_var = get_var_name(decl->initializer.get());
                            if (!moved_var.empty()) {
                                moved_vars.insert(moved_var);
                            }
                        }
                        
                        // Error: cannot create a reference to a moved value (Type& name := expr)
                        if (decl->is_reference && decl->is_move)
                        {
                            std::cerr << "\033[1;31mError:\033[0m Cannot create reference to moved value. "
                                      << "Use either 'Type& " << decl->name << " = expr' (reference) or "
                                      << "'Type " << decl->name << " := expr' (move), not both. "
                                      << "At line " << decl->line << std::endl;
                            exit(1);
                        }
                        
                        // Error: cannot copy a nocopy type (must use := or &)
                        // Only applies when copying from another variable, not from function returns
                        if (!decl->is_move && !decl->is_reference && DefSchema::instance().is_nocopy(type)
                            && dynamic_cast<Identifier*>(decl->initializer.get()))
                        {
                            std::cerr << "\033[1;31mError:\033[0m Cannot copy '" << type << "' - it is a nocopy type. "
                                      << "Use '" << decl->name << " := :source' (move) or '" << decl->name << " = &source' (reference) instead. "
                                      << "At line " << decl->line << std::endl;
                            exit(1);
                        }
                        
                        std::string init = infer_expression_type(decl->initializer.get(), current_scope);
                        if (init != "unknown" && !is_compatible_type(init, type))
                        {
                    std::cerr << "\033[1;31mError:\033[0m Variable '" << decl->name << "' expects '" << type << "' but got '" << init << "' at line " << decl->line << std::endl;
                            exit(1);
                        }
                    }
                    current_scope[decl->name] = type;
                    // Track mutability for const-correctness checks
                    if (decl->is_mutable) {
                        mutable_vars.insert(decl->name);
                    }
                }
                else if (auto assign = dynamic_cast<Assignment *>(stmt.get()))
                {
                    // Check if the target variable itself was moved
                    if (moved_vars.count(assign->name)) {
                        std::cerr << "\033[1;31mError:\033[0m Assignment to moved variable '" << assign->name 
                                  << "' at line " << assign->line << ". Variable was moved and can no longer be used." << std::endl;
                        exit(1);
                    }
                    
                    std::string var_type = current_scope.count(assign->name) ? current_scope.at(assign->name) : "unknown";
                    
                    // Check value for use of moved variables
                    check_moved_use(assign->value.get(), assign->line);
                    
                    // If this is a move (:=), mark the source variable as moved
                    if (assign->is_move)
                    {
                        std::string moved_var = get_var_name(assign->value.get());
                        if (!moved_var.empty()) {
                            moved_vars.insert(moved_var);
                        }
                    }
                    
                    // Error: cannot copy a nocopy type (must use :=)
                    // Only applies when copying from another variable, not from function returns
                    if (!assign->is_move && DefSchema::instance().is_nocopy(var_type)
                        && dynamic_cast<Identifier*>(assign->value.get()))
                    {
                        std::cerr << "\033[1;31mError:\033[0m Cannot copy '" << var_type << "' - it is a nocopy type. "
                                  << "Use '" << assign->name << " := :source' (move) instead. "
                                  << "At line " << assign->line << std::endl;
                        exit(1);
                    }
                    
                    std::string val_type = infer_expression_type(assign->value.get(), current_scope);

                    // Store the target type for code generation (needed for handle casts)
                    assign->target_type = var_type;

                    if (var_type != "unknown" && val_type != "unknown")
                    {
                        if (!is_compatible_type(val_type, var_type))
                        {
                            std::cerr << "\033[1;31mError:\033[0m Assigning '" << val_type << "' to '" << assign->name << "' of type '" << var_type << "' at line " << assign->line << std::endl;
                            exit(1);
                        }
                    }
                }
                else if (auto if_stmt = dynamic_cast<IfStatement *>(stmt.get()))
                {
                    // Check condition for use of moved variables
                    check_moved_use(if_stmt->condition.get(), if_stmt->line);
                    
                    check_stmt(if_stmt->then_branch, current_scope);
                    if (if_stmt->else_branch)
                        check_stmt(if_stmt->else_branch, current_scope);
                }
                else if (auto for_range = dynamic_cast<ForRangeStatement *>(stmt.get()))
                {
                    // Check range expressions for use of moved variables
                    check_moved_use(for_range->start.get(), for_range->line);
                    check_moved_use(for_range->end.get(), for_range->line);
                    
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
                    // Check iterable for use of moved variables
                    check_moved_use(for_each->iterable.get(), for_each->line);
                    
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
                    // Check array, index, and value for use of moved variables
                    check_moved_use(idx_assign->array.get(), idx_assign->line);
                    check_moved_use(idx_assign->index.get(), idx_assign->line);
                    check_moved_use(idx_assign->value.get(), idx_assign->line);
                    
                    // If this is a move (:=), mark the source variable as moved
                    if (idx_assign->is_move)
                    {
                        std::string moved_var = get_var_name(idx_assign->value.get());
                        if (!moved_var.empty()) {
                            moved_vars.insert(moved_var);
                        }
                    }
                    
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
                    if (index_type != "int32" && index_type != "float64" && index_type != "float32" && index_type != "unknown")
                    {
                        std::cerr << "Type error: Array index must be numeric, got '" << index_type << "'" << std::endl;
                        exit(1);
                    }
                }
                else if (auto member_assign = dynamic_cast<MemberAssignment *>(stmt.get()))
                {
                    // Check object and value for use of moved variables
                    check_moved_use(member_assign->object.get(), member_assign->line);
                    check_moved_use(member_assign->value.get(), member_assign->line);
                    
                    // If this is a move (:=), mark the source variable as moved
                    if (member_assign->is_move)
                    {
                        std::string moved_var = get_var_name(member_assign->value.get());
                        if (!moved_var.empty()) {
                            moved_vars.insert(moved_var);
                        }
                    }
                    
                    // Type check member assignment: obj.member = value
                    // Check if we're trying to assign to a child component's member (not allowed)
                    // This includes both direct access (comp.member) and indexed access (arr[i].member)
                    
                    // Get the immediate object being accessed (before the final .member)
                    Expression* immediate_obj = member_assign->object.get();
                    
                    // Infer the type of the immediate object
                    std::string obj_type = infer_expression_type(immediate_obj, current_scope);
                    
                    // Check if the object is a component type
                    if (component_names.count(obj_type)) {
                        // Build a descriptive error message
                        std::string access_desc;
                        if (auto id = dynamic_cast<Identifier*>(immediate_obj)) {
                            access_desc = id->name;
                        } else if (auto idx = dynamic_cast<IndexAccess*>(immediate_obj)) {
                            if (auto arr_id = dynamic_cast<Identifier*>(idx->array.get())) {
                                access_desc = arr_id->name + "[...]";
                            } else {
                                access_desc = "array element";
                            }
                        } else if (auto ma = dynamic_cast<MemberAccess*>(immediate_obj)) {
                            access_desc = "nested member";
                        } else {
                            access_desc = "expression";
                        }
                        
                        std::cerr << "Error: Cannot assign to member '" << member_assign->member 
                                  << "' of component '" << obj_type << "' (via " << access_desc << "). "
                                  << "Component state can only be modified from within the component itself. "
                                  << "Use a public method like 'set" 
                                  << (char)std::toupper(member_assign->member[0]) 
                                  << member_assign->member.substr(1) << "()' instead." << std::endl;
                        exit(1);
                    }
                    
                    // Validate the value type
                    infer_expression_type(member_assign->value.get(), current_scope);
                }
                else if (auto expr_stmt = dynamic_cast<ExpressionStatement *>(stmt.get()))
                {
                    // Check expression for use of moved variables
                    check_moved_use(expr_stmt->expression.get(), expr_stmt->line);
                    
                    // Check for calling mutating methods on const component variables
                    if (auto call = dynamic_cast<FunctionCall *>(expr_stmt->expression.get()))
                    {
                        size_t dot_pos = call->name.rfind('.');
                        if (dot_pos != std::string::npos)
                        {
                            std::string obj_name = call->name.substr(0, dot_pos);
                            std::string method_name = call->name.substr(dot_pos + 1);
                            
                            // Check if obj_name is a local variable (in scope)
                            if (current_scope.count(obj_name))
                            {
                                std::string obj_type = current_scope.at(obj_name);
                                
                                // Check if it's a component type and the variable is not mutable
                                if (component_map.count(obj_type) && !mutable_vars.count(obj_name))
                                {
                                    // Check if the method is mutating (modifies state)
                                    const Component* target_comp = component_map.at(obj_type);
                                    for (const auto& m : target_comp->methods)
                                    {
                                        if (m.name == method_name)
                                        {
                                            // Check if this method modifies any state
                                            std::set<std::string> modified_vars;
                                            m.collect_modifications(modified_vars);
                                            
                                            if (!modified_vars.empty())
                                            {
                                                std::cerr << "\033[1;31mError:\033[0m Cannot call mutating method '" 
                                                          << method_name << "' on const component variable '" 
                                                          << obj_name << "'. Declare as 'mut " << obj_type << " " 
                                                          << obj_name << "' to allow mutation. At line " 
                                                          << expr_stmt->line << std::endl;
                                                exit(1);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // Validate expression type
                    infer_expression_type(expr_stmt->expression.get(), current_scope);
                }
                else if (auto ret_stmt = dynamic_cast<ReturnStatement *>(stmt.get()))
                {
                    // Validate return type matches method's declared return type
                    if (ret_stmt->value)
                    {
                        // Check return value for use of moved variables
                        check_moved_use(ret_stmt->value.get(), ret_stmt->line);
                        
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
                            passed_prop.is_callback = declared_param->is_callback;
                            passed_prop.callback_param_types = declared_param->callback_param_types;
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
                                // For callbacks that expect arguments:
                                // - Only allow identifier (handler reference): &onRemove={removeTodo}
                                // - Disallow any parentheses: &onRemove={removeTodo()} or &onRemove={removeTodo(arg)}
                                //   because args are provided at call site inside the component
                                // For no-argument callbacks:
                                // - Allow identifier or empty function call: &onclick={toggle} or &onclick={toggle()}
                                if (auto *func_call = dynamic_cast<FunctionCall *>(passed_prop.value.get()))
                                {
                                    if (!declared_param->callback_param_types.empty())
                                    {
                                        // Callback expects arguments but got parentheses - not allowed
                                        throw std::runtime_error(
                                            "Callback parameter '" + passed_prop.name + "' in component '" + comp_inst->component_name +
                                            "' expects " + std::to_string(declared_param->callback_param_types.size()) +
                                            " argument(s) provided by the component. Use '&" + passed_prop.name + 
                                            "={handler}' without parentheses at line " +
                                            std::to_string(comp_inst->line));
                                    }
                                    // No-argument callback with empty () is OK
                                }
                                // Identifier (no parentheses) is always allowed - args are provided at call site
                                // e.g., &onRemove={removeTodo} is valid, component calls onRemove(id) internally
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
                        // Check for case-insensitive match to provide helpful error
                        std::string suggestion;
                        for (const auto &declared_param : target_comp->params)
                        {
                            std::string lower_passed = passed_prop.name;
                            std::string lower_declared = declared_param->name;
                            std::transform(lower_passed.begin(), lower_passed.end(), lower_passed.begin(), ::tolower);
                            std::transform(lower_declared.begin(), lower_declared.end(), lower_declared.begin(), ::tolower);
                            if (lower_passed == lower_declared)
                            {
                                suggestion = "; did you mean '" + declared_param->name + "'?";
                                break;
                            }
                        }
                        throw std::runtime_error(
                            "Unknown parameter '" + passed_prop.name + "' for component '" + comp_inst->component_name +
                            "'" + suggestion + " at line " + std::to_string(comp_inst->line));
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
            // Validate attribute types
            for (const auto &attr : el->attributes)
            {
                // Check if this is an event handler (starts with "on")
                bool is_event_handler = attr.name.size() > 2 && attr.name[0] == 'o' && attr.name[1] == 'n';
                
                if (is_event_handler)
                {
                    // Validate event handler parameter types
                    // oninput/onchange pass a string, onkeydown passes an int (keycode)
                    if (attr.name == "oninput" || attr.name == "onchange" || attr.name == "onkeydown")
                    {
                        std::string handler_name;
                        if (auto *func = dynamic_cast<FunctionCall *>(attr.value.get()))
                            handler_name = func->name;
                        else if (auto *id = dynamic_cast<Identifier *>(attr.value.get()))
                            handler_name = id->name;
                        
                        if (!handler_name.empty() && scope.count(handler_name))
                        {
                            std::string sig = scope.at(handler_name);
                            // sig format: "method(param_types):return_type"
                            if (sig.starts_with("method(") && sig.find("):") != std::string::npos)
                            {
                                std::string params = sig.substr(7, sig.find("):") - 7);
                                std::string expected_type = (attr.name == "onkeydown") ? "int32" : "string";
                                
                                if (params.empty())
                                    throw std::runtime_error("Event '" + attr.name + "' handler '" + handler_name + 
                                        "' needs 1 " + expected_type + " parameter at line " + std::to_string(el->line));
                                if (params.find(',') != std::string::npos)
                                    throw std::runtime_error("Event '" + attr.name + "' handler '" + handler_name + 
                                        "' should have 1 parameter, not multiple at line " + std::to_string(el->line));
                                if (!is_compatible_type(expected_type, normalize_type(params)))
                                    throw std::runtime_error("Event '" + attr.name + "' handler '" + handler_name + 
                                        "' parameter must be " + expected_type + ", not '" + params + "' at line " + std::to_string(el->line));
                            }
                        }
                    }
                }
                else
                {
                    // Non-event attributes must be strings
                    std::string attr_type = normalize_type(infer_expression_type(attr.value.get(), scope));
                    if (attr_type != "string" && attr_type != "unknown")
                    {
                        throw std::runtime_error("HTML attribute '" + attr.name + "' requires string, got '" + 
                            display_type_name(attr_type) + "'. Use \"{" + attr.value->to_webcc() + "}\" at line " + std::to_string(el->line));
                    }
                }
            }
            
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

    // Validate router/route relationship
    std::function<bool(ASTNode *)> has_route_placeholder = [&](ASTNode *node) -> bool
    {
        if (!node)
            return false;
        if (dynamic_cast<RoutePlaceholder *>(node))
            return true;
        if (auto *el = dynamic_cast<HTMLElement *>(node))
        {
            for (const auto &child : el->children)
            {
                if (has_route_placeholder(child.get()))
                    return true;
            }
        }
        else if (auto *viewIf = dynamic_cast<ViewIfStatement *>(node))
        {
            for (const auto &child : viewIf->then_children)
                if (has_route_placeholder(child.get()))
                    return true;
            for (const auto &child : viewIf->else_children)
                if (has_route_placeholder(child.get()))
                    return true;
        }
        else if (auto *viewFor = dynamic_cast<ViewForRangeStatement *>(node))
        {
            for (const auto &child : viewFor->children)
                if (has_route_placeholder(child.get()))
                    return true;
        }
        else if (auto *viewForEach = dynamic_cast<ViewForEachStatement *>(node))
        {
            for (const auto &child : viewForEach->children)
                if (has_route_placeholder(child.get()))
                    return true;
        }
        return false;
    };

    for (const auto &comp : components)
    {
        bool has_router_block = comp.router != nullptr;
        bool has_route_in_view = false;
        
        for (const auto &root : comp.render_roots)
        {
            if (has_route_placeholder(root.get()))
            {
                has_route_in_view = true;
                break;
            }
        }

        if (has_router_block && !has_route_in_view)
        {
            throw std::runtime_error("Component '" + comp.name + "' has a router block but no <route /> placeholder in its view. Add <route /> where the routed component should be rendered at line " + std::to_string(comp.router->line));
        }

        if (has_route_in_view && !has_router_block)
        {
            throw std::runtime_error("Component '" + comp.name + "' has <route /> but no router block. Add a router block to define routes");
        }

        // Validate that route components exist and their arguments match parameters
        if (has_router_block)
        {
            for (const auto &route : comp.router->routes)
            {
                auto it = component_map.find(route.component_name);
                if (it == component_map.end())
                {
                    throw std::runtime_error("Route '" + route.path + "' references unknown component '" + route.component_name + "' at line " + std::to_string(route.line));
                }

                // Use shared validation for route arguments
                std::string error = validate_component_args(
                    route.args,
                    it->second->params,
                    route.component_name,
                    "Route '" + route.path + "'",
                    route.line
                );
                if (!error.empty())
                {
                    throw std::runtime_error(error);
                }
            }
        }
    }
}
