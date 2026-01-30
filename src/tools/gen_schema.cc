// Generates .coi definition files for the coi compiler
// Reads webcc's schema definitions and produces def/*.d.coi files
// These are the source of truth for type information, method mappings, etc.

#include <iostream>
#include <fstream>
#include <set>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cctype>

// Include webcc's schema definitions
#include "../../deps/webcc/src/cli/schema.h"
#include "../../deps/webcc/src/cli/webcc_schema.h"

// Functions that are handled by Coi language constructs (not exposed directly)
// Format: "namespace::function_name" to allow same function names in different namespaces
static const std::set<std::string> EXCLUDED_FUNCTIONS = {
    "system::set_main_loop",               // Handled by tick {}
    "dom::add_click_listener",             // Handled by onClick attribute
    "input::init_keyboard",                // Called internally when Input.isKeyDown is used
    "input::init_mouse",                   // Handled by onMouseDown/onMouseMove/onMouseUp attributes
    "dom::create_element_deferred",        // Internal compiler function
    "dom::create_element_deferred_scoped", // Internal compiler function (scoped CSS)
    "dom::create_element_scoped",          // Internal compiler function (scoped CSS)
    "dom::create_comment_deferred",        // Internal compiler function
    "dom::add_input_listener",             // Handled by onInput attribute
    "dom::add_change_listener",            // Handled by onChange attribute
    "dom::add_keydown_listener",           // Handled by onKeydown attribute
    "websocket::connect",                  // WebSocket.connect with callbacks handled via intrinsic
    "fetch::get",                          // FetchRequest.get with callbacks handled via intrinsic
    "fetch::post",                         // FetchRequest.post with callbacks handled via intrinsic
};

// Convert snake_case to camelCase for Coi function names
std::string to_camel_case(const std::string &snake)
{
    std::string result;
    bool capitalize_next = false;
    for (char c : snake)
    {
        if (c == '_')
        {
            capitalize_next = true;
        }
        else
        {
            if (capitalize_next)
            {
                result += std::toupper(c);
                capitalize_next = false;
            }
            else
            {
                result += c;
            }
        }
    }
    return result;
}

// Convert webcc type to Coi type
std::string to_coi_type(const std::string &type, const std::string &handle_type)
{
    if (type == "handle" && !handle_type.empty())
    {
        return handle_type;
    }
    if (type == "int32")
        return "int32";
    if (type == "uint32")
        return "uint32";
    if (type == "uint8")
        return "uint8";
    if (type == "int64")
        return "int64";
    if (type == "uint64")
        return "uint64";
    if (type == "float32")
        return "float32";
    if (type == "float64")
        return "float64";
    if (type == "string")
        return "string";
    if (type == "bool")
        return "bool";
    if (type == "func_ptr")
        return "func"; // Special case
    return type;
}

