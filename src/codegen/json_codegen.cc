// =============================================================================
// JSON Code Generation for Coi - Implementation
// =============================================================================

#include "json_codegen.h"
#include <sstream>

// ============================================================================
// DataTypeRegistry Implementation
// ============================================================================

DataTypeRegistry& DataTypeRegistry::instance() {
    static DataTypeRegistry instance;
    return instance;
}

void DataTypeRegistry::register_type(const std::string& name, const std::vector<DataField>& fields) {
    types_[name] = fields;
}

const std::vector<DataField>* DataTypeRegistry::lookup(const std::string& name) const {
    auto it = types_.find(name);
    return it != types_.end() ? &it->second : nullptr;
}

void DataTypeRegistry::clear() {
    types_.clear();
}

// ============================================================================
// Meta Struct Generation
// ============================================================================

std::string generate_meta_struct(const std::string& data_type) {
    auto* fields = DataTypeRegistry::instance().lookup(data_type);
    if (!fields) return "";
    
    std::stringstream ss;
    ss << "struct " << data_type << "Meta : json::MetaBase {\n";
    
    // Generate has_fieldName() methods for each field
    uint32_t i = 0;
    for (const auto& field : *fields) {
        ss << "    bool has_" << field.name << "() const { return has(" << i << "); }\n";
        i++;
    }
    
    // Nested meta fields for nested data types
    for (const auto& field : *fields) {
        if (!field.type.empty() && std::isupper(field.type[0]) && 
            DataTypeRegistry::instance().lookup(field.type)) {
            ss << "    " << field.type << "Meta " << field.name << ";\n";
        }
    }
    
    ss << "};\n";
    return ss.str();
}

// ============================================================================
// JSON Parse Code Generation
// ============================================================================

// Map Coi types to extraction calls
static std::string get_extractor(const std::string& type, const std::string& pos_var) {
    if (type == "string") {
        return "json::ext_str(_s, " + pos_var + ", _len)";
    }
    if (type == "int") {
        return "json::ext_int(_s, " + pos_var + ", _len, _ok)";
    }
    if (type == "float") {
        return "json::ext_float(_s, " + pos_var + ", _len, _ok)";
    }
    if (type == "bool") {
        return "json::ext_bool(_s, " + pos_var + ", _len, _ok)";
    }
    // Nested data type - will need recursive parsing
    if (!type.empty() && std::isupper(type[0])) {
        return "__json_parse_" + type + "(_s + " + pos_var + ", _len - " + pos_var + ")";
    }
    // Array types handled separately
    return "";
}

// Check if type needs the _ok variable
static bool needs_ok_var(const std::string& type) {
    return type == "int" || type == "float" || type == "bool";
}

// Check if type is an array
static bool is_array_type(const std::string& type) {
    return type.size() > 2 && type.substr(type.size() - 2) == "[]";
}

// Get element type from array type (e.g., "User[]" -> "User")
static std::string get_array_element_type(const std::string& type) {
    return type.substr(0, type.size() - 2);
}

<<<<<<< HEAD
// Forward declaration
static void generate_object_fields_parse(std::stringstream& ss,
                                          const std::string& data_type,
                                          const std::string& result_var,
                                          const std::string& meta_var,
                                          const std::string& src_var,
                                          const std::string& len_var,
                                          const std::string& ok_var,
                                          const std::string& indent);

// Generate inline parsing code for a single primitive field
static void generate_primitive_field_parse(std::stringstream& ss,
                                            const std::string& field_type,
                                            const std::string& field_name,
                                            uint32_t field_idx,
                                            const std::string& result_var,
                                            const std::string& meta_var,
                                            const std::string& src_var,
                                            const std::string& pos_var,
                                            const std::string& len_var,
                                            const std::string& ok_var,
                                            const std::string& indent) {
    ss << indent << "if (!json::is_null(" << src_var << ", " << pos_var << ", " << len_var << ")) {\n";
    if (field_type == "string") {
        ss << indent << "    " << result_var << "." << field_name << " = json::ext_str(" << src_var << ", " << pos_var << ", " << len_var << ");\n";
        ss << indent << "    " << meta_var << ".set(" << field_idx << ");\n";
    } else {
        ss << indent << "    " << result_var << "." << field_name << " = json::ext_" << field_type << "(" << src_var << ", " << pos_var << ", " << len_var << ", " << ok_var << ");\n";
        ss << indent << "    if (" << ok_var << ") " << meta_var << ".set(" << field_idx << ");\n";
    }
    ss << indent << "}\n";
}

