// Generates .coi definition files for the coi compiler
// Reads webcc's schema.wcc.bin binary cache and produces defs/*.d.coi files
// These are the source of truth for type information, method mappings, etc.

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cctype>

// Include webcc's schema definitions (for types and load_defs_binary)
#include "../../deps/webcc/src/cli/schema.h"

// Whitelist of functions and intrinsics exposed to Coi users
// Loaded from src/tools/schema_whitelist.def at runtime
//
// Format:
//   function_name              - Regular webcc function (auto-generates @map)
//   // comment                 - Comment (preserved if followed by @intrinsic/@inline)
//   @intrinsic("x") def ...    - Raw definition line (emitted as-is)
//   @inline("x") def ...       - Raw definition line (emitted as-is)

static std::set<std::string> WHITELISTED_FUNCTIONS;

// Intrinsic definitions by namespace - stores raw lines to emit (with preceding comments)
static std::map<std::string, std::vector<std::string>> INTRINSIC_DEFS;

// Load whitelist from src/tools/schema_whitelist.def
bool load_whitelist(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
    {
        return false;
    }

    std::string current_ns;
    std::string line;
    std::vector<std::string> pending_comments; // Comments waiting for an @intrinsic/@inline

    while (std::getline(file, line))
    {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
        {
            pending_comments.clear(); // Empty line clears pending comments
            continue;
        }
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        // Skip empty lines
        if (line.empty())
        {
            pending_comments.clear();
            continue;
        }

        // Check for namespace header [namespace]
        if (line[0] == '[' && line.back() == ']')
        {
            current_ns = line.substr(1, line.size() - 2);
            pending_comments.clear();
            continue;
        }

        // Comments starting with // - accumulate for potential @intrinsic/@inline
        if (line.rfind("//", 0) == 0)
        {
            pending_comments.push_back(line);
            continue;
        }

        // Skip # comments (not preserved)
        if (line[0] == '#')
        {
            continue;
        }

        if (!current_ns.empty())
        {
            // Check if it's an intrinsic/inline definition (@intrinsic or @inline)
            if (line[0] == '@')
            {
                // Add pending comments first
                for (const auto &comment : pending_comments)
                {
                    INTRINSIC_DEFS[current_ns].push_back(comment);
                }
                pending_comments.clear();
                // Add the intrinsic definition itself
                INTRINSIC_DEFS[current_ns].push_back(line);
            }
            else
            {
                // Regular function name
                WHITELISTED_FUNCTIONS.insert(current_ns + "::" + line);
                pending_comments.clear();
            }
        }
    }

    return true;
}