int main()
{
    std::set<std::string> handles;
    std::map<std::string, std::string> type_to_ns; // Type name -> namespace (e.g., "DOMElement" -> "dom")

    // Force rebuild by touching this file
    std::cout << "[Coi] Regenerating schema..." << std::endl;

    // Collect all handle types from commands and map them to namespaces
    for (const auto *c = webcc::SCHEMA_COMMANDS; !c->ns.empty(); ++c)
    {
        // Check return handle type
        if (!c->return_handle_type.empty())
        {
            handles.insert(c->return_handle_type);
            // Map handle type to namespace (first occurrence wins)
            if (type_to_ns.find(c->return_handle_type) == type_to_ns.end())
            {
                type_to_ns[c->return_handle_type] = c->ns;
            }
        }
        // Check param handle types - if first param is a handle, map it to this namespace
        for (size_t i = 0; i < c->params.size(); ++i)
        {
            const auto &p = c->params[i];
            if (!p.handle_type.empty())
            {
                handles.insert(p.handle_type);
                // First param handle type defines the receiver type for instance methods
                if (i == 0 && type_to_ns.find(p.handle_type) == type_to_ns.end())
                {
                    type_to_ns[p.handle_type] = c->ns;
                }
            }
        }
    }

    // Add utility/static-only namespaces as types (e.g., System -> system, Input -> input)
    // These are namespaces that have functions but NO associated handle types
    // Don't add for namespaces like "dom" which have DOMElement, "canvas" which has Canvas, etc.
    std::set<std::string> namespaces_with_funcs;
    std::set<std::string> namespaces_with_handles; // Namespaces that have handle types
    for (const auto *c = webcc::SCHEMA_COMMANDS; !c->ns.empty(); ++c)
    {
        namespaces_with_funcs.insert(c->ns);
        // If this command has handle types, mark the namespace as having handles
        if (!c->return_handle_type.empty())
        {
            namespaces_with_handles.insert(c->ns);
        }
        for (const auto &p : c->params)
        {
            if (!p.handle_type.empty())
            {
                namespaces_with_handles.insert(c->ns);
            }
        }
    }

    // Helper to capitalize first letter
    auto capitalize = [](const std::string &s) -> std::string
    {
        if (s.empty())
            return s;
        std::string result = s;
        result[0] = std::toupper(result[0]);
        return result;
    };

    // For utility-only namespaces (no handles), add capitalized name as a type
    // e.g., System -> system, Storage -> storage, Input -> input
    // But NOT Dom -> dom (because dom has DOMElement)
    for (const auto &ns : namespaces_with_funcs)
    {
        // Skip namespaces that have handle types - users must use the handle type name
        if (namespaces_with_handles.count(ns))
        {
            continue;
        }
        std::string type_name = capitalize(ns);
        if (type_to_ns.find(type_name) == type_to_ns.end())
        {
            type_to_ns[type_name] = ns;
        }
    }

    // Collect from events too
    for (const auto *e = webcc::SCHEMA_EVENTS; !e->ns.empty(); ++e)
    {
        for (const auto &p : e->params)
        {
            if (!p.handle_type.empty())
            {
                handles.insert(p.handle_type);
            }
        }
    }

    // Collect from inheritance
    for (const auto *kv = webcc::HANDLE_INHERITANCE; kv->first != nullptr; ++kv)
    {
        handles.insert(kv->first);
        handles.insert(kv->second);
    }

    // =========================================================
    // Generate .coi definition files in /def/web folder
    // =========================================================
    namespace fs = std::filesystem;

    // Create def/web directory
    fs::create_directories("def/web");

    // Group commands by namespace
    std::map<std::string, std::vector<const webcc::SchemaCommand *>> commands_by_ns;
    std::map<std::string, std::set<std::string>> handles_by_ns; // Track which handles belong to which namespace

    for (const auto *c = webcc::SCHEMA_COMMANDS; !c->ns.empty(); ++c)
    {
        // Track handle types for this namespace BEFORE exclusion check
        // This ensures namespaces with intrinsic-only types still get generated
        if (!c->return_handle_type.empty())
        {
            handles_by_ns[c->ns].insert(c->return_handle_type);
            // Ensure namespace exists in commands_by_ns even if all commands are excluded
            if (commands_by_ns.find(c->ns) == commands_by_ns.end())
            {
                commands_by_ns[c->ns] = {};
            }
        }

        // Skip excluded functions (check namespace::function_name)
        if (EXCLUDED_FUNCTIONS.count(c->ns + "::" + c->func_name))
        {
            continue;
        }
        // Skip functions with func_ptr params (not supported in Coi)
        bool has_func_ptr = false;
        for (const auto &p : c->params)
        {
            if (p.type == "func_ptr")
            {
                has_func_ptr = true;
                break;
            }
        }
        if (has_func_ptr)
            continue;

        commands_by_ns[c->ns].push_back(c);
    }

    // Group commands by their "receiver" handle type (first param if it's a handle)
    // This lets us show methods on handle types properly
    struct MethodInfo
    {
        const webcc::SchemaCommand *cmd;
        std::string receiver_type; // Empty if standalone function
    };

    // Generate a .coi file for each namespace
    for (const auto &[ns, commands] : commands_by_ns)
    {
        std::string filename = "def/web/" + ns + ".d.coi";
        std::ofstream out(filename);
        if (!out)
        {
            std::cerr << "[Coi] Error: Cannot create " << filename << std::endl;
            continue;
        }

        std::string header_file = "webcc/" + ns + ".h";
        std::string ns_type = capitalize(ns); // e.g., "storage" -> "Storage"

        out << "// GENERATED FILE - DO NOT EDIT\n";
        out << "// Coi definitions for " << ns << " namespace\n";
        out << "// Maps to: " << header_file << "\n";
        out << "\n";

        // Categorize functions:
        // 1. Methods on handle types (first param is handle)
        // 2. Static factories (returns handle matching namespace, e.g., Image.load)
        // 3. Namespace utilities (everything else -> Storage.clear, System.log)

        std::vector<const webcc::SchemaCommand *> static_factories;
        std::vector<const webcc::SchemaCommand *> namespace_utils;
        std::map<std::string, std::vector<const webcc::SchemaCommand *>> methods_by_handle;

        for (const auto *cmd : commands)
        {
            // Check if first param is a handle (making this an instance method)
            if (!cmd->params.empty() && cmd->params[0].type == "handle" && !cmd->params[0].handle_type.empty())
            {
                methods_by_handle[cmd->params[0].handle_type].push_back(cmd);
            }
            // Check if it returns a handle that matches the namespace (static factory)
            else if (!cmd->return_handle_type.empty() &&
                     (cmd->return_handle_type == ns_type ||
                      std::tolower(cmd->return_handle_type[0]) == ns[0]))
            {
                static_factories.push_back(cmd);
            }
            // Everything else is a namespace utility
            else
            {
                namespace_utils.push_back(cmd);
            }
        }

        // Group static factories by return type
        std::map<std::string, std::vector<const webcc::SchemaCommand *>> factories_by_type;
        for (const auto *cmd : static_factories)
        {
            factories_by_type[cmd->return_handle_type].push_back(cmd);
        }

        // Collect all handle types that need to be generated (either have factories, methods, or intrinsics)
        std::set<std::string> all_handle_types;
        for (const auto &[type, _] : factories_by_type)
            all_handle_types.insert(type);
        for (const auto &[type, _] : methods_by_handle)
            all_handle_types.insert(type);
        // Also include handle types from this namespace (even if all commands are excluded - for intrinsics)
        if (handles_by_ns.count(ns))
        {
            for (const auto &type : handles_by_ns[ns])
            {
                all_handle_types.insert(type);
            }
        }

        // Generate each handle type with both static and instance methods combined
        for (const auto &handle_type : all_handle_types)
        {
            // Check for inheritance
            std::string extends = "";
            for (const auto *kv = webcc::HANDLE_INHERITANCE; kv->first != nullptr; ++kv)
            {
                if (kv->first == handle_type)
                {
                    extends = kv->second;
                    break;
                }
            }

            out << "// =========================================================\n";
            out << "// " << handle_type;
            if (!extends.empty())
            {
                out << " (extends " << extends << ")";
            }
            out << "\n";
            out << "// =========================================================\n\n";

            // Add @nocopy annotation for handle types (they are browser resources
            // that cannot be copied, only moved or referenced)
            // Skip if it extends another type - it will inherit @nocopy from parent
            if (extends.empty())
            {
                out << "@nocopy\n";
            }
            out << "type " << handle_type;
            if (!extends.empty())
            {
                out << " extends " << extends;
            }
            out << " {\n";

            // Shared (static) factory methods first
            if (factories_by_type.count(handle_type))
            {
                out << "    // Shared methods (call as " << handle_type << ".methodName(...))\n";
                for (const auto *cmd : factories_by_type[handle_type])
                {
                    std::string coi_name = to_camel_case(cmd->func_name);
                    std::string return_type = to_coi_type(cmd->return_type, cmd->return_handle_type);

                    out << "    @map(\"" << ns << "::" << cmd->func_name << "\")\n";
                    out << "    shared def " << coi_name << "(";

                    bool first = true;
                    for (const auto &p : cmd->params)
                    {
                        if (!first)
                            out << ", ";
                        first = false;
                        std::string param_type = to_coi_type(p.type, p.handle_type);
                        std::string param_name = p.name.empty() ? "arg" : p.name;
                        out << param_type << " " << param_name;
                    }

                    out << "): " << return_type << "\n\n";
                }
            }

            // Instance methods
            if (methods_by_handle.count(handle_type))
            {
                out << "    // Instance methods (call as instance.methodName(...))\n";
                for (const auto *cmd : methods_by_handle[handle_type])
                {
                    std::string coi_name = to_camel_case(cmd->func_name);
                    std::string return_type = to_coi_type(cmd->return_type, cmd->return_handle_type);
                    if (return_type.empty())
                        return_type = "void";

                    out << "    @map(\"" << ns << "::" << cmd->func_name << "\")\n";
                    out << "    def " << coi_name << "(";

                    // Skip first param (it's the receiver/this)
                    bool first = true;
                    for (size_t i = 1; i < cmd->params.size(); ++i)
                    {
                        const auto &p = cmd->params[i];
                        if (!first)
                            out << ", ";
                        first = false;
                        std::string param_type = to_coi_type(p.type, p.handle_type);
                        std::string param_name = p.name.empty() ? "arg" : p.name;
                        out << param_type << " " << param_name;
                    }

                    out << "): " << return_type << "\n\n";
                }
            }

            // Inject WebSocket intrinsics (connect + callback registration)
            if (handle_type == "WebSocket")
            {
                out << "    // WebSocket.connect with optional callback parameters (compiler intrinsic)\n";
                out << "    @intrinsic(\"ws_connect\")\n";
                out << "    shared def connect(string url, "
                    << "def onMessage(string) : void = void, "
                    << "def onOpen() : void = void, "
                    << "def onClose() : void = void, "
                    << "def onError() : void = void"
                    << "): WebSocket\n\n";

                out << "    // Check if the WebSocket is connected (handle is valid)\n";
                out << "    @inline(\"$self.is_valid()\")\n";
                out << "    def isConnected(): bool\n";

                out << "    // WebSocket callback registration (compiler intrinsics)\n";
                out << "    @intrinsic(\"ws_on_message\")\n";
                out << "    def onMessage(def callback(string) : void): void\n\n";
                out << "    @intrinsic(\"ws_on_open\")\n";
                out << "    def onOpen(def callback : void): void\n\n";
                out << "    @intrinsic(\"ws_on_close\")\n";
                out << "    def onClose(def callback : void): void\n\n";
                out << "    @intrinsic(\"ws_on_error\")\n";
                out << "    def onError(def callback : void): void\n";
            }

            // Inject FetchRequest intrinsics (get/post with callbacks)
            if (handle_type == "FetchRequest")
            {
                out << "    // FetchRequest.get with optional callback parameters (compiler intrinsic)\n";
                out << "    @intrinsic(\"fetch_get\")\n";
                out << "    shared def get(string url, "
                    << "def onSuccess(string) : void = void, "
                    << "def onError(string) : void = void"
                    << "): FetchRequest\n\n";

                out << "    // FetchRequest.post with optional callback parameters (compiler intrinsic)\n";
                out << "    @intrinsic(\"fetch_post\")\n";
                out << "    shared def post(string url, string body, "
                    << "def onSuccess(string) : void = void, "
                    << "def onError(string) : void = void"
                    << "): FetchRequest\n";
            }

            out << "}\n\n";
        }

        // Generate namespace utilities as a type with shared methods (e.g., Storage.clear, System.log)
        // These are types with only shared (static) methods - not instantiable
        if (!namespace_utils.empty())
        {
            // Check if we already generated this type (with factories or instance methods)
            if (!all_handle_types.count(ns_type))
            {
                out << "// =========================================================\n";
                out << "// " << ns_type << " (static utilities - not instantiable)\n";
                out << "// =========================================================\n";
                out << "// Usage: " << ns_type << ".methodName(...)\n\n";

                out << "type " << ns_type << " {\n";
                out << "    // Shared methods (call as " << ns_type << ".methodName(...))\n";
            }
            else
            {
                // Type already exists, just add a comment
                out << "    // Additional shared methods\n";
            }

            for (const auto *cmd : namespace_utils)
            {
                std::string coi_name = to_camel_case(cmd->func_name);
                std::string return_type = to_coi_type(cmd->return_type, cmd->return_handle_type);
                if (return_type.empty())
                    return_type = "void";

                out << "    @map(\"" << ns << "::" << cmd->func_name << "\")\n";
                out << "    shared def " << coi_name << "(";

                bool first = true;
                for (const auto &p : cmd->params)
                {
                    if (!first)
                        out << ", ";
                    first = false;
                    std::string param_type = to_coi_type(p.type, p.handle_type);
                    std::string param_name = p.name.empty() ? "arg" : p.name;
                    out << param_type << " " << param_name;
                }

                out << "): " << return_type << "\n\n";
            }

            // Inject intrinsics directly into generated files for documentation
            if (ns == "input")
            {
                out << "\n    // Keyboard state queries (compiler intrinsics)\n";
                out << "    @intrinsic(\"key_down\")\n";
                out << "    shared def isKeyDown(int keyCode): bool\n";
                out << "    @intrinsic(\"key_up\")\n";
                out << "    shared def isKeyUp(int keyCode): bool\n";
            }

            if (ns == "system")
            {
                out << "\n    // Router navigation (compiler intrinsics - calls app router)\n";
                out << "    @intrinsic(\"navigate\")\n";
                out << "    shared def navigate(string route): void\n";
                out << "    @intrinsic(\"get_route\")\n";
                out << "    shared def getRoute(): string\n";
                out << "\n    // Force flush of all pending DOM operations (compiler intrinsic)\n";
                out << "    @intrinsic(\"flush\")\n";
                out << "    shared def flush(): void\n";
            }

            if (!all_handle_types.count(ns_type))
            {
                out << "}\n\n";
            }
        }

        out.close();
        std::cout << "[Coi] Generated " << filename << " with " << commands.size() << " functions" << std::endl;
    }

    // =========================================================
    // Generate main index file (def/web/index.d.coi)
    // =========================================================
    {
        std::ofstream out("def/web/index.d.coi");
        if (!out)
        {
            std::cerr << "[Coi] Error: Cannot create def/web/index.d.coi" << std::endl;
            return 1;
        }

        out << "// GENERATED FILE - DO NOT EDIT\n";
        out << "// Coi Standard Library Index\n";
        out << "//\n";
        out << "// This file lists all available Coi definitions.\n";
        out << "// These map to the webcc library for web platform access.\n";
        out << "//\n";
        out << "// Available modules:\n";

        for (const auto &[ns, commands] : commands_by_ns)
        {
            out << "//   - " << ns << ".d.coi (" << commands.size() << " functions)\n";
        }

        out << "\n";
        out << "// =========================================================\n";
        out << "// All Handle Types\n";
        out << "// =========================================================\n\n";

        // List all handles with their inheritance
        for (const auto &handle : handles)
        {
            std::string extends = "";
            for (const auto *kv = webcc::HANDLE_INHERITANCE; kv->first != nullptr; ++kv)
            {
                if (kv->first == handle)
                {
                    extends = kv->second;
                    break;
                }
            }

            if (!extends.empty())
            {
                out << "// " << handle << " extends " << extends << "\n";
                out << "type " << handle << " extends " << extends << " {}\n\n";
            }
            else
            {
                out << "// " << handle << "\n";
                out << "@nocopy\n";
                out << "type " << handle << " {}\n\n";
            }
        }

        out << "// =========================================================\n";
        out << "// Language Constructs (built into Coi)\n";
        out << "// =========================================================\n";
        out << "//\n";
        out << "// The following functionality is handled by Coi language constructs:\n";
        out << "//\n";
        out << "// - init { ... }          : Runs once when component mounts\n";
        out << "// - tick { ... }          : Main loop (replaces setMainLoop)\n";
        out << "// - style { ... }         : Scoped CSS styles for this component\n";
        out << "// - style global { ... }  : Global CSS styles (not scoped)\n";
        out << "// - onclick={handler}     : Click events (replaces addEventListener)\n";
        out << "// - view { ... }          : DOM generation\n";
        out << "// - component Name { }    : Component definition\n";
        out << "// - prop Type name        : Component properties\n";
        out << "// - mut Type name         : Mutable state\n";
        out << "//\n";

        out.close();
        std::cout << "[Coi] Generated def/web/index.d.coi" << std::endl;
    }

    return 0;
}
