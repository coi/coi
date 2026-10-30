#include "definitions.h"
#include "../codegen/codegen_utils.h"
#include "node.h"
#include "codegen_state.h"
#include "expressions.h"

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
        result += " " + cpp_name(fn.params[i].name);

        ComponentTypeContext::instance().set_method_symbol_type(fn.params[i].name, fn.params[i].type);
    }

    result += ")";
    return result;
}

// the names a method declares itself: its params, locals, loop variables, match bindings and
// lambda params. A component's reference prop of the same name is that variable inside the
// method, not *prop
static void declared_names(ASTNode* node, std::set<std::string>& out) {
    if (!node) return;
    if (auto* v = dynamic_cast<VarDeclaration*>(node)) out.insert(v->name);
    else if (auto* f = dynamic_cast<ForRangeStatement*>(node)) out.insert(f->var_name);
    else if (auto* f = dynamic_cast<ForEachStatement*>(node)) out.insert(f->var_name);
    else if (auto* l = dynamic_cast<LambdaExpr*>(node)) out.insert(l->param);
    else if (auto* m = dynamic_cast<MatchExpr*>(node)) {
        for (auto& arm : m->arms) {
            for (auto& b : arm.pattern.variant_bindings) out.insert(b.name);
            for (auto& f : arm.pattern.fields) if (!f.value) out.insert(f.name);
        }
    }
    for (ASTNode* child : node->get_child_nodes()) declared_names(child, out);
}

std::string FunctionDef::to_webcc(const std::string& injected_code) {
    ComponentTypeContext::instance().begin_method_scope();
    std::set<std::string> own;
    for (auto& p : params) own.insert(p.name);
    for (auto& stmt : body) declared_names(stmt.get(), own);
    std::set<std::string> hidden;
    for (auto& n : own) if (g_ref_props.erase(n)) hidden.insert(n);

    std::string result = function_signature(*this, cpp_name(name)) + " {\n";
    // the updates run when the method leaves, early returns included
    if(!injected_code.empty()) {
        result += "    auto _coi_after = coi::on_exit([&]() {\n" + injected_code + "    });\n";
    }
    for(auto& stmt : body){
        result += "    " + stmt->to_webcc() + "\n";
    }
    result += "}\n";

    for (auto& n : hidden) g_ref_props.insert(n);
    ComponentTypeContext::instance().end_method_scope();
    return result;
}

std::string FunctionDef::free_declaration() {
    ComponentTypeContext::instance().begin_method_scope();
    std::string result = function_signature(*this, cpp_name(qualified_name(module_name, name))) + ";\n";
    ComponentTypeContext::instance().end_method_scope();
    return result;
}

// the line of a statement's first call
static int call_line(ASTNode* node) {
    if (!node) return 0;
    if (auto* call = dynamic_cast<FunctionCall*>(node); call && call->line > 0) return call->line;
    if (auto* call = dynamic_cast<MethodCall*>(node); call && call->line > 0) return call->line;
    for (ASTNode* child : node->get_child_nodes())
        if (int line = call_line(child)) return line;
    return 0;
}

std::string FunctionDef::free_definition() {
    ComponentTypeContext::instance().begin_method_scope();
    std::string result = function_signature(*this, cpp_name(qualified_name(module_name, name))) + " {\n";
    // a test says where it is for a failed page action, and stops at one
    if (!test_name.empty())
        result += "    coi_test_where = " + cpp_string_literal(g_test_file) + ";\n";
    for(auto& stmt : body){
        int line = test_name.empty() ? 0 : call_line(stmt.get());
        if (line) result += "    coi_test_line = " + std::to_string(line) + ";\n";
        result += "    " + stmt->to_webcc() + "\n";
        if (!test_name.empty()) result += "    if (coi_test_failed_now) return;\n";
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

// a union is one struct holding every alternative, with which one it is: a value type like a
// pod, copyable, no heap. Made from an alternative it takes that alternative's first tag; a
// match arm Point(Point p) reads is_Point() and as_Point()._0
static std::string emit_union(const DataDef &u) {
    std::stringstream ss;
    std::string me = qualified_name(u.module_name, u.name);
    ss << "struct " << me << " {\n";
    for (const auto& field : u.fields)
        ss << "    " << convert_type(field.type) << " " << cpp_name(field.name) << ";\n";
    ss << "    int32_t _which = -1;\n";
    for (size_t i = 0; i < u.alts.size(); ++i)
        ss << "    " << convert_type(u.alts[i].type) << " _" << i << "{};\n";
    ss << "    " << me << "() = default;\n";
    for (size_t i = 0; i < u.alts.size(); ++i)
        ss << "    " << me << "(const " << convert_type(u.alts[i].type) << "& v) : " << cpp_name(u.tag) << "(\"" << u.alts[i].tags[0]
           << "\"), _which(" << i << "), _" << i << "(v) {}\n";
    for (size_t i = 0; i < u.alts.size(); ++i) {
        ss << "    bool is_" << u.alts[i].name << "() const { return _which == " << i << "; }\n";
        ss << "    struct _As" << i << " { const " << convert_type(u.alts[i].type) << "& _0; };\n";
        ss << "    _As" << i << " as_" << u.alts[i].name << "() const { return _As" << i << "{_" << i << "}; }\n";
    }
    ss << "    bool operator==(const " << me << "&) const = default;\n";
    ss << "};\n";
    return ss.str();
}

std::string DataDef::to_webcc() {
    if (is_union)
        return emit_union(*this);
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
        ss << "    " << convert_type(field.type) << " " << cpp_name(field.name) << ";\n";
    }
    // == compares field by field; only compiled where a program compares two of them
    ss << "    bool operator==(const " << qualified_name(module_name, name) << "&) const = default;\n";
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
