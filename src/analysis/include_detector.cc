#include "include_detector.h"
#include "ast/ast.h"
#include "../defs/def_parser.h"
#include <map>

// Build type-to-header mapping from DefSchema (handle types -> namespace)
static std::map<std::string, std::string> build_type_to_header()
{
    std::map<std::string, std::string> result;
    auto &schema = DefSchema::instance();

    for (const auto &[type_name, type_def] : schema.types())
    {
        // Get the namespace for this type (from @map annotations)
        std::string ns = schema.get_namespace_for_type(type_name);
        if (ns.empty())
            continue;

        // Map the type itself to its namespace
        result[type_name] = ns;

        // Also map return types and parameter types from methods
        for (const auto &method : type_def.methods)
        {
            // Map return type if it's a handle type
            if (!method.return_type.empty() && schema.lookup_type(method.return_type))
            {
                std::string return_ns = schema.get_namespace_for_type(method.return_type);
                if (!return_ns.empty())
                {
                    result[method.return_type] = return_ns;
                }
            }
            // Map parameter types if they're handle types
            for (const auto &param : method.params)
            {
                if (schema.lookup_type(param.type))
                {
                    std::string param_ns = schema.get_namespace_for_type(param.type);
                    if (!param_ns.empty())
                    {
                        result[param.type] = param_ns;
                    }
                }
            }
        }
    }
    return result;
}

// Extract base type from array types (e.g., "Audio[]" -> "Audio")
static std::string get_base_type(const std::string &type)
{
    size_t bracket = type.find('[');
    if (bracket != std::string::npos)
    {
        return type.substr(0, bracket);
    }
    return type;
}

// Collect types used in expressions (recursively scan AST)
static void collect_types_from_expr(Expression *expr, std::set<std::string> &types)
{
    if (!expr)
        return;

    // Check for static method calls like FetchRequest.post(), System.log(), etc.
    if (auto *call = dynamic_cast<FunctionCall *>(expr))
    {
        // The function name might be "FetchRequest.post" or similar
        size_t dot = call->name.find('.');
        if (dot != std::string::npos)
        {
            types.insert(call->name.substr(0, dot));
        }
        for (auto &arg : call->args)
        {
            collect_types_from_expr(arg.value.get(), types);
        }
    }
    else if (auto *member = dynamic_cast<MemberAccess *>(expr))
    {
        // Check if object is an identifier (type name for static calls)
        if (auto *id = dynamic_cast<Identifier *>(member->object.get()))
        {
            types.insert(id->name);
        }
        collect_types_from_expr(member->object.get(), types);
    }
    else if (auto *binary = dynamic_cast<BinaryOp *>(expr))
    {
        collect_types_from_expr(binary->left.get(), types);
        collect_types_from_expr(binary->right.get(), types);
    }
    else if (auto *unary = dynamic_cast<UnaryOp *>(expr))
    {
        collect_types_from_expr(unary->operand.get(), types);
    }
    else if (auto *ternary = dynamic_cast<TernaryOp *>(expr))
    {
        collect_types_from_expr(ternary->condition.get(), types);
        collect_types_from_expr(ternary->true_expr.get(), types);
        collect_types_from_expr(ternary->false_expr.get(), types);
    }
    else if (auto *postfix = dynamic_cast<PostfixOp *>(expr))
    {
        collect_types_from_expr(postfix->operand.get(), types);
    }
    else if (auto *index = dynamic_cast<IndexAccess *>(expr))
    {
        collect_types_from_expr(index->array.get(), types);
        collect_types_from_expr(index->index.get(), types);
    }
}

// Collect types used in statements (recursively scan AST)
static void collect_types_from_stmt(Statement *stmt, std::set<std::string> &types)
{
    if (!stmt)
        return;

    if (auto *expr_stmt = dynamic_cast<ExpressionStatement *>(stmt))
    {
        collect_types_from_expr(expr_stmt->expression.get(), types);
    }
    else if (auto *var_decl = dynamic_cast<VarDeclaration *>(stmt))
    {
        types.insert(get_base_type(var_decl->type));
        collect_types_from_expr(var_decl->initializer.get(), types);
    }
    else if (auto *assign = dynamic_cast<Assignment *>(stmt))
    {
        collect_types_from_expr(assign->value.get(), types);
    }
    else if (auto *idx_assign = dynamic_cast<IndexAssignment *>(stmt))
    {
        collect_types_from_expr(idx_assign->array.get(), types);
        collect_types_from_expr(idx_assign->index.get(), types);
        collect_types_from_expr(idx_assign->value.get(), types);
    }
    else if (auto *if_stmt = dynamic_cast<IfStatement *>(stmt))
    {
        collect_types_from_expr(if_stmt->condition.get(), types);
        collect_types_from_stmt(if_stmt->then_branch.get(), types);
        collect_types_from_stmt(if_stmt->else_branch.get(), types);
    }
    else if (auto *for_stmt = dynamic_cast<ForRangeStatement *>(stmt))
    {
        collect_types_from_expr(for_stmt->start.get(), types);
        collect_types_from_expr(for_stmt->end.get(), types);
        collect_types_from_stmt(for_stmt->body.get(), types);
    }
    else if (auto *for_each = dynamic_cast<ForEachStatement *>(stmt))
    {
        collect_types_from_expr(for_each->iterable.get(), types);
        collect_types_from_stmt(for_each->body.get(), types);
    }
    else if (auto *block = dynamic_cast<BlockStatement *>(stmt))
    {
        for (auto &s : block->statements)
            collect_types_from_stmt(s.get(), types);
    }
    else if (auto *ret = dynamic_cast<ReturnStatement *>(stmt))
    {
        collect_types_from_expr(ret->value.get(), types);
    }
}

// Collect all types used in a component (including method bodies)
static void collect_used_types(const Component &comp, std::set<std::string> &types)
{
    // Collect from state variables
    for (const auto &var : comp.state)
    {
        types.insert(get_base_type(var->type));
        collect_types_from_expr(var->initializer.get(), types);
    }
    // Collect from parameters
    for (const auto &param : comp.params)
    {
        types.insert(get_base_type(param->type));
    }
    // Collect from method parameters, return types, and bodies
    for (const auto &method : comp.methods)
    {
        types.insert(get_base_type(method.return_type));
        for (const auto &param : method.params)
        {
            types.insert(get_base_type(param.type));
        }
        // Scan method body for type usage
        for (const auto &stmt : method.body)
        {
            collect_types_from_stmt(stmt.get(), types);
        }
    }
}

// Determine which headers are needed based on used types
std::set<std::string> get_required_headers(const std::vector<Component> &components)
{
    static auto type_to_header = build_type_to_header();

    std::set<std::string> used_types;
    for (const auto &comp : components)
    {
        collect_used_types(comp, used_types);
    }

    std::set<std::string> headers;
    // Always include dom, system, and input (needed for basic DOM operations, main loop, and key state)
    headers.insert("dom");
    headers.insert("system");
    headers.insert("input");

    for (const auto &type : used_types)
    {
        auto it = type_to_header.find(type);
        if (it != type_to_header.end())
        {
            // Skip 'json' header - it's embedded inline when features.json is true
            if (it->second != "json")
            {
                headers.insert(it->second);
            }
        }
    }

    return headers;
}
