#include "node.h"
#include "../defs/def_parser.h"
#include <cctype>
#include <algorithm>

std::string convert_type(const std::string& type) {
    if (type == "string") return "coi::string";
    
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
    // Skip webcc:: types - those should stay as-is
    size_t dcolon_pos = type.find("::");
    if (dcolon_pos != std::string::npos) {
        std::string prefix = type.substr(0, dcolon_pos);
        // Only convert if it's not a webcc type (webcc uses C++ namespace syntax)
        if (prefix != "webcc") {
            std::string name = type.substr(dcolon_pos + 2);
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
    // Handle fixed-size arrays: T[N]
    size_t bracket_pos = type.rfind('[');
    if (bracket_pos != std::string::npos && type.back() == ']') {
        std::string size_str = type.substr(bracket_pos + 1, type.length() - bracket_pos - 2);
        // Check if it's a number (fixed-size array)
        bool is_number = !size_str.empty() && std::all_of(size_str.begin(), size_str.end(), ::isdigit);
        if (is_number) {
            std::string inner = type.substr(0, bracket_pos);
            return "coi::array<" + convert_type(inner) + ", " + size_str + ">";
        }
    }
    // Check if type is a webcc handle type and add prefix
    if (DefSchema::instance().is_handle(type)) {
        return "webcc::" + type;
    }
    return type;
}