// Generate inline parsing code for an array field
static void generate_array_field_parse(std::stringstream& ss,
                                        const std::string& elem_type,
                                        const std::string& field_name,
                                        uint32_t field_idx,
                                        const std::string& result_var,
                                        const std::string& meta_var,
                                        const std::string& src_var,
                                        const std::string& pos_var,
                                        const std::string& len_var,
                                        const std::string& indent) {
    ss << indent << "auto _arr_view = json::isolate(" << src_var << ", " << pos_var << ", " << len_var << ");\n";
    ss << indent << "if (_arr_view.length() > 0) {\n";
    ss << indent << "    json::for_each(_arr_view.data(), 0, _arr_view.length(), [&](const char* _aes, uint32_t _aep, uint32_t _aelen) {\n";
    
    if (elem_type == "string") {
        ss << indent << "        " << result_var << "." << field_name << ".push_back(json::ext_str(_aes, _aep, _aelen));\n";
    } else if (elem_type == "int" || elem_type == "float" || elem_type == "bool") {
        ss << indent << "        bool _aok;\n";
        ss << indent << "        " << result_var << "." << field_name << ".push_back(json::ext_" << elem_type << "(_aes, _aep, _aelen, _aok));\n";
    } else if (!elem_type.empty() && std::isupper(elem_type[0]) && DataTypeRegistry::instance().lookup(elem_type)) {
        // Nested data type array
        ss << indent << "        auto _ae_view = json::isolate(_aes, _aep, _aelen);\n";
        ss << indent << "        if (_ae_view.length() > 0) {\n";
        ss << indent << "            " << elem_type << " _ae{};\n";
        ss << indent << "            " << elem_type << "Meta _ae_meta{};\n";
        ss << indent << "            bool _ae_ok;\n";
        generate_object_fields_parse(ss, elem_type, "_ae", "_ae_meta", 
                                     "_ae_view.data()", "_ae_view.length()", "_ae_ok",
                                     indent + "            ");
        ss << indent << "            " << result_var << "." << field_name << ".push_back(_ae);\n";
        ss << indent << "        }\n";
    }
    
    ss << indent << "    });\n";
    ss << indent << "    " << meta_var << ".set(" << field_idx << ");\n";
    ss << indent << "}\n";
}

// Generate inline parsing code for a nested object field
static void generate_nested_field_parse(std::stringstream& ss,
                                         const std::string& nested_type,
                                         const std::string& field_name,
                                         uint32_t field_idx,
                                         const std::string& result_var,
                                         const std::string& meta_var,
                                         const std::string& src_var,
                                         const std::string& pos_var,
                                         const std::string& len_var,
                                         const std::string& indent) {
    ss << indent << "auto _nv = json::isolate(" << src_var << ", " << pos_var << ", " << len_var << ");\n";
    ss << indent << "if (_nv.length() > 0) {\n";
    ss << indent << "    bool _n_ok;\n";
    generate_object_fields_parse(ss, nested_type, 
                                 result_var + "." + field_name,
                                 meta_var + "." + field_name,
                                 "_nv.data()", "_nv.length()", "_n_ok",
                                 indent + "    ");
    ss << indent << "    " << meta_var << ".set(" << field_idx << ");\n";
    ss << indent << "}\n";
}

