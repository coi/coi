#include "node.h"
#include "../codegen/codegen_utils.h"
#include "../defs/def_parser.h"
#include "../cli/error.h"
#include <cctype>
#include <algorithm>

std::string convert_type(const std::string& type) {
    if (type == "string") return "coi::string";
    
    // Handle generic types: Result<int>, Pair<A, B>
    size_t lt_pos = type.find('<');
    if (lt_pos != std::string::npos && type.back() == '>') {
        std::string base = type.substr(0, lt_pos);
        std::string args_str = type.substr(lt_pos + 1, type.length() - lt_pos - 2);
        
        // Parse and convert type arguments, handling nested generics
        std::string converted_args;
        std::string current_arg;
        int depth = 0;
        
        for (size_t i = 0; i < args_str.length(); ++i) {
            char c = args_str[i];
            if (c == '<') {
                depth++;
                current_arg += c;
            } else if (c == '>') {
                depth--;
                current_arg += c;
            } else if (c == ',' && depth == 0) {
                // Trim whitespace
                size_t start = current_arg.find_first_not_of(" ");
                size_t end = current_arg.find_last_not_of(" ");
                if (start != std::string::npos) {
                    current_arg = current_arg.substr(start, end - start + 1);
                }
                if (!converted_args.empty()) converted_args += ", ";
                converted_args += convert_type(current_arg);
                current_arg.clear();
            } else {
                current_arg += c;
            }
        }
        
        // Handle last argument
        if (!current_arg.empty()) {
            size_t start = current_arg.find_first_not_of(" ");
            size_t end = current_arg.find_last_not_of(" ");
            if (start != std::string::npos) {
                current_arg = current_arg.substr(start, end - start + 1);
            }
            if (!converted_args.empty()) converted_args += ", ";
            converted_args += convert_type(current_arg);
        }
        
        // Convert the base type
        std::string converted_base = convert_type(base);
        return converted_base + "<" + converted_args + ">";
    }
    
    // Check if this is a component-local type and prefix it
    std::string resolved_local = ComponentTypeContext::instance().resolve(type);
    if (resolved_local != type) {
        return resolved_local;
    }
    
    // Check if this is a Meta type for a component-local data type (e.g., TestStructMeta)
    if (type.size() > 4 && type.substr(type.size() - 4) == "Meta") {
        std::string base_type = type.substr(0, type.size() - 4);
        if (ComponentTypeContext::instance().is_local(base_type)) {
            return ComponentTypeContext::instance().resolve(base_type) + "Meta";
        }
    }
    
    // Resolve type aliases from schema (e.g., int -> int32, float -> float64)
    std::string resolved = DefSchema::instance().resolve_alias(type);
    
    // Integer types - explicit bit widths
    if (resolved == "int8") return "int8_t";
    if (resolved == "int16") return "int16_t";
    if (resolved == "int32") return "int32_t";
    if (resolved == "int64") return "int64_t";
    
    // Unsigned integer types
    if (resolved == "uint8") return "uint8_t";
    if (resolved == "uint16") return "uint16_t";
    if (resolved == "uint32") return "uint32_t";
    if (resolved == "uint64") return "uint64_t";
    
    // Floating point types
    if (resolved == "float32") return "float";
    if (resolved == "float64") return "double";
    
    // Handle Module::ComponentName type syntax - convert to Module_ComponentName
    // This handles cross-module component types used in variable declarations
    size_t dcolon_pos = type.find("::");
    if (dcolon_pos != std::string::npos) {
        std::string prefix = type.substr(0, dcolon_pos);
        // :: inside something else, e.g. webcc::function<...>
        bool is_ident = !prefix.empty() &&
                        (std::isalpha(static_cast<unsigned char>(prefix[0])) || prefix[0] == '_') &&
                        std::all_of(prefix.begin(), prefix.end(), [](unsigned char c) {
                            return std::isalnum(c) || c == '_';
                        });
        std::string name = type.substr(dcolon_pos + 2);
        // Geo::Point[] and Geo::Point[string] go through the array/map cases below, element first
        if (is_ident && prefix != "webcc" && prefix != "coi" && prefix != "std" && name.find('[') == std::string::npos) {
            return prefix + "_" + name;
        }
    }
    
    // Handle Component.EnumName type syntax - convert to Component_EnumName
    if (type.find('.') != std::string::npos) {
        std::string result = type;
        size_t pos = result.find('.');
        result.replace(pos, 1, "_");
        return result;
    }
    // Handle dynamic arrays: T[]
    if (type.ends_with("[]")) {
        std::string inner = type.substr(0, type.length() - 2);
        return "coi::vector<" + convert_type(inner) + ">";
    }
    // Handle fixed-size arrays: T[N] and maps: V[K]
    size_t bracket_pos = type.rfind('[');
    if (bracket_pos != std::string::npos && type.back() == ']') {
        std::string bracket_content = type.substr(bracket_pos + 1, type.length() - bracket_pos - 2);
        // Check if it's a number (fixed-size array)
        bool is_number = !bracket_content.empty() && std::all_of(bracket_content.begin(), bracket_content.end(), ::isdigit);
        if (is_number) {
            std::string inner = type.substr(0, bracket_pos);
            return "coi::array<" + convert_type(inner) + ", " + bracket_content + ">";
        }
        // Otherwise it's a map type: ValueType[KeyType]
        if (!bracket_content.empty()) {
            std::string value_type = type.substr(0, bracket_pos);
            std::string key_type = bracket_content;
            return "coi::map<" + convert_type(key_type) + ", " + convert_type(value_type) + ">";
        }
    }
    // Check if type is a webcc handle type and add prefix
    if (DefSchema::instance().is_handle(type)) {
        return "webcc::" + type;
    }
    return type;
}

std::string FreeFunctionRegistry::resolve_call(const std::string& name) const {
    if (functions.empty() || name.empty() || name.find('.') != std::string::npos) return "";
    const auto& ctx = ComponentTypeContext::instance();
    size_t dcolon = name.find("::");
    if (dcolon != std::string::npos) {
        std::string module = name.substr(0, dcolon);
        std::string fn = name.substr(dcolon + 2);
        const FreeFunctionInfo* info = find(module, fn);
        if (!info) return "";
        if (module != ctx.module_name && !info->is_public) {
            ErrorHandler::compiler_error("Function '" + fn + "' in module '" + module +
                "' is not public. Add 'pub' to make it importable: pub def " + fn);
        }
        return cpp_name(qualified_name(module, fn));
    }
    if (!std::islower(static_cast<unsigned char>(name[0]))) return "";
    if (ctx.has_method(name) || !ctx.get_symbol_type(name).empty()) return "";
    if (!find(ctx.module_name, name)) return "";
    return cpp_name(qualified_name(ctx.module_name, name));
}
