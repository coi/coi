#include "definitions.h"

std::string FunctionDef::to_webcc(const std::string& injected_code) {
    std::string result = convert_type(return_type) + " " + name + "(";
    for(size_t i = 0; i < params.size(); i++){
        if(i > 0) result += ", ";
        result += (params[i].is_mutable ? "" : "const ") + convert_type(params[i].type);
        if(params[i].is_reference) result += "&";
        result += " " + params[i].name;
    }
    result += ") {\n";
    for(auto& stmt : body){
        result += "    " + stmt->to_webcc() + "\n";
    }
    if(!injected_code.empty()) {
        result += injected_code;
    }
    result += "}\n";
    return result;
}

void FunctionDef::collect_modifications(std::set<std::string>& mods) const {
    for(const auto& stmt : body) {
        collect_mods_recursive(stmt.get(), mods);
    }
}

std::string StructDef::to_webcc() {
    std::stringstream ss;
    ss << "struct " << name << " {\n";
    for(const auto& field : fields){
        ss << "    " << convert_type(field.type) << " " << field.name << ";\n";
    }
    // Constructor
    ss << "    " << name << "(";
    for(size_t i = 0; i < fields.size(); i++){
        if(i > 0) ss << ", ";
        ss << convert_type(fields[i].type) << " " << fields[i].name;
    }
    ss << ") : ";
    for(size_t i = 0; i < fields.size(); i++){
        if(i > 0) ss << ", ";
        ss << fields[i].name << "(" << fields[i].name << ")";
    }
    ss << " {}\n";
    ss << "    " << name << "() {}\n"; // Default constructor
    ss << "};\n";
    return ss.str();
}

std::string EnumDef::to_webcc() {
    std::stringstream ss;
    size_t total_values = values.size() + 1; // Including _COUNT

    // Explicitly select the smallest possible type for ALL sizes
    ss << "enum struct " << name << " : ";
    
    if (total_values <= 256) {
        ss << "uint8_t";
    } else if (total_values <= 65536) {
        ss << "uint16_t";
    } else {
        ss << "uint32_t"; // Fallback for massive enums
    }
    
    ss << " {\n";

    // Generate entries
    for (const auto& val : values) {
        ss << "    " << val << ",\n";
    }

    // Add _COUNT and close
    ss << "    _COUNT\n";
    ss << "};\n";
    
    return ss.str();
}