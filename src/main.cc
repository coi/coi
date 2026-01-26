#include "lexer.h"
#include "parser.h"
#include "ast/ast.h"
#include "def_parser.h"
#include "type_checker.h"
#include "json_codegen.h"
#include "cli.h"
#include "error.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <functional>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <queue>
#include <algorithm>

namespace fs = std::filesystem;

// =========================================================
// INCLUDE DETECTION
// =========================================================

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
static std::set<std::string> get_required_headers(const std::vector<Component> &components)
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

// =========================================================
// RUNTIME FEATURE SYSTEM
// =========================================================

// Feature flags detected from code analysis
struct FeatureFlags
{
    // DOM event dispatchers
    bool click = false;   // onclick handlers
    bool input = false;   // oninput handlers
    bool change = false;  // onchange handlers
    bool keydown = false; // onkeydown handlers (element-level)
    // Runtime features
    bool keyboard = false;    // Global key state tracking (Input.isKeyDown)
    bool router = false;      // Browser history/popstate (any component)
    bool websocket = false;   // WebSocket connections
    bool fetch = false;       // HTTP fetch requests
    bool json = false;        // JSON parsing (Json.parse)
};

// Scan view nodes for event handler attributes
static void scan_view_for_events(ASTNode *node, FeatureFlags &flags)
{
    if (!node)
        return;

    if (auto *el = dynamic_cast<HTMLElement *>(node))
    {
        for (const auto &attr : el->attributes)
        {
            if (attr.name == "onclick")
                flags.click = true;
            else if (attr.name == "oninput")
                flags.input = true;
            else if (attr.name == "onchange")
                flags.change = true;
            else if (attr.name == "onkeydown")
                flags.keydown = true;
        }
        for (const auto &child : el->children)
        {
            scan_view_for_events(child.get(), flags);
        }
    }
    else if (auto *viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (const auto &child : viewIf->then_children)
            scan_view_for_events(child.get(), flags);
        for (const auto &child : viewIf->else_children)
            scan_view_for_events(child.get(), flags);
    }
    else if (auto *viewFor = dynamic_cast<ViewForRangeStatement *>(node))
    {
        for (const auto &child : viewFor->children)
            scan_view_for_events(child.get(), flags);
    }
    else if (auto *viewForEach = dynamic_cast<ViewForEachStatement *>(node))
    {
        for (const auto &child : viewForEach->children)
            scan_view_for_events(child.get(), flags);
    }
}

// Detect which features are actually used by analyzing components
static FeatureFlags detect_features(const std::vector<Component> &components,
                                    const std::set<std::string> &headers)
{
    FeatureFlags flags;
    flags.websocket = headers.count("websocket") > 0;
    flags.fetch = headers.count("fetch") > 0;

    // Scan all components for routers
    for (const auto &comp : components)
    {
        if (comp.router)
        {
            flags.router = true;
            break;
        }
    }

    // Scan views for event handlers
    for (const auto &comp : components)
    {
        for (const auto &root : comp.render_roots)
        {
            scan_view_for_events(root.get(), flags);
        }
    }

    // Detect keyboard usage (Input.isKeyDown), Json.parse, and after_paint usage
    // by scanning for specific patterns in method bodies
    std::function<void(Expression *)> scan_expr = [&](Expression *expr)
    {
        if (!expr)
            return;
        if (auto *call = dynamic_cast<FunctionCall *>(expr))
        {
            // Check for Input.isKeyDown pattern
            if (call->name.find("Input.isKeyDown") != std::string::npos)
            {
                flags.keyboard = true;
            }
            // Check for Json.parse pattern
            if (call->name == "Json.parse")
            {
                flags.json = true;
            }
            for (auto &arg : call->args)
                scan_expr(arg.value.get());
        }
        else if (auto *member = dynamic_cast<MemberAccess *>(expr))
        {
            // Check for isKeyDown method call
            if (member->member == "isKeyDown")
            {
                if (auto *id = dynamic_cast<Identifier *>(member->object.get()))
                {
                    if (id->name == "Input")
                        flags.keyboard = true;
                }
            }
            scan_expr(member->object.get());
        }
        else if (auto *binary = dynamic_cast<BinaryOp *>(expr))
        {
            scan_expr(binary->left.get());
            scan_expr(binary->right.get());
        }
        else if (auto *ternary = dynamic_cast<TernaryOp *>(expr))
        {
            scan_expr(ternary->condition.get());
            scan_expr(ternary->true_expr.get());
            scan_expr(ternary->false_expr.get());
        }
    };

    std::function<void(Statement *)> scan_stmt = [&](Statement *stmt)
    {
        if (!stmt)
            return;
        if (auto *expr_stmt = dynamic_cast<ExpressionStatement *>(stmt))
        {
            scan_expr(expr_stmt->expression.get());
        }
        else if (auto *var_decl = dynamic_cast<VarDeclaration *>(stmt))
        {
            scan_expr(var_decl->initializer.get());
        }
        else if (auto *assign = dynamic_cast<Assignment *>(stmt))
        {
            scan_expr(assign->value.get());
        }
        else if (auto *if_stmt = dynamic_cast<IfStatement *>(stmt))
        {
            scan_expr(if_stmt->condition.get());
            scan_stmt(if_stmt->then_branch.get());
            scan_stmt(if_stmt->else_branch.get());
        }
        else if (auto *block = dynamic_cast<BlockStatement *>(stmt))
        {
            for (auto &s : block->statements)
                scan_stmt(s.get());
        }
        else if (auto *ret = dynamic_cast<ReturnStatement *>(stmt))
        {
            scan_expr(ret->value.get());
        }
    };

    for (const auto &comp : components)
    {
        for (const auto &method : comp.methods)
        {
            for (const auto &stmt : method.body)
            {
                scan_stmt(stmt.get());
            }
        }
    }

    return flags;
}