// Count total intrinsic definitions
size_t count_intrinsics()
{
    size_t count = 0;
    for (const auto &[ns, defs] : INTRINSIC_DEFS)
    {
        for (const auto &def : defs)
        {
            if (def[0] == '@')
                count++;
        }
    }
    return count;
}

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

    // Load whitelist from src/tools/schema_whitelist.def
    std::string whitelist_path = "src/tools/schema_whitelist.def";
    if (!load_whitelist(whitelist_path))
    {
        std::cerr << "[Coi] Error: Cannot load " << whitelist_path << std::endl;
        std::cerr << "       This file defines which webcc functions are exposed to Coi." << std::endl;
        return 1;
    }
    std::cout << "[Coi] Loaded " << WHITELISTED_FUNCTIONS.size() << " functions, " 
              << count_intrinsics() << " intrinsics from " << whitelist_path << std::endl;

    // Load schema from binary cache (deps/webcc/schema.wcc.bin)
    std::string schema_path = "deps/webcc/schema.wcc.bin";
    webcc::SchemaDefs defs;
    if (!webcc::load_defs_binary(defs, schema_path))
    {
        std::cerr << "[Coi] Error: Cannot load " << schema_path << std::endl;
        std::cerr << "       Run './build.sh' in deps/webcc first to generate the schema cache." << std::endl;
        return 1;
    }
    std::cout << "[Coi] Loaded " << defs.commands.size() << " commands, " << defs.events.size() << " events from " << schema_path << std::endl;

    // Collect all handle types from commands and map them to namespaces
    for (const auto &c : defs.commands)
    {
        // Check return handle type
        if (!c.return_handle_type.empty())
        {
            handles.insert(c.return_handle_type);
            // Map handle type to namespace (first occurrence wins)
            if (type_to_ns.find(c.return_handle_type) == type_to_ns.end())
            {
                type_to_ns[c.return_handle_type] = c.ns;
            }
        }
        // Check param handle types - if first param is a handle, map it to this namespace
        for (size_t i = 0; i < c.params.size(); ++i)
        {
            const auto &p = c.params[i];
            if (!p.handle_type.empty())
            {
                handles.insert(p.handle_type);
                // First param handle type defines the receiver type for instance methods
                if (i == 0 && type_to_ns.find(p.handle_type) == type_to_ns.end())
                {
                    type_to_ns[p.handle_type] = c.ns;
                }
            }
        }
    }

    // Add utility/static-only namespaces as types (e.g., System -> system, Input -> input)
    // These are namespaces that have functions but NO associated handle types
    // Don't add for namespaces like "dom" which have DOMElement, "canvas" which has Canvas, etc.
    std::set<std::string> namespaces_with_funcs;
    std::set<std::string> namespaces_with_handles; // Namespaces that have handle types
    for (const auto &c : defs.commands)
    {
        namespaces_with_funcs.insert(c.ns);
        // If this command has handle types, mark the namespace as having handles
        if (!c.return_handle_type.empty())
        {
            namespaces_with_handles.insert(c.ns);
        }
        for (const auto &p : c.params)
        {
            if (!p.handle_type.empty())
            {
                namespaces_with_handles.insert(c.ns);
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
    for (const auto &e : defs.events)
    {
        for (const auto &p : e.params)
        {
            if (!p.handle_type.empty())
            {
                handles.insert(p.handle_type);
            }
        }
    }

    // Collect from inheritance
    for (const auto &[derived, base] : defs.handle_inheritance)
    {
        handles.insert(derived);
        handles.insert(base);
    }

    // =========================================================
    // Generate .coi definition files in /defs/web folder
    // =========================================================
    namespace fs = std::filesystem;

    // Create defs/web directory
    fs::create_directories("defs/web");

    // Group commands by namespace
    std::map<std::string, std::vector<const webcc::SchemaCommand *>> commands_by_ns;
    std::map<std::string, std::set<std::string>> handles_by_ns; // Track which handles belong to which namespace

    for (const auto &c : defs.commands)
    {
        // Track handle types for this namespace BEFORE exclusion check
        // This ensures namespaces with intrinsic-only types still get generated
        if (!c.return_handle_type.empty())
        {
            handles_by_ns[c.ns].insert(c.return_handle_type);
            // Ensure namespace exists in commands_by_ns even if all commands are excluded
            if (commands_by_ns.find(c.ns) == commands_by_ns.end())
            {
                commands_by_ns[c.ns] = {};
            }
        }

        // Skip functions NOT in the whitelist
        std::string qualified_name = c.ns + "::" + c.func_name;
        if (!WHITELISTED_FUNCTIONS.count(qualified_name))
        {
            continue;
        }
        // Skip functions with func_ptr params (not supported in Coi)
        bool has_func_ptr = false;
        for (const auto &p : c.params)
        {
            if (p.type == "func_ptr")
            {
                has_func_ptr = true;
                break;
            }
        }
        if (has_func_ptr)
            continue;

        commands_by_ns[c.ns].push_back(&c);
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
        std::string filename = "defs/web/" + ns + ".d.coi";
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
            auto inherit_it = defs.handle_inheritance.find(handle_type);
            if (inherit_it != defs.handle_inheritance.end())
            {
                extends = inherit_it->second;
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

            // Emit intrinsic definitions from whitelist for this handle type
            // Handle types like WebSocket, FetchRequest have intrinsics defined in the whitelist
            // We need to check which namespace this handle belongs to
            std::string handle_ns;
            auto ns_it = type_to_ns.find(handle_type);
            if (ns_it != type_to_ns.end())
            {
                handle_ns = ns_it->second;
            }
            if (!handle_ns.empty() && INTRINSIC_DEFS.count(handle_ns))
            {
                for (const auto &def : INTRINSIC_DEFS[handle_ns])
                {
                    out << "    " << def << "\n";
                    // Add blank line after @intrinsic/@inline definitions (not after comments)
                    if (def[0] == '@')
                    {
                        out << "\n";
                    }
                }
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

            // Emit intrinsic definitions from whitelist for this namespace
            if (INTRINSIC_DEFS.count(ns))
            {
                for (const auto &def : INTRINSIC_DEFS[ns])
                {
                    out << "    " << def << "\n";
                    // Add blank line after @intrinsic/@inline definitions (not after comments)
                    if (def[0] == '@')
                    {
                        out << "\n";
                    }
                }
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
    // Generate main index file (defs/web/index.d.coi)
    // =========================================================
    {
        std::ofstream out("defs/web/index.d.coi");
        if (!out)
        {
            std::cerr << "[Coi] Error: Cannot create defs/web/index.d.coi" << std::endl;
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
            auto inherit_it = defs.handle_inheritance.find(handle);
            if (inherit_it != defs.handle_inheritance.end())
            {
                extends = inherit_it->second;
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
        std::cout << "[Coi] Generated defs/web/index.d.coi" << std::endl;
    }

    return 0;
}
