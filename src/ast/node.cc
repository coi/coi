#include "node.h"
#include "../defs/def_parser.h"
#include <cctype>
#include <algorithm>

std::string convert_type(const std::string& type) {
    if (type == "string") return "webcc::string";
    
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
    
    // Handle Component.EnumName type syntax - convert to Component::EnumName
    if (type.find('.') != std::string::npos) {
        std::string result = type;
        size_t pos = result.find('.');
        result.replace(pos, 1, "::");
        return result;
    }
    // Handle dynamic arrays: T[]
    if (type.ends_with("[]")) {
        std::string inner = type.substr(0, type.length() - 2);
        return "webcc::vector<" + convert_type(inner) + ">";
    }
    // Handle fixed-size arrays: T[N]
    size_t bracket_pos = type.rfind('[');
    if (bracket_pos != std::string::npos && type.back() == ']') {
        std::string size_str = type.substr(bracket_pos + 1, type.length() - bracket_pos - 2);
        // Check if it's a number (fixed-size array)
        bool is_number = !size_str.empty() && std::all_of(size_str.begin(), size_str.end(), ::isdigit);
        if (is_number) {
            std::string inner = type.substr(0, bracket_pos);
            return "webcc::array<" + convert_type(inner) + ", " + size_str + ">";
        }
    }
    // Check if type is a webcc handle type and add prefix
    if (DefSchema::instance().is_handle(type)) {
        return "webcc::" + type;
    }
    return type;
}