// Emit global declarations for enabled features
static void emit_feature_globals(std::ostream &out, const FeatureFlags &f)
{
    // DOM event dispatchers
    if (f.click)
    {
        out << "Dispatcher<webcc::function<void()>, 128> g_dispatcher;\n";
    }
    if (f.input)
    {
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_input_dispatcher;\n";
    }
    if (f.change)
    {
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_change_dispatcher;\n";
    }
    if (f.keydown)
    {
        out << "Dispatcher<webcc::function<void(int)>> g_keydown_dispatcher;\n";
    }
    // Runtime features
    if (f.keyboard)
    {
        out << "bool g_key_state[256] = {};\n";
    }
    if (f.router)
    {
        out << "webcc::function<void(const webcc::string&)> g_popstate_callback;\n";
    }
    if (f.websocket)
    {
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_ws_message_dispatcher;\n";
        out << "Dispatcher<webcc::function<void()>> g_ws_open_dispatcher;\n";
        out << "Dispatcher<webcc::function<void()>> g_ws_close_dispatcher;\n";
        out << "Dispatcher<webcc::function<void()>> g_ws_error_dispatcher;\n";
    }
    if (f.fetch)
    {
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_fetch_success_dispatcher;\n";
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_fetch_error_dispatcher;\n";
    }
}

// Emit event handlers for enabled features
static void emit_feature_event_handlers(std::ostream &out, const FeatureFlags &f)
{
    // DOM events
    if (f.click)
    {
        out << "        } else if (e.opcode == webcc::dom::ClickEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::ClickEvent>()) g_dispatcher.dispatch(evt->handle);\n";
    }
    if (f.input)
    {
        out << "        } else if (e.opcode == webcc::dom::InputEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::InputEvent>()) g_input_dispatcher.dispatch(evt->handle, webcc::string(evt->value));\n";
    }
    if (f.change)
    {
        out << "        } else if (e.opcode == webcc::dom::ChangeEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::ChangeEvent>()) g_change_dispatcher.dispatch(evt->handle, webcc::string(evt->value));\n";
    }
    if (f.keydown)
    {
        out << "        } else if (e.opcode == webcc::dom::KeydownEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::KeydownEvent>()) g_keydown_dispatcher.dispatch(evt->handle, evt->keycode);\n";
    }
    // Runtime features
    if (f.keyboard)
    {
        out << "        } else if (e.opcode == webcc::input::KeyDownEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::input::KeyDownEvent>()) { if (evt->key_code >= 0 && evt->key_code < 256) g_key_state[evt->key_code] = true; }\n";
        out << "        } else if (e.opcode == webcc::input::KeyUpEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::input::KeyUpEvent>()) { if (evt->key_code >= 0 && evt->key_code < 256) g_key_state[evt->key_code] = false; }\n";
    }
    if (f.router)
    {
        out << "        } else if (e.opcode == webcc::system::PopstateEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::system::PopstateEvent>()) { if (g_popstate_callback) g_popstate_callback(webcc::string(evt->path)); }\n";
    }
    if (f.websocket)
    {
        out << "        } else if (e.opcode == webcc::websocket::MessageEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::websocket::MessageEvent>()) g_ws_message_dispatcher.dispatch(evt->handle, webcc::string(evt->data));\n";
        out << "        } else if (e.opcode == webcc::websocket::OpenEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::websocket::OpenEvent>()) g_ws_open_dispatcher.dispatch(evt->handle);\n";
        out << "        } else if (e.opcode == webcc::websocket::CloseEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::websocket::CloseEvent>()) {\n";
        out << "                g_ws_close_dispatcher.dispatch(evt->handle);\n";
        out << "                g_ws_message_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_open_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_close_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_error_dispatcher.remove(evt->handle);\n";
        out << "            }\n";
        out << "        } else if (e.opcode == webcc::websocket::ErrorEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::websocket::ErrorEvent>()) {\n";
        out << "                g_ws_error_dispatcher.dispatch(evt->handle);\n";
        out << "                g_ws_message_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_open_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_close_dispatcher.remove(evt->handle);\n";
        out << "                g_ws_error_dispatcher.remove(evt->handle);\n";
        out << "            }\n";
    }
    if (f.fetch)
    {
        out << "        } else if (e.opcode == webcc::fetch::SuccessEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::fetch::SuccessEvent>()) {\n";
        out << "                g_fetch_success_dispatcher.dispatch(evt->id, webcc::string(evt->data));\n";
        out << "                g_fetch_success_dispatcher.remove(evt->id);\n";
        out << "                g_fetch_error_dispatcher.remove(evt->id);\n";
        out << "            }\n";
        out << "        } else if (e.opcode == webcc::fetch::ErrorEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::fetch::ErrorEvent>()) {\n";
        out << "                g_fetch_error_dispatcher.dispatch(evt->id, webcc::string(evt->error));\n";
        out << "                g_fetch_success_dispatcher.remove(evt->id);\n";
        out << "                g_fetch_error_dispatcher.remove(evt->id);\n";
        out << "            }\n";
    }
}

// Check if the Dispatcher template is needed
static bool needs_dispatcher(const FeatureFlags &f)
{
    return f.click || f.input || f.change || f.keydown || f.websocket || f.fetch;
}

// Emit initialization code for enabled features
static void emit_feature_init(std::ostream &out, const FeatureFlags &f, const std::string &root_comp)
{
    if (f.keyboard)
    {
        out << "    webcc::input::init_keyboard();\n";
    }
    if (f.router)
    {
        out << "    g_popstate_callback = [](const webcc::string& path) {\n";
        out << "        if (app) app->_handle_popstate(path);\n";
        out << "    };\n";
        out << "    webcc::system::init_popstate();\n";
    }
}

// =========================================================
// DEF SCHEMA INITIALIZATION
// =========================================================

static void load_def_schema()
{
    // Initialize DefSchema from def files (for @intrinsic, @inline, @map)
    // Always use the def directory next to the executable
    fs::path exe_dir = get_executable_dir();
    std::string def_dir;

    if (!exe_dir.empty() && fs::exists(exe_dir / "def"))
    {
        def_dir = (exe_dir / "def").string();
    }
    else
    {
        std::string hint;
        if (exe_dir.empty())
        {
            hint = "Could not determine executable location.\n"
                   "  If you see this error, please open an issue at:\n"
                   "  https://github.com/io-eric/coi/issues\n"
                   "  Include your OS, how you installed coi, and how you ran the command.";
        }
        else
        {
            hint = "Expected location: " + (exe_dir / "def").string();
        }
        ErrorHandler::cli_error("Could not find 'def' directory next to executable", hint);
        exit(1);
    }

    // Load from binary cache (generated at build time by gen_schema)
    std::string cache_path = def_dir + "/.cache/def_cache.bin";
    auto &def_schema = DefSchema::instance();

    if (def_schema.is_cache_valid(cache_path, def_dir))
    {
        def_schema.load_cache(cache_path);
    }
    else
    {
        // Cache missing or outdated - parse def files
        def_schema.load(def_dir);
        // Save cache for next time (only in the compiler's def directory)
        fs::create_directories(def_dir + "/.cache");
        def_schema.save_cache(cache_path);
    }
}

// =========================================================
// MAIN COMPILER
// =========================================================