// Generate inline parsing code for all fields of a data type
static void generate_object_fields_parse(std::stringstream& ss,
                                          const std::string& data_type,
                                          const std::string& result_var,
                                          const std::string& meta_var,
                                          const std::string& src_var,
                                          const std::string& len_var,
                                          const std::string& ok_var,
                                          const std::string& indent) {
    auto* fields = DataTypeRegistry::instance().lookup(data_type);
    if (!fields) return;
    
    for (uint32_t i = 0; i < fields->size(); i++) {
        const auto& field = (*fields)[i];
        ss << indent << "if (uint32_t _fp = json::find_key(" << src_var << ", " << len_var << ", \"" 
           << field.name << "\", " << field.name.length() << ")) {\n";
        ss << indent << "    _fp = json::skip_ws(" << src_var << ", _fp, " << len_var << ");\n";
        
        if (is_array_type(field.type)) {
            generate_array_field_parse(ss, get_array_element_type(field.type), field.name, i,
                                       result_var, meta_var, src_var, "_fp", len_var, indent + "    ");
        } else if (!field.type.empty() && std::isupper(field.type[0]) && 
                   DataTypeRegistry::instance().lookup(field.type)) {
            generate_nested_field_parse(ss, field.type, field.name, i,
                                        result_var, meta_var, src_var, "_fp", len_var, indent + "    ");
        } else {
            generate_primitive_field_parse(ss, field.type, field.name, i,
                                           result_var, meta_var, src_var, "_fp", len_var, ok_var, indent + "    ");
=======
// Generate inline parsing code for a nested object
// Returns the code to parse nested_type from (_ns, 0, _nlen) into result_var and meta_var
static void generate_nested_parse(std::stringstream& ss, 
                                   const std::string& nested_type,
                                   const std::string& result_var,
                                   const std::string& meta_var,
                                   const std::string& src_var,
                                   const std::string& len_var,
                                   const std::string& indent) {
    auto* fields = DataTypeRegistry::instance().lookup(nested_type);
    if (!fields) return;
    
    ss << indent << "bool _nok;\n";
    
    for (uint32_t i = 0; i < fields->size(); i++) {
        const auto& field = (*fields)[i];
        ss << indent << "if (uint32_t _np = json::find_key(" << src_var << ", " << len_var << ", \"" 
           << field.name << "\", " << field.name.length() << ")) {\n";
        ss << indent << "    _np = json::skip_ws(" << src_var << ", _np, " << len_var << ");\n";
        
        if (!field.type.empty() && std::isupper(field.type[0]) && 
            DataTypeRegistry::instance().lookup(field.type)) {
            // Recursively nested type
            ss << indent << "    auto _deep_view = json::isolate(" << src_var << ", _np, " << len_var << ");\n";
            ss << indent << "    if (_deep_view.length() > 0) {\n";
            ss << indent << "        const char* _ds = _deep_view.data();\n";
            ss << indent << "        uint32_t _dlen = _deep_view.length();\n";
            generate_nested_parse(ss, field.type, 
                                  result_var + "." + field.name, 
                                  meta_var + "." + field.name,
                                  "_ds", "_dlen",
                                  indent + "        ");
            ss << indent << "        " << meta_var << ".set(" << i << ");\n";
            ss << indent << "    }\n";
        } else if (field.type == "string") {
            ss << indent << "    if (!json::is_null(" << src_var << ", _np, " << len_var << ")) {\n";
            ss << indent << "        " << result_var << "." << field.name << " = json::ext_str(" << src_var << ", _np, " << len_var << ");\n";
            ss << indent << "        " << meta_var << ".set(" << i << ");\n";
            ss << indent << "    }\n";
        } else if (field.type == "int") {
            ss << indent << "    if (!json::is_null(" << src_var << ", _np, " << len_var << ")) {\n";
            ss << indent << "        " << result_var << "." << field.name << " = json::ext_int(" << src_var << ", _np, " << len_var << ", _nok);\n";
            ss << indent << "        if (_nok) " << meta_var << ".set(" << i << ");\n";
            ss << indent << "    }\n";
        } else if (field.type == "float") {
            ss << indent << "    if (!json::is_null(" << src_var << ", _np, " << len_var << ")) {\n";
            ss << indent << "        " << result_var << "." << field.name << " = json::ext_float(" << src_var << ", _np, " << len_var << ", _nok);\n";
            ss << indent << "        if (_nok) " << meta_var << ".set(" << i << ");\n";
            ss << indent << "    }\n";
        } else if (field.type == "bool") {
            ss << indent << "    if (!json::is_null(" << src_var << ", _np, " << len_var << ")) {\n";
            ss << indent << "        " << result_var << "." << field.name << " = json::ext_bool(" << src_var << ", _np, " << len_var << ", _nok);\n";
            ss << indent << "        if (_nok) " << meta_var << ".set(" << i << ");\n";
            ss << indent << "    }\n";
>>>>>>> 688f98f (The big cleanup :) (#23))
        }
        
        ss << indent << "}\n";
    }
}

<<<<<<< HEAD
// Generate JSON parse code for root-level arrays (e.g., Json.parse(User[], ...))
static std::string generate_json_parse_array(
    const std::string& array_type,
    const std::string& json_expr,
    const std::string& on_success_callback,
    const std::string& on_error_callback)
{
    std::string elem_type = get_array_element_type(array_type);
    if (!DataTypeRegistry::instance().lookup(elem_type)) {
        return "/* Error: Unknown element type '" + elem_type + "' for Json.parse */";
    }
    
    std::stringstream ss;
    ss << "[&]() {\n";
    ss << "            const char* _s = " << json_expr << ".data();\n";
    ss << "            uint32_t _len = " << json_expr << ".length();\n";
    ss << "            uint32_t _p = json::skip_ws(_s, 0, _len);\n";
    ss << "            if (_p >= _len || _s[_p] != '[') {\n";
    if (!on_error_callback.empty()) {
        ss << "                this->" << on_error_callback << "();\n";
    }
    ss << "                return;\n";
    ss << "            }\n";
    ss << "            webcc::vector<" << elem_type << "> _results;\n";
    ss << "            webcc::vector<" << elem_type << "Meta> _metas;\n";
    ss << "            json::for_each(_s, _p, _len, [&](const char* _es, uint32_t _ep, uint32_t _elen) {\n";
    ss << "                auto _ev = json::isolate(_es, _ep, _elen);\n";
    ss << "                if (_ev.length() > 0) {\n";
    ss << "                    " << elem_type << " _elem{};\n";
    ss << "                    " << elem_type << "Meta _elem_meta{};\n";
    ss << "                    bool _ok;\n";
    generate_object_fields_parse(ss, elem_type, "_elem", "_elem_meta", "_ev.data()", "_ev.length()", "_ok", "                    ");
    ss << "                    _results.push_back(webcc::move(_elem));\n";
    ss << "                    _metas.push_back(webcc::move(_elem_meta));\n";
    ss << "                }\n";
    ss << "            });\n";
    if (!on_success_callback.empty()) {
        ss << "            this->" << on_success_callback << "(webcc::move(_results), webcc::move(_metas));\n";
    }
    ss << "        }()";
    return ss.str();
}

=======
>>>>>>> 688f98f (The big cleanup :) (#23))
std::string generate_json_parse(
    const std::string& data_type,
    const std::string& json_expr,
    const std::string& on_success_callback,
    const std::string& on_error_callback)
{
<<<<<<< HEAD
    // Check if this is an array type at the root level (e.g., "User[]")
    if (is_array_type(data_type)) {
        return generate_json_parse_array(data_type, json_expr, on_success_callback, on_error_callback);
    }
    
    if (!DataTypeRegistry::instance().lookup(data_type)) {
=======
    auto* fields = DataTypeRegistry::instance().lookup(data_type);
    if (!fields) {
>>>>>>> 688f98f (The big cleanup :) (#23))
        return "/* Error: Unknown data type '" + data_type + "' for Json.parse */";
    }
    
    std::stringstream ss;
<<<<<<< HEAD
    ss << "[&]() {\n";
    ss << "            const char* _s = " << json_expr << ".data();\n";
    ss << "            uint32_t _len = " << json_expr << ".length();\n";
    ss << "            if (!json::is_valid(_s, _len)) {\n";
    if (!on_error_callback.empty()) {
        ss << "                this->" << on_error_callback << "();\n";
    }
    ss << "                return;\n";
    ss << "            }\n";
    ss << "            " << data_type << " _result{};\n";
    ss << "            " << data_type << "Meta _meta{};\n";
    ss << "            bool _ok;\n";
    generate_object_fields_parse(ss, data_type, "_result", "_meta", "_s", "_len", "_ok", "            ");
    if (!on_success_callback.empty()) {
        ss << "            this->" << on_success_callback << "(webcc::move(_result), webcc::move(_meta));\n";
    }
    ss << "        }()";
=======
    
    // Generate inline lambda that does the parsing
    ss << "[&]() {\n";
    ss << "            const char* _s = " << json_expr << ".data();\n";
    ss << "            uint32_t _len = " << json_expr << ".length();\n";
    ss << "            \n";
    
    // Validate JSON structure
    ss << "            if (!json::is_valid(_s, _len)) {\n";
    if (!on_error_callback.empty()) {
        ss << "                this->" << on_error_callback << "(\"Invalid JSON structure\");\n";
    }
    ss << "                return;\n";
    ss << "            }\n";
    ss << "            \n";
    
    // Declare result and meta
    ss << "            " << data_type << " _result{};\n";
    ss << "            " << data_type << "Meta _meta{};\n";
    ss << "            bool _ok;\n";
    ss << "            \n";
    
    // Extract each field
    uint32_t field_idx = 0;
    for (const auto& field : *fields) {
        ss << "            // Field: " << field.name << " (" << field.type << ")\n";
        ss << "            if (uint32_t _p = json::find_key(_s, _len, \"" << field.name << "\", " 
           << field.name.length() << ")) {\n";
        ss << "                _p = json::skip_ws(_s, _p, _len);\n";
        
        if (is_array_type(field.type)) {
            // Array field
            std::string elem_type = get_array_element_type(field.type);
            ss << "                auto _arr_view = json::isolate(_s, _p, _len);\n";
            ss << "                if (_arr_view.length() > 0) {\n";
            ss << "                    json::for_each(_arr_view.data(), 0, _arr_view.length(), [&](const char* _es, uint32_t _ep, uint32_t _elen) {\n";
            
            if (elem_type == "string") {
                ss << "                        _result." << field.name << ".push_back(json::ext_str(_es, _ep, _elen));\n";
            } else if (elem_type == "int") {
                ss << "                        bool _eok;\n";
                ss << "                        _result." << field.name << ".push_back(json::ext_int(_es, _ep, _elen, _eok));\n";
            } else if (elem_type == "float") {
                ss << "                        bool _eok;\n";
                ss << "                        _result." << field.name << ".push_back(json::ext_float(_es, _ep, _elen, _eok));\n";
            } else if (elem_type == "bool") {
                ss << "                        bool _eok;\n";
                ss << "                        _result." << field.name << ".push_back(json::ext_bool(_es, _ep, _elen, _eok));\n";
            } else {
                // Nested data type array
                ss << "                        auto _elem_view = json::isolate(_es, _ep, _elen);\n";
                ss << "                        if (_elem_view.length() > 0) {\n";
                ss << "                            " << elem_type << " _elem{};\n";
                ss << "                            const char* _os = _elem_view.data();\n";
                ss << "                            uint32_t _olen = _elem_view.length();\n";
                
                // Generate inline parsing for each field of the nested type
                auto* elem_fields = DataTypeRegistry::instance().lookup(elem_type);
                if (elem_fields) {
                    ss << "                            bool _eok;\n";
                    for (uint32_t ei = 0; ei < elem_fields->size(); ei++) {
                        const auto& ef = (*elem_fields)[ei];
                        ss << "                            if (uint32_t _op = json::find_key(_os, _olen, \"" 
                           << ef.name << "\", " << ef.name.length() << ")) {\n";
                        ss << "                                _op = json::skip_ws(_os, _op, _olen);\n";
                        
                        if (ef.type == "string") {
                            ss << "                                if (!json::is_null(_os, _op, _olen)) {\n";
                            ss << "                                    _elem." << ef.name << " = json::ext_str(_os, _op, _olen);\n";
                            ss << "                                }\n";
                        } else if (ef.type == "int") {
                            ss << "                                if (!json::is_null(_os, _op, _olen)) {\n";
                            ss << "                                    _elem." << ef.name << " = json::ext_int(_os, _op, _olen, _eok);\n";
                            ss << "                                }\n";
                        } else if (ef.type == "float") {
                            ss << "                                if (!json::is_null(_os, _op, _olen)) {\n";
                            ss << "                                    _elem." << ef.name << " = json::ext_float(_os, _op, _olen, _eok);\n";
                            ss << "                                }\n";
                        } else if (ef.type == "bool") {
                            ss << "                                if (!json::is_null(_os, _op, _olen)) {\n";
                            ss << "                                    _elem." << ef.name << " = json::ext_bool(_os, _op, _olen, _eok);\n";
                            ss << "                                }\n";
                        } else if (!ef.type.empty() && std::isupper(ef.type[0]) && 
                                   DataTypeRegistry::instance().lookup(ef.type)) {
                            // Nested object within array element - use recursive parsing
                            ss << "                                auto _nested_view = json::isolate(_os, _op, _olen);\n";
                            ss << "                                if (_nested_view.length() > 0) {\n";
                            ss << "                                    const char* _ns = _nested_view.data();\n";
                            ss << "                                    uint32_t _nlen = _nested_view.length();\n";
                            ss << "                                    " << ef.type << "Meta _elem_nested_meta{};\n";
                            generate_nested_parse(ss, ef.type,
                                                  "_elem." + ef.name,
                                                  "_elem_nested_meta",
                                                  "_ns", "_nlen",
                                                  "                                    ");
                            ss << "                                }\n";
                        }
                        
                        ss << "                            }\n";
                    }
                }
                
                ss << "                            _result." << field.name << ".push_back(_elem);\n";
                ss << "                        }\n";
            }
            
            ss << "                    });\n";
            ss << "                    _meta.set(" << field_idx << ");\n";
            ss << "                }\n";
        } else if (!field.type.empty() && std::isupper(field.type[0]) && 
                   DataTypeRegistry::instance().lookup(field.type)) {
            // Nested data type
            ss << "                auto _nested_view = json::isolate(_s, _p, _len);\n";
            ss << "                if (_nested_view.length() > 0) {\n";
            ss << "                    const char* _ns = _nested_view.data();\n";
            ss << "                    uint32_t _nlen = _nested_view.length();\n";
            generate_nested_parse(ss, field.type, 
                                  "_result." + field.name, 
                                  "_meta." + field.name,
                                  "_ns", "_nlen",
                                  "                    ");
            ss << "                    _meta.set(" << field_idx << ");\n";
            ss << "                }\n";
        } else {
            // Primitive field
            ss << "                if (!json::is_null(_s, _p, _len)) {\n";
            
            if (field.type == "string") {
                ss << "                    _result." << field.name << " = json::ext_str(_s, _p, _len);\n";
                ss << "                    _meta.set(" << field_idx << ");\n";
            } else {
                ss << "                    _result." << field.name << " = json::ext_" 
                   << (field.type == "float" ? "float" : field.type) << "(_s, _p, _len, _ok);\n";
                ss << "                    if (_ok) _meta.set(" << field_idx << ");\n";
            }
            
            ss << "                }\n";
        }
        
        ss << "            }\n";
        ss << "            \n";
        field_idx++;
    }
    
    // Call success callback with result and meta
    if (!on_success_callback.empty()) {
        ss << "            this->" << on_success_callback << "(_result, _meta);\n";
    }
    
    ss << "        }()";
    
>>>>>>> 688f98f (The big cleanup :) (#23))
    return ss.str();
}

// ============================================================================
// Emit JSON Runtime Helpers (inline into generated code)
// ============================================================================

void emit_json_runtime(std::ostream& out) {
    out << R"(
// ============================================================================
// JSON Runtime Helpers (auto-generated by Coi compiler)
// ============================================================================
namespace json {

struct MetaBase {
    uint32_t bits = 0;
    bool has(uint32_t i) const { return (bits >> i) & 1; }
    void set(uint32_t i) { bits |= (1u << i); }
};

inline uint32_t skip_ws(const char* s, uint32_t p, uint32_t len) {
    while (p < len && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
    return p;
}

inline uint32_t find_key(const char* s, uint32_t len, const char* key, uint32_t klen) {
    int depth = 0;
    uint32_t p = skip_ws(s, 0, len);
    if (p >= len || s[p] != '{') return 0;
    p++;
    while (p < len) {
        p = skip_ws(s, p, len);
        if (p >= len) return 0;
        char c = s[p];
        if (c == '{' || c == '[') { depth++; p++; continue; }
        if (c == '}' || c == ']') { if (depth == 0) return 0; depth--; p++; continue; }
        if (depth > 0) {
            if (c == '"') { p++; while (p < len && s[p] != '"') { if (s[p] == '\\') p++; p++; } p++; }
            else p++;
            continue;
        }
        if (c == '"') {
            uint32_t ks = p + 1; p++;
            while (p < len && s[p] != '"') { if (s[p] == '\\') p++; p++; }
            uint32_t ke = p; p++;
            if (ke - ks == klen) {
                bool match = true;
                for (uint32_t i = 0; i < klen && match; i++) if (s[ks + i] != key[i]) match = false;
                if (match) { p = skip_ws(s, p, len); if (p < len && s[p] == ':') return skip_ws(s, p + 1, len); }
            }
            continue;
        }
        p++;
    }
    return 0;
}

inline webcc::string_view isolate(const char* s, uint32_t p, uint32_t len) {
    if (p >= len) return {};
    char open = s[p];
    if (open != '{' && open != '[') return {};
    char close = (open == '{') ? '}' : ']';
    uint32_t start = p;
    int depth = 1; p++;
    while (p < len && depth > 0) {
        char c = s[p];
        if (c == '"') { p++; while (p < len && s[p] != '"') { if (s[p] == '\\') p++; p++; } p++; }
        else { if (c == open) depth++; else if (c == close) depth--; p++; }
    }
    return depth == 0 ? webcc::string_view(s + start, p - start) : webcc::string_view();
}

inline webcc::string ext_str(const char* s, uint32_t p, uint32_t len) {
    if (p >= len || s[p] != '"') return {};
    p++;
    webcc::string r;
    while (p < len && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < len) {
            p++;
            switch (s[p]) {
                case '"': r += '"'; break; case '\\': r += '\\'; break;
                case 'n': r += '\n'; break; case 'r': r += '\r'; break;
                case 't': r += '\t'; break; default: r += s[p]; break;
            }
        } else r += s[p];
        p++;
    }
    return r;
}

inline int32_t ext_int(const char* s, uint32_t p, uint32_t len, bool& ok) {
    ok = false;
    if (p >= len) return 0;
    bool neg = s[p] == '-'; if (neg) p++;
    if (p >= len || s[p] < '0' || s[p] > '9') return 0;
    int32_t r = 0;
    while (p < len && s[p] >= '0' && s[p] <= '9') { r = r * 10 + (s[p] - '0'); p++; }
    ok = true;
    return neg ? -r : r;
}

inline double ext_float(const char* s, uint32_t p, uint32_t len, bool& ok) {
    ok = false;
    if (p >= len) return 0;
    bool neg = s[p] == '-'; if (neg) p++;
    if (p >= len || s[p] < '0' || s[p] > '9') return 0;
    double r = 0;
    while (p < len && s[p] >= '0' && s[p] <= '9') { r = r * 10 + (s[p] - '0'); p++; }
    if (p < len && s[p] == '.') { p++; double d = 10; while (p < len && s[p] >= '0' && s[p] <= '9') { r += (s[p] - '0') / d; d *= 10; p++; } }
    ok = true;
    return neg ? -r : r;
}

inline bool ext_bool(const char* s, uint32_t p, uint32_t len, bool& ok) {
    ok = false;
    if (p + 4 <= len && s[p] == 't' && s[p+1] == 'r' && s[p+2] == 'u' && s[p+3] == 'e') { ok = true; return true; }
    if (p + 5 <= len && s[p] == 'f' && s[p+1] == 'a' && s[p+2] == 'l' && s[p+3] == 's' && s[p+4] == 'e') { ok = true; return false; }
    return false;
}

inline bool is_null(const char* s, uint32_t p, uint32_t len) {
    return p + 4 <= len && s[p] == 'n' && s[p+1] == 'u' && s[p+2] == 'l' && s[p+3] == 'l';
}

inline bool is_valid(const char* s, uint32_t len) {
    uint32_t p = skip_ws(s, 0, len);
    if (p >= len || s[p] != '{') return false;
    int d = 0; bool in_str = false;
    for (uint32_t i = p; i < len; i++) {
        char c = s[i];
        if (in_str) { if (c == '\\' && i + 1 < len) { i++; continue; } if (c == '"') in_str = false; }
        else { if (c == '"') in_str = true; else if (c == '{' || c == '[') d++; else if (c == '}' || c == ']') d--; }
    }
    return d == 0 && !in_str;
}

template<typename F>
inline void for_each(const char* s, uint32_t p, uint32_t len, F fn) {
    p = skip_ws(s, p, len);
    if (p >= len || s[p] != '[') return;
    p++; p = skip_ws(s, p, len);
    while (p < len && s[p] != ']') {
        fn(s, p, len);
        char c = s[p];
        if (c == '{' || c == '[') { auto v = isolate(s, p, len); p += v.length(); }
        else if (c == '"') { p++; while (p < len && s[p] != '"') { if (s[p] == '\\') p++; p++; } p++; }
        else { while (p < len && s[p] != ',' && s[p] != ']') p++; }
        p = skip_ws(s, p, len);
        if (p < len && s[p] == ',') { p++; p = skip_ws(s, p, len); }
    }
}

} // namespace json

)";
}
