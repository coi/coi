#include "definitions.h"
#include "node.h"

// template header + signature, registers params in scope
static std::string function_signature(const FunctionDef& fn, const std::string& emitted_name) {
    std::string result;

    // Generate template declaration for generic functions
    if (!fn.type_params.empty()) {
        result += "template<";
        for (size_t i = 0; i < fn.type_params.size(); ++i) {
            if (i > 0) result += ", ";
            result += "typename " + fn.type_params[i];
        }
        result += ">\n";
    }

    result += convert_type(fn.return_type) + " " + emitted_name + "(";

    for(size_t i = 0; i < fn.params.size(); i++){
        if(i > 0) result += ", ";
        result += (fn.params[i].is_mutable ? "" : "const ") + convert_type(fn.params[i].type);
        if(fn.params[i].is_reference) result += "&";
        result += " " + fn.params[i].name;

        ComponentTypeContext::instance().set_method_symbol_type(fn.params[i].name, fn.params[i].type);
    }

    result += ")";
    return result;
}

std::string FunctionDef::to_webcc(const std::string& injected_code) {
    ComponentTypeContext::instance().begin_method_scope();

    std::string result = function_signature(*this, name) + " {\n";
    // the updates run when the method leaves, early returns included
    if(!injected_code.empty()) {
        result += "    auto _coi_after = coi::on_exit([&]() {\n" + injected_code + "    });\n";
    }
    for(auto& stmt : body){
        result += "    " + stmt->to_webcc() + "\n";
    }
    result += "}\n";

    ComponentTypeContext::instance().end_method_scope();
    return result;
}

std::string FunctionDef::free_declaration() {
    ComponentTypeContext::instance().begin_method_scope();
    std::string result = function_signature(*this, qualified_name(module_name, name)) + ";\n";
    ComponentTypeContext::instance().end_method_scope();
    return result;
}

std::string FunctionDef::free_definition() {
    ComponentTypeContext::instance().begin_method_scope();
    std::string result = function_signature(*this, qualified_name(module_name, name)) + " {\n";
    for(auto& stmt : body){
        result += "    " + stmt->to_webcc() + "\n";
    }
    result += "}\n";
    ComponentTypeContext::instance().end_method_scope();
    return result;
}

void FunctionDef::collect_modifications(std::set<std::string>& mods) const {
    for(const auto& stmt : body) {
        collect_mods_recursive(stmt.get(), mods);
    }
}

void register_free_functions(const std::vector<std::unique_ptr<FunctionDef>>& functions) {
    auto& reg = FreeFunctionRegistry::instance().functions;
    reg.clear();
    for (const auto& fn : functions) {
        FreeFunctionInfo info;
        info.is_public = fn->is_public;
        for (const auto& param : fn->params) {
            info.mut_ref_params.push_back(param.is_mutable && param.is_reference);
        }
        reg[fn->module_name][fn->name] = info;
    }
}

std::string DataDef::to_webcc() {
    std::stringstream ss;
    
    // Generate template declaration for generic types
    if (!type_params.empty()) {
        ss << "template<";
        for (size_t i = 0; i < type_params.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "typename " << type_params[i];
        }
        ss << ">\n";
    }
    
    ss << "struct " << qualified_name(module_name, name) << " {\n";
    for(const auto& field : fields){
        ss << "    " << convert_type(field.type) << " " << field.name << ";\n";
    }
    ss << "};\n";
    return ss.str();
}

// struct around a plain enum so it converts to/from int
std::string emit_coi_enum(const std::string &cpp_name, const std::vector<std::string> &values)
{
    size_t total_values = values.size() + 1; // Including _COUNT
    const char *base = total_values <= 256 ? "uint8_t" : total_values <= 65536 ? "uint16_t" : "uint32_t";
    std::stringstream ss;
    ss << "struct " << cpp_name << " {\n";
    ss << "    enum _V : " << base << " {";
    for (const auto &val : values)
        ss << " " << val << ",";
    ss << " _COUNT };\n";
    ss << "    _V v;\n";
    ss << "    constexpr " << cpp_name << "(_V x = (_V)0) : v(x) {}\n";
    ss << "    constexpr " << cpp_name << "(int x) : v((_V)x) {}\n";
    ss << "    constexpr operator int() const { return v; }\n";
    ss << "};\n";
    return ss.str();
}

std::string EnumDef::to_webcc() {
    return emit_coi_enum(qualified_name(module_name, name), values);
}