// Collect child component names from a node
void collect_component_deps(ASTNode *node, std::set<std::string> &deps)
{
    if (!node)
        return;
    if (auto *comp_inst = dynamic_cast<ComponentInstantiation *>(node))
    {
        deps.insert(comp_inst->component_name);
    }
    else if (auto *el = dynamic_cast<HTMLElement *>(node))
    {
        for (const auto &child : el->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (const auto &child : viewIf->then_children)
        {
            collect_component_deps(child.get(), deps);
        }
        for (const auto &child : viewIf->else_children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewFor = dynamic_cast<ViewForRangeStatement *>(node))
    {
        for (const auto &child : viewFor->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewForEach = dynamic_cast<ViewForEachStatement *>(node))
    {
        for (const auto &child : viewForEach->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
}

// Extract base type name from array types (e.g., "Ball[]" -> "Ball")
static std::string extract_base_type_name(const std::string &type)
{
    size_t bracket = type.find('[');
    if (bracket != std::string::npos)
    {
        return type.substr(0, bracket);
    }
    return type;
}

// Topologically sort components so dependencies come first
std::vector<Component *> topological_sort_components(std::vector<Component> &components)
{
    std::map<std::string, Component *> comp_map;
    std::map<std::string, std::set<std::string>> dependencies;
    std::map<std::string, int> in_degree;

    for (auto &comp : components)
    {
        comp_map[comp.name] = &comp;
        in_degree[comp.name] = 0;
    }

    // Build dependency graph
    for (auto &comp : components)
    {
        std::set<std::string> deps;
        // Collect dependencies from view
        for (const auto &root : comp.render_roots)
        {
            collect_component_deps(root.get(), deps);
        }
        // Collect dependencies from router routes
        if (comp.router)
        {
            for (const auto &route : comp.router->routes)
            {
                deps.insert(route.component_name);
            }
        }
        // Collect dependencies from parameter types (e.g., Vector pos)
        for (const auto &param : comp.params)
        {
            std::string base_type = extract_base_type_name(param->type);
            if (comp_map.count(base_type))
            {
                deps.insert(base_type);
            }
        }
        // Collect dependencies from state variable types
        for (const auto &var : comp.state)
        {
            std::string base_type = extract_base_type_name(var->type);
            if (comp_map.count(base_type))
            {
                deps.insert(base_type);
            }
        }
        dependencies[comp.name] = deps;
    }

    // Calculate in-degrees
    for (auto &[name, deps] : dependencies)
    {
        for (auto &dep : deps)
        {
            if (comp_map.count(dep))
            {
                in_degree[name]++;
            }
        }
    }

    // Kahn's algorithm
    std::queue<std::string> queue;
    for (auto &[name, degree] : in_degree)
    {
        if (degree == 0)
        {
            queue.push(name);
        }
    }

    std::vector<Component *> sorted;
    while (!queue.empty())
    {
        std::string curr = queue.front();
        queue.pop();
        sorted.push_back(comp_map[curr]);

        // For each component that depends on curr, decrease in-degree
        for (auto &[name, deps] : dependencies)
        {
            if (deps.count(curr))
            {
                in_degree[name]--;
                if (in_degree[name] == 0)
                {
                    queue.push(name);
                }
            }
        }
    }

    // Check for cycles
    if (sorted.size() != components.size())
    {
        ErrorHandler::compiler_error("Circular dependency detected among components", -1);
    }

    return sorted;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_help(argv[0]);
        return 1;
    }

    std::string first_arg = argv[1];

    // Handle special commands
    if (first_arg == "help" || first_arg == "--help" || first_arg == "-h")
    {
        print_help(argv[0]);
        return 0;
    }

    if (first_arg == "init")
    {
        std::string project_name;
        if (argc >= 3)
        {
            project_name = argv[2];
        }
        return init_project(project_name);
    }

    // Hidden command for build system to pre-generate cache
    if (first_arg == "--gen-def-cache")
    {
        load_def_schema();
        return 0;
    }

    // Return the absolute path to the bundled def/ directory next to the executable
    if (first_arg == "--def-path")
    {
        fs::path exe_dir = get_executable_dir();
        if (exe_dir.empty())
        {
            ErrorHandler::cli_error("could not determine executable directory");
            return 1;
        }
        fs::path def_dir = exe_dir / "def";
        std::cout << def_dir.string() << std::endl;
        return 0;
    }

    // Parse build flags (shared by build, dev, and direct compilation)
    bool keep_cc = false;
    bool cc_only = false;
    for (int i = 2; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--keep-cc")
            keep_cc = true;
        else if (arg == "--cc-only")
            cc_only = true;
    }

    if (first_arg == "build")
    {
        return build_project(keep_cc, cc_only);
    }

    if (first_arg == "dev")
    {
        return dev_project(keep_cc, cc_only);
    }

    // From here on, we're doing actual compilation - load DefSchema
    load_def_schema();

    std::string input_file;
    std::string output_dir;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--cc-only")
            cc_only = true;
        else if (arg == "--keep-cc")
            keep_cc = true;
        else if (arg == "--out" || arg == "-o")
        {
            if (i + 1 < argc)
            {
                output_dir = argv[++i];
            }
            else
            {
                ErrorHandler::cli_error("--out requires an argument");
                return 1;
            }
        }
        else if (input_file.empty())
            input_file = arg;
        else
        {
            std::cerr << "Unknown argument or multiple input files: " << arg << std::endl;
            return 1;
        }
    }

    if (input_file.empty())
    {
        std::cerr << "No input file specified." << std::endl;
        return 1;
    }

    std::vector<Component> all_components;
    std::vector<std::unique_ptr<DataDef>> all_global_data;
    std::vector<std::unique_ptr<EnumDef>> all_global_enums;
    AppConfig final_app_config;
    std::set<std::string> processed_files;
    std::queue<std::string> file_queue;

    try
    {
        file_queue.push(fs::canonical(input_file).string());
    }
    catch (const std::exception &e)
    {
        std::cerr << colors::RED << "Error:" << colors::RESET << " resolving input file path: " << e.what() << std::endl;
        return 1;
    }

    try
    {
        while (!file_queue.empty())
        {
            std::string current_file_path = file_queue.front();
            file_queue.pop();

            if (processed_files.count(current_file_path))
                continue;
            processed_files.insert(current_file_path);

            std::cerr << "Processing " << current_file_path << "..." << std::endl;

            std::ifstream file(current_file_path);
            if (!file)
            {
                std::cerr << colors::RED << "Error:" << colors::RESET << " Could not open file " << current_file_path << std::endl;
                return 1;
            }
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // Lexical analysis
            Lexer lexer(source);
            auto tokens = lexer.tokenize();

            // Parsing
            Parser parser(tokens);
            parser.parse_file();

            // Add components with duplicate name check
            for (auto &comp : parser.components)
            {
                bool duplicate = false;
                for (const auto &existing : all_components)
                {
                    if (existing.name == comp.name)
                    {
                        std::cerr << colors::RED << "Error:" << colors::RESET << " Component '" << comp.name << "' is defined multiple times (found in " << current_file_path << " at line " << comp.line << ")" << std::endl;
                        return 1;
                    }
                }
                all_components.push_back(std::move(comp));
            }

            // Collect global enums
            for (auto &enum_def : parser.global_enums)
            {
                all_global_enums.push_back(std::move(enum_def));
            }

            // Collect global data types
            for (auto &data_def : parser.global_data)
            {
                all_global_data.push_back(std::move(data_def));
            }

            if (!parser.app_config.root_component.empty())
            {
                final_app_config = parser.app_config;
            }

            fs::path current_path(current_file_path);
            fs::path parent_path = current_path.parent_path();

            for (const auto &import_path_str : parser.imports)
            {
                fs::path import_path = parent_path / import_path_str;
                try
                {
                    std::string abs_path = fs::canonical(import_path).string();
                    if (processed_files.find(abs_path) == processed_files.end())
                    {
                        file_queue.push(abs_path);
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << colors::RED << "Error:" << colors::RESET << " resolving import path " << import_path_str << ": " << e.what() << std::endl;
                    return 1;
                }
            }
        }

        std::cerr << "All files processed. Total components: " << all_components.size() << std::endl;

        validate_view_hierarchy(all_components);
        validate_mutability(all_components);
        validate_types(all_components, all_global_enums, all_global_data);

        // Determine output filename
        fs::path input_path(input_file);
        fs::path output_path;
        fs::path final_output_dir;

        if (!output_dir.empty())
        {
            fs::path out_dir_path(output_dir);
            try
            {
                fs::create_directories(out_dir_path);
            }
            catch (const fs::filesystem_error &e)
            {
                std::cerr << "Error: Could not create output directory " << output_dir << ": " << e.what() << std::endl;
                return 1;
            }
            final_output_dir = out_dir_path;
        }
        else
        {
            final_output_dir = input_path.parent_path();
            if (final_output_dir.empty())
                final_output_dir = ".";
        }

        // Create cache directory in project folder (alongside output dir)
        fs::path cache_dir = final_output_dir.parent_path() / ".coi_cache";
        if (final_output_dir.filename() == ".")
        {
            cache_dir = fs::current_path() / ".coi_cache";
        }
        fs::create_directories(cache_dir);

        // Generate .cc in output dir if --keep-cc or --cc-only, otherwise in cache
        if (keep_cc || cc_only)
        {
            output_path = final_output_dir / "app.cc";
        }
        else
        {
            output_path = cache_dir / "app.cc";
        }

        std::string output_cc = output_path.string();

        std::ofstream out(output_cc);
        if (!out)
        {
            std::cerr << "Error: Could not open output file " << output_cc << std::endl;
            return 1;
        }

        // Code generation - automatically detect required headers
        std::set<std::string> required_headers = get_required_headers(all_components);
        for (const auto &header : required_headers)
        {
            out << "#include \"webcc/" << header << ".h\"\n";
        }
        out << "#include \"webcc/core/function.h\"\n";
        out << "#include \"webcc/core/allocator.h\"\n";
        out << "#include \"webcc/core/new.h\"\n";
        out << "#include \"webcc/core/array.h\"\n";
        out << "#include \"webcc/core/vector.h\"\n";
        out << "#include \"webcc/core/random.h\"\n";

        // Sort components topologically so dependencies come first
        auto sorted_components = topological_sort_components(all_components);

        // Detect which runtime features are actually used
        FeatureFlags features = detect_features(all_components, required_headers);

        // Emit JSON runtime helpers inline if Json.parse is used
        if (features.json)
        {
            emit_json_runtime(out);
        }
        out << "\n";

        // Register all data types in the DataTypeRegistry for JSON codegen
        DataTypeRegistry::instance().clear();
        for (const auto &data_def : all_global_data)
        {
            DataTypeRegistry::instance().register_type(data_def->name, data_def->fields);
        }
        for (const auto &comp : all_components)
        {
            for (const auto &data_def : comp.data)
            {
                DataTypeRegistry::instance().register_type(data_def->name, data_def->fields);
            }
        }

        // Populate global set of components with scoped CSS (for view.cc to conditionally emit scope attributes)
        extern std::set<std::string> g_components_with_scoped_css;
        g_components_with_scoped_css.clear();
        for (const auto& comp : all_components) {
            if (!comp.css.empty()) {
                g_components_with_scoped_css.insert(comp.name);
            }
        }

        // Generic event dispatcher template (only if needed)
        if (needs_dispatcher(features))
        {
            out << "template<typename Callback, int MaxListeners = 64>\n";
            out << "struct Dispatcher {\n";
            out << "    int32_t handles[MaxListeners];\n";
            out << "    Callback callbacks[MaxListeners];\n";
            out << "    int count = 0;\n";
            out << "    void set(webcc::handle h, Callback cb) {\n";
            out << "        int32_t hid = (int32_t)h;\n";
            out << "        for (int i = 0; i < count; i++) {\n";
            out << "            if (handles[i] == hid) { callbacks[i] = cb; return; }\n";
            out << "        }\n";
            out << "        if (count < MaxListeners) {\n";
            out << "            handles[count] = hid;\n";
            out << "            callbacks[count] = cb;\n";
            out << "            count++;\n";
            out << "        }\n";
            out << "    }\n";
            out << "    void remove(webcc::handle h) {\n";
            out << "        int32_t hid = (int32_t)h;\n";
            out << "        for (int i = 0; i < count; i++) {\n";
            out << "            if (handles[i] == hid) {\n";
            out << "                handles[i] = handles[count-1];\n";
            out << "                callbacks[i] = callbacks[count-1];\n";
            out << "                count--;\n";
            out << "                return;\n";
            out << "            }\n";
            out << "        }\n";
            out << "    }\n";
            out << "    template<typename... Args>\n";
            out << "    bool dispatch(webcc::handle h, Args&&... args) {\n";
            out << "        int32_t hid = (int32_t)h;\n";
            out << "        for (int i = 0; i < count; i++) {\n";
            out << "            if (handles[i] == hid) { callbacks[i](args...); return true; }\n";
            out << "        }\n";
            out << "        return false;\n";
            out << "    }\n";
            out << "};\n\n";
        }

        out << "int g_view_depth = 0;\n";

        // Emit feature-specific globals (dispatchers, callbacks, etc.)
        emit_feature_globals(out, features);
        out << "\n";

        // Create compiler session for cross-component state
        CompilerSession session;

        // Populate component info for parent-child reactivity wiring
        for (auto *comp : sorted_components)
        {
            ComponentMemberInfo info;
            for (const auto &param : comp->params)
            {
                if (param->is_public && param->is_mutable)
                {
                    info.pub_mut_members.insert(param->name);
                }
            }
            session.component_info[comp->name] = info;
        }

        // Output global enums (defined outside components)
        for (const auto &enum_def : all_global_enums)
        {
            out << enum_def->to_webcc();
        }
        if (!all_global_enums.empty())
        {
            out << "\n";
        }

        // Output global data types (defined outside components)
        for (const auto &data_def : all_global_data)
        {
            out << data_def->to_webcc();
        }
        if (!all_global_data.empty())
        {
            out << "\n";
        }

        // Output Meta structs for JSON parsing (if Json.parse is used)
        if (features.json)
        {
            for (const auto &data_def : all_global_data)
            {
                out << generate_meta_struct(data_def->name);
            }
            for (const auto &comp : all_components)
            {
                for (const auto &data_def : comp.data)
                {
                    out << generate_meta_struct(data_def->name);
                }
            }
            out << "\n";
        }

        // Forward declarations
        for (auto *comp : sorted_components)
        {
            out << "struct " << comp->name << ";\n";
        }
        out << "\n";

        // Forward declare global navigation functions (defined after components)
        out << "void g_app_navigate(const webcc::string& route);\n";
        out << "webcc::string g_app_get_route();\n\n";

        for (auto *comp : sorted_components)
        {
            out << comp->to_webcc(session);
        }

        if (final_app_config.root_component.empty())
        {
            std::cerr << "Error: No root component defined. Use 'app { root = ComponentName }' to define the entry point." << std::endl;
            return 1;
        }

        out << "\n"
            << final_app_config.root_component << "* app = nullptr;\n";

        if (features.router)
        {
            out << "void g_app_navigate(const webcc::string& route) { if (app) app->navigate(route); }\n";
            out << "webcc::string g_app_get_route() { return app ? app->_current_route : \"\"; }\n";
        }
        else
        {
            // Stub functions if no router - prevents linker errors
            out << "void g_app_navigate(const webcc::string& route) {}\n";
            out << "webcc::string g_app_get_route() { return \"\"; }\n";
        }

        out << "void dispatch_events(const webcc::Event* events, uint32_t event_count) {\n";
        out << "    for (uint32_t i = 0; i < event_count; i++) {\n";
        out << "        const auto& e = events[i];\n";
        out << "        if (false) {\n"; // Dummy to allow all handlers to use "} else if"
        emit_feature_event_handlers(out, features);
        out << "        }\n";
        out << "    }\n";
        out << "}\n\n";
        out << "void update_wrapper(double time) {\n";
        out << "    static double last_time = 0;\n";
        out << "    double dt = (time - last_time) / 1000.0;\n";
        out << "    last_time = time;\n";
        out << "    if (dt > 0.1) dt = 0.1; // Cap dt to avoid huge jumps\n";
        out << "    static webcc::Event events[64];\n";
        ;
        out << "    uint32_t count = 0;\n";
        out << "    webcc::Event e;\n";
        out << "    while (webcc::poll_event(e) && count < 64) {\n";
        out << "        events[count++] = e;\n";
        out << "    }\n";
        out << "    dispatch_events(events, count);\n";
        // Only call tick if the root component has a tick method
        if (session.components_with_tick.count(final_app_config.root_component))
        {
            out << "    if (app) app->tick(dt);\n";
        }
        out << "    webcc::flush();\n";
        out << "}\n\n";

        out << "int main() {\n";
        out << "    // We allocate the app on the heap because the stack is destroyed when main() returns.\n";
        out << "    // The app needs to persist for the event loop (update_wrapper).\n";
        out << "    // We use webcc::malloc to ensure memory is tracked by the framework.\n";
        out << "    void* app_mem = webcc::malloc(sizeof(" << final_app_config.root_component << "));\n";
        out << "    app = new (app_mem) " << final_app_config.root_component << "();\n";
        emit_feature_init(out, features, final_app_config.root_component);

        out << "    app->view();\n";
        out << "    webcc::system::set_main_loop(update_wrapper);\n";
        out << "    webcc::flush();\n";
        out << "    return 0;\n";
        out << "}\n";

        out.close();
        if (keep_cc)
        {
            std::cerr << "Generated " << output_cc << std::endl;
        }

        if (!cc_only)
        {
            // Generate CSS file with all styles
            {
                fs::path css_path = final_output_dir / "app.css";
                std::ofstream css_out(css_path);
                if (css_out)
                {
                    // Bundle external stylesheets from styles/ folder at project root
                    // Project root is the parent of src/ 
                    fs::path input_dir = fs::path(input_file).parent_path();
                    fs::path project_root = (input_dir.filename() == "src") ? input_dir.parent_path() : input_dir;
                    fs::path styles_dir = project_root / "styles";
                    
                    if (fs::exists(styles_dir) && fs::is_directory(styles_dir))
                    {
                        std::vector<fs::path> css_files;
                        for (const auto &entry : fs::recursive_directory_iterator(styles_dir))
                        {
                            if (entry.is_regular_file() && entry.path().extension() == ".css")
                            {
                                css_files.push_back(entry.path());
                            }
                        }
                        
                        if (!css_files.empty())
                        {
                            // Sort for deterministic order
                            std::sort(css_files.begin(), css_files.end());
                            
                            for (const auto &css_path : css_files)
                            {
                                std::ifstream style_file(css_path);
                                if (style_file)
                                {
                                    fs::path rel_path = fs::relative(css_path, styles_dir.parent_path());
                                    css_out << "/* " << rel_path.string() << " */\n";
                                    css_out << std::string((std::istreambuf_iterator<char>(style_file)),
                                                            std::istreambuf_iterator<char>());
                                    css_out << "\n";
                                }
                                else
                                {
                                    std::cerr << colors::YELLOW << "Warning:" << colors::RESET
                                              << " Could not open stylesheet: " << css_path.string() << std::endl;
                                }
                            }
                        }
                    }

                    // Collect all CSS from components
                    for (const auto &comp : all_components)
                    {
                        bool has_styles = !comp.global_css.empty() || !comp.css.empty();
                        if (has_styles)
                        {
                            css_out << "/* " << comp.name << " */\n";
                        }

                        // Global CSS (no scoping)
                        if (!comp.global_css.empty())
                        {
                            css_out << comp.global_css << "\n";
                        }

                        // Scoped CSS: prefix selectors with [coi-scope="ComponentName"]
                        // Handle @keyframes and @media specially
                        if (!comp.css.empty())
                        {
                            std::string raw = comp.css;
                            size_t pos = 0;

                            // Helper lambda to scope a single selector
                            auto scope_selector = [&](const std::string &sel) -> std::string
                            {
                                size_t start = sel.find_first_not_of(" \t\n\r");
                                size_t end = sel.find_last_not_of(" \t\n\r");
                                if (start == std::string::npos)
                                    return sel;
                                std::string trimmed = sel.substr(start, end - start + 1);
                                size_t colon = trimmed.find(':');
                                if (colon != std::string::npos)
                                {
                                    return trimmed.substr(0, colon) + "[coi-scope=\"" + comp.name + "\"]" + trimmed.substr(colon);
                                }
                                else
                                {
                                    return trimmed + "[coi-scope=\"" + comp.name + "\"]";
                                }
                            };

                            while (pos < raw.length())
                            {
                                // Skip whitespace
                                while (pos < raw.length() && std::isspace(raw[pos]))
                                {
                                    css_out << raw[pos];
                                    pos++;
                                }
                                if (pos >= raw.length())
                                    break;

                                // Check for @keyframes
                                if (raw.substr(pos, 10) == "@keyframes")
                                {
                                    size_t kf_start = pos;
                                    size_t kf_brace = raw.find('{', pos);
                                    if (kf_brace == std::string::npos)
                                    {
                                        css_out << raw.substr(pos);
                                        break;
                                    }
                                    // Output @keyframes name as-is (no scoping)
                                    css_out << raw.substr(pos, kf_brace - pos + 1);
                                    pos = kf_brace + 1;

                                    // Find matching closing brace for @keyframes block
                                    int brace_depth = 1;
                                    size_t kf_end = pos;
                                    while (kf_end < raw.length() && brace_depth > 0)
                                    {
                                        if (raw[kf_end] == '{')
                                            brace_depth++;
                                        else if (raw[kf_end] == '}')
                                            brace_depth--;
                                        kf_end++;
                                    }
                                    // Output keyframes content as-is (from, to, percentages don't get scoped)
                                    css_out << raw.substr(pos, kf_end - pos);
                                    pos = kf_end;
                                    continue;
                                }

                                // Check for @media
                                if (raw.substr(pos, 6) == "@media")
                                {
                                    size_t media_brace = raw.find('{', pos);
                                    if (media_brace == std::string::npos)
                                    {
                                        css_out << raw.substr(pos);
                                        break;
                                    }
                                    // Output @media query as-is
                                    css_out << raw.substr(pos, media_brace - pos + 1) << "\n";
                                    pos = media_brace + 1;

                                    // Find matching closing brace for @media block
                                    int brace_depth = 1;
                                    size_t media_end = pos;
                                    while (media_end < raw.length() && brace_depth > 0)
                                    {
                                        if (raw[media_end] == '{')
                                            brace_depth++;
                                        else if (raw[media_end] == '}')
                                            brace_depth--;
                                        media_end++;
                                    }
                                    media_end--; // Back up to the closing brace

                                    // Process selectors inside @media
                                    while (pos < media_end)
                                    {
                                        size_t brace = raw.find('{', pos);
                                        if (brace == std::string::npos || brace >= media_end)
                                            break;

                                        std::string selector_group = raw.substr(pos, brace - pos);
                                        std::stringstream ss_sel(selector_group);
                                        std::string selector;
                                        bool first = true;
                                        while (std::getline(ss_sel, selector, ','))
                                        {
                                            if (!first)
                                                css_out << ",";
                                            css_out << scope_selector(selector);
                                            first = false;
                                        }

                                        size_t end_brace = raw.find('}', brace);
                                        if (end_brace == std::string::npos || end_brace >= media_end)
                                        {
                                            css_out << raw.substr(brace, media_end - brace);
                                            break;
                                        }
                                        css_out << raw.substr(brace, end_brace - brace + 1) << "\n";
                                        pos = end_brace + 1;
                                    }
                                    css_out << "}\n";
                                    pos = media_end + 1;
                                    continue;
                                }

                                // Regular selector
                                size_t brace = raw.find('{', pos);
                                if (brace == std::string::npos)
                                {
                                    css_out << raw.substr(pos);
                                    break;
                                }

                                std::string selector_group = raw.substr(pos, brace - pos);
                                std::stringstream ss_sel(selector_group);
                                std::string selector;
                                bool first = true;
                                while (std::getline(ss_sel, selector, ','))
                                {
                                    if (!first)
                                        css_out << ",";
                                    css_out << scope_selector(selector);
                                    first = false;
                                }

                                size_t end_brace = raw.find('}', brace);
                                if (end_brace == std::string::npos)
                                {
                                    css_out << raw.substr(brace);
                                    break;
                                }
                                css_out << raw.substr(brace, end_brace - brace + 1) << "\n";
                                pos = end_brace + 1;
                            }
                            css_out << "\n";
                        }
                    }
                    css_out.close();
                    std::cerr << "Generated " << css_path.string() << std::endl;
                }
            }
        } // end if (!cc_only) for CSS

        if (!cc_only)
        {
            // Generate HTML template in cache directory
            fs::path template_path = cache_dir / "index.template.html";
            {
                std::ofstream tmpl_out(template_path);
                if (tmpl_out)
                {
                    std::string lang = final_app_config.lang.empty() ? "en" : final_app_config.lang;
                    std::string title = final_app_config.title.empty() ? "Coi App" : final_app_config.title;

                    tmpl_out << "<!DOCTYPE html>\n";
                    tmpl_out << "<html lang=\"" << lang << "\">\n";
                    tmpl_out << "<head>\n";
                    tmpl_out << "    <meta charset=\"utf-8\">\n";
                    tmpl_out << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, viewport-fit=cover\">\n";
                    tmpl_out << "    <title>" << title << "</title>\n";
                    if (!final_app_config.description.empty())
                    {
                        tmpl_out << "    <meta name=\"description\" content=\"" << final_app_config.description << "\">\n";
                    }
                    // Auto-include generated CSS
                    tmpl_out << "    <link rel=\"stylesheet\" href=\"app.css\">\n";
                    tmpl_out << "</head>\n";
                    tmpl_out << "<body>\n";
                    tmpl_out << "{{script}}\n";
                    tmpl_out << "</body>\n";
                    tmpl_out << "</html>\n";
                    tmpl_out.close();
                }
            }

            fs::path abs_output_cc = fs::absolute(output_cc);
            fs::path abs_output_dir = fs::absolute(final_output_dir);
            fs::path abs_template = fs::absolute(template_path);
            fs::path webcc_cache_dir = cache_dir / "webcc";
            fs::create_directories(webcc_cache_dir);

            // Find webcc relative to coi binary (in deps/webcc/)
            fs::path exe_dir = get_executable_dir();
            fs::path webcc_path = exe_dir / "deps" / "webcc" / "webcc";
            if (!fs::exists(webcc_path))
            {
                std::cerr << colors::RED << "Error:" << colors::RESET << " Could not find webcc at " << webcc_path << std::endl;
                return 1;
            }

            std::string cmd = webcc_path.string() + " " + abs_output_cc.string();
            cmd += " --out " + abs_output_dir.string();
            cmd += " --cache-dir " + webcc_cache_dir.string();
            cmd += " --template " + abs_template.string();

            std::cerr << "Running: " << cmd << std::endl;
            int ret = system(cmd.c_str());

            // Clean up intermediate files from cache (keep webcc cache for faster rebuilds)
            if (!keep_cc)
            {
                fs::remove(cache_dir / "app.cc");
            }
            fs::remove(cache_dir / "index.template.html");

            if (ret != 0)
            {
                std::cerr << "Error: webcc compilation failed." << std::endl;
                return 1;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << colors::RED << "Error:" << colors::RESET << " " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
