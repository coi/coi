// =============================================================================
// JSON Code Generation for Coi - Implementation
// =============================================================================

#include "json_codegen.h"
#include <cstdint>
#include <sstream>
#include <set>
#include <cctype>

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
    ss << "struct " << data_type << "Meta : __coi_json::MetaBase {\n";
    
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

// how a JSON value is read into a field of this Coi type
static std::string scalar_kind(const std::string& type) {
    if (type == "string" || type == "bool") return type;
    if (type.rfind("float", 0) == 0) return "float";
    return "int";  // int*, uint*, enums
}

static std::string extract_scalar(const std::string& kind, const std::string& target_type,
                                  const std::string& s, const std::string& p, const std::string& len, const std::string& ok) {
    if (kind == "string") return "__coi_json::ext_str(" + s + ", " + p + ", " + len + ")";
    if (kind == "bool") return "__coi_json::ext_bool(" + s + ", " + p + ", " + len + ", " + ok + ")";
    return "(" + target_type + ")__coi_json::ext_" + kind + "(" + s + ", " + p + ", " + len + ", " + ok + ")";
}

// Check if type is an array
static bool is_array_type(const std::string& type) {
    return type.size() > 2 && type.substr(type.size() - 2) == "[]";
}

// Get element type from array type (e.g., "User[]" -> "User")
static std::string get_array_element_type(const std::string& type) {
    return type.substr(0, type.size() - 2);
}

// Convert type names like App_User[] to valid C++ identifier suffixes.
static std::string sanitize_type_for_symbol(const std::string& type) {
    std::string symbol;
    symbol.reserve(type.size() + 8);
    for (char c : type) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            symbol += c;
        } else {
            symbol += '_';
        }
    }
    return symbol;
}

std::string field_token_symbol_name(const std::string& data_type, const std::string& field_name) {
    return "__coi_field_" + sanitize_type_for_symbol(data_type) + "_" + field_name;
}

std::string generate_field_token_constants(const std::string& data_type) {
    auto* fields = DataTypeRegistry::instance().lookup(data_type);
    if (!fields) return "";

    std::stringstream ss;
    for (uint32_t i = 0; i < fields->size(); i++) {
        ss << "static constexpr uint32_t "
           << field_token_symbol_name(data_type, (*fields)[i].name)
           << " = " << i << ";\n";
    }
    return ss.str();
}

// Forward declaration. `depth` suffixes the emitted locals (_nv0, _fp1, ...) so
// nested objects don't reuse an enclosing scope's names. Without it, three-deep
// nesting emits `auto _nv = isolate(_nv.data(), ...)`, which won't compile.
static void generate_object_fields_parse(std::stringstream& ss,
                                          const std::string& data_type,
                                          const std::string& result_var,
                                          const std::string& meta_var,
                                          const std::string& src_var,
                                          const std::string& len_var,
                                          const std::string& ok_var,
                                          const std::string& indent,
                                          int depth);

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
    ss << indent << "if (!__coi_json::is_null(" << src_var << ", " << pos_var << ", " << len_var << ")) {\n";
    std::string kind = scalar_kind(field_type);
    std::string target = result_var + "." + field_name;
    ss << indent << "    " << target << " = " << extract_scalar(kind, "decltype(" + target + ")", src_var, pos_var, len_var, ok_var) << ";\n";
    if (kind == "string")
        ss << indent << "    " << meta_var << ".set(" << field_idx << ");\n";
    else
        ss << indent << "    if (" << ok_var << ") " << meta_var << ".set(" << field_idx << ");\n";
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
                                        const std::string& indent,
                                        int depth) {
    std::string d = std::to_string(depth);
    std::string arr_view = "_arr_view" + d;
    std::string aes = "_aes" + d, aep = "_aep" + d, aelen = "_aelen" + d;
    std::string aok = "_aok" + d;
    std::string ae_view = "_ae_view" + d, ae = "_ae" + d, ae_meta = "_ae_meta" + d, ae_ok = "_ae_ok" + d;

    ss << indent << "auto " << arr_view << " = __coi_json::isolate(" << src_var << ", " << pos_var << ", " << len_var << ");\n";
    ss << indent << "if (" << arr_view << ".length() > 0) {\n";
    ss << indent << "    __coi_json::for_each(" << arr_view << ".data(), 0, " << arr_view << ".length(), [&](const char* " << aes << ", uint32_t " << aep << ", uint32_t " << aelen << ") {\n";

    if (!(!elem_type.empty() && std::isupper(elem_type[0]) && DataTypeRegistry::instance().lookup(elem_type))) {
        std::string target = result_var + "." + field_name;
        ss << indent << "        bool " << aok << ";\n";
        ss << indent << "        " << target << ".push_back("
           << extract_scalar(scalar_kind(elem_type), "decltype(" + target + ")::value_type", aes, aep, aelen, aok) << ");\n";
        ss << indent << "        (void)" << aok << ";\n";
    } else {
        // Nested data type array
        ss << indent << "        auto " << ae_view << " = __coi_json::isolate(" << aes << ", " << aep << ", " << aelen << ");\n";
        ss << indent << "        if (" << ae_view << ".length() > 0) {\n";
        ss << indent << "            " << elem_type << " " << ae << "{};\n";
        ss << indent << "            " << elem_type << "Meta " << ae_meta << "{};\n";
        ss << indent << "            bool " << ae_ok << ";\n";
        generate_object_fields_parse(ss, elem_type, ae, ae_meta,
                                     ae_view + ".data()", ae_view + ".length()", ae_ok,
                                     indent + "            ", depth + 1);
        ss << indent << "            " << result_var << "." << field_name << ".push_back(" << ae << ");\n";
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
                                         const std::string& indent,
                                         int depth) {
    std::string d = std::to_string(depth);
    std::string nv = "_nv" + d;
    std::string n_ok = "_n_ok" + d;

    ss << indent << "auto " << nv << " = __coi_json::isolate(" << src_var << ", " << pos_var << ", " << len_var << ");\n";
    ss << indent << "if (" << nv << ".length() > 0) {\n";
    ss << indent << "    bool " << n_ok << ";\n";
    generate_object_fields_parse(ss, nested_type,
                                 result_var + "." + field_name,
                                 meta_var + "." + field_name,
                                 nv + ".data()", nv + ".length()", n_ok,
                                 indent + "    ", depth + 1);
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
                                          const std::string& indent,
                                          int depth) {
    auto* fields = DataTypeRegistry::instance().lookup(data_type);
    if (!fields) return;

    std::string fp = "_fp" + std::to_string(depth);

    for (uint32_t i = 0; i < fields->size(); i++) {
        const auto& field = (*fields)[i];
        ss << indent << "if (uint32_t " << fp << " = __coi_json::find_key(" << src_var << ", " << len_var << ", \""
           << field.name << "\", " << field.name.length() << ")) {\n";
        ss << indent << "    " << fp << " = __coi_json::skip_ws(" << src_var << ", " << fp << ", " << len_var << ");\n";

        if (is_array_type(field.type)) {
            generate_array_field_parse(ss, get_array_element_type(field.type), field.name, i,
                                       result_var, meta_var, src_var, fp, len_var, indent + "    ", depth);
        } else if (!field.type.empty() && std::isupper(field.type[0]) &&
                   DataTypeRegistry::instance().lookup(field.type)) {
            generate_nested_field_parse(ss, field.type, field.name, i,
                                        result_var, meta_var, src_var, fp, len_var, indent + "    ", depth);
        } else {
            generate_primitive_field_parse(ss, field.type, field.name, i,
                                           result_var, meta_var, src_var, fp, len_var, ok_var, indent + "    ");
        }

        ss << indent << "}\n";
    }
}

// Generate JSON parse code for root-level arrays (e.g., Json.parse(User[], ...))
static std::string generate_json_parse_array(
    const std::string& array_type,
    const std::string& json_expr)
{
    std::string elem_type = get_array_element_type(array_type);
    if (!DataTypeRegistry::instance().lookup(elem_type)) {
        // Json.parse(string[], json): a list of scalars, the meta has nothing to say
        static const std::set<std::string> scalars = {"string", "bool", "int", "int8", "int16", "int32", "int64",
                                                       "uint8", "uint16", "uint32", "uint64", "float", "float32", "float64"};
        if (!scalars.count(elem_type))
            return "/* Error: Unknown element type '" + elem_type + "' for Json.parse */";
        std::string cpp = convert_type(elem_type);
        std::stringstream ss;
        ss << "[&]() {\n";
        ss << "            const coi::string& _json_str = (" << json_expr << ");\n"
           << "            coi::string_view _json = _json_str;\n";
        ss << "            const char* _s = _json.data();\n";
        ss << "            uint32_t _len = _json.length();\n";
        ss << "            struct __JsonParseResult {\n";
        ss << "                struct __SuccessPayload { coi::vector<" << cpp << "> _0; __coi_json::MetaBase _1; };\n";
        ss << "                struct __ErrorPayload { coi::string _0; };\n";
        ss << "                bool ok;\n";
        ss << "                coi::vector<" << cpp << "> value;\n";
        ss << "                __coi_json::MetaBase meta;\n";
        ss << "                coi::string error;\n";
        ss << "                __SuccessPayload success;\n";
        ss << "                __ErrorPayload error_payload;\n";
        ss << "                bool is_Success() const { return ok; }\n";
        ss << "                bool is_Error() const { return !ok; }\n";
        ss << "                const __SuccessPayload& as_Success() const { return success; }\n";
        ss << "                const __ErrorPayload& as_Error() const { return error_payload; }\n";
        ss << "            } _r{};\n";
        ss << "            uint32_t _p = __coi_json::skip_ws(_s, 0, _len);\n";
        ss << "            if (_p >= _len || _s[_p] != '[') {\n";
        ss << "                _r.ok = false;\n";
        ss << "                _r.error = \"Expected JSON array\";\n";
        ss << "                _r.error_payload._0 = _r.error;\n";
        ss << "                return _r;\n";
        ss << "            }\n";
        ss << "            _r.ok = true;\n";
        ss << "            __coi_json::for_each(_s, _p, _len, [&](const char* _es, uint32_t _ep, uint32_t _elen) {\n";
        ss << "                bool _ok;\n";
        ss << "                _r.value.push_back(" << extract_scalar(scalar_kind(elem_type), cpp, "_es", "_ep", "_elen", "_ok") << ");\n";
        ss << "                (void)_ok;\n";
        ss << "            });\n";
        ss << "            _r.success._0 = _r.value;\n";
        ss << "            return _r;\n";
        ss << "        }()";
        return ss.str();
    }
    
    std::stringstream ss;
    ss << "[&]() {\n";
    ss << "            const coi::string& _json_str = (" << json_expr << ");\n"
       << "            coi::string_view _json = _json_str;\n";
    ss << "            const char* _s = _json.data();\n";
    ss << "            uint32_t _len = _json.length();\n";
    ss << "            struct __JsonParseResult {\n";
    ss << "                struct __SuccessPayload {\n";
    ss << "                    coi::vector<" << elem_type << "> _0;\n";
    ss << "                    coi::vector<" << elem_type << "Meta> _1;\n";
    ss << "                };\n";
    ss << "                struct __ErrorPayload {\n";
    ss << "                    coi::string _0;\n";
    ss << "                };\n";
    ss << "                bool ok;\n";
    ss << "                coi::vector<" << elem_type << "> value;\n";
    ss << "                coi::vector<" << elem_type << "Meta> meta;\n";
    ss << "                coi::string error;\n";
    ss << "                __SuccessPayload success;\n";
    ss << "                __ErrorPayload error_payload;\n";
    ss << "                bool is_Success() const { return ok; }\n";
    ss << "                bool is_Error() const { return !ok; }\n";
    ss << "                const __SuccessPayload& as_Success() const { return success; }\n";
    ss << "                const __ErrorPayload& as_Error() const { return error_payload; }\n";
    ss << "            } _r{};\n";
    ss << "            uint32_t _p = __coi_json::skip_ws(_s, 0, _len);\n";
    ss << "            if (_p >= _len || _s[_p] != '[') {\n";
    ss << "                _r.ok = false;\n";
    ss << "                _r.error = \"Expected JSON array\";\n";
    ss << "                _r.error_payload._0 = _r.error;\n";
    ss << "                return _r;\n";
    ss << "            }\n";
    ss << "            _r.ok = true;\n";
    ss << "            __coi_json::for_each(_s, _p, _len, [&](const char* _es, uint32_t _ep, uint32_t _elen) {\n";
    ss << "                auto _ev = __coi_json::isolate(_es, _ep, _elen);\n";
    ss << "                if (_ev.length() > 0) {\n";
    ss << "                    " << elem_type << " _elem{};\n";
    ss << "                    " << elem_type << "Meta _elem_meta{};\n";
    ss << "                    bool _ok;\n";
    generate_object_fields_parse(ss, elem_type, "_elem", "_elem_meta", "_ev.data()", "_ev.length()", "_ok", "                    ", 0);
    ss << "                    _r.value.push_back(coi::move(_elem));\n";
    ss << "                    _r.meta.push_back(coi::move(_elem_meta));\n";
    ss << "                }\n";
    ss << "            });\n";
    ss << "            _r.success._0 = _r.value;\n";
    ss << "            _r.success._1 = _r.meta;\n";
    ss << "            return _r;\n";
    ss << "        }()";
    return ss.str();
}

std::string generate_json_parse(
    const std::string& data_type,
    const std::string& json_expr)
{
    // Check if this is an array type at the root level (e.g., "User[]")
    if (is_array_type(data_type)) {
        return generate_json_parse_array(data_type, json_expr);
    }
    
    if (!DataTypeRegistry::instance().lookup(data_type)) {
        return "/* Error: Unknown data type '" + data_type + "' for Json.parse */";
    }
    
    std::stringstream ss;
    ss << "[&]() {\n";
    ss << "            const coi::string& _json_str = (" << json_expr << ");\n"
       << "            coi::string_view _json = _json_str;\n";
    ss << "            const char* _s = _json.data();\n";
    ss << "            uint32_t _len = _json.length();\n";
    ss << "            struct __JsonParseResult {\n";
    ss << "                struct __SuccessPayload {\n";
    ss << "                    " << data_type << " _0;\n";
    ss << "                    " << data_type << "Meta _1;\n";
    ss << "                };\n";
    ss << "                struct __ErrorPayload {\n";
    ss << "                    coi::string _0;\n";
    ss << "                };\n";
    ss << "                bool ok;\n";
    ss << "                " << data_type << " value;\n";
    ss << "                " << data_type << "Meta meta;\n";
    ss << "                coi::string error;\n";
    ss << "                __SuccessPayload success;\n";
    ss << "                __ErrorPayload error_payload;\n";
    ss << "                bool is_Success() const { return ok; }\n";
    ss << "                bool is_Error() const { return !ok; }\n";
    ss << "                const __SuccessPayload& as_Success() const { return success; }\n";
    ss << "                const __ErrorPayload& as_Error() const { return error_payload; }\n";
    ss << "            } _r{};\n";
    ss << "            if (!__coi_json::is_valid(_s, _len)) {\n";
    ss << "                _r.ok = false;\n";
    ss << "                _r.error = \"Invalid JSON\";\n";
    ss << "                _r.error_payload._0 = _r.error;\n";
    ss << "                return _r;\n";
    ss << "            }\n";
    ss << "            _r.ok = true;\n";
    ss << "            bool _ok;\n";
    generate_object_fields_parse(ss, data_type, "_r.value", "_r.meta", "_s", "_len", "_ok", "            ", 0);
    ss << "            _r.success._0 = _r.value;\n";
    ss << "            _r.success._1 = _r.meta;\n";
    ss << "            return _r;\n";
    ss << "        }()";
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
namespace __coi_json {

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

inline coi::string_view isolate(const char* s, uint32_t p, uint32_t len) {
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
    return depth == 0 ? coi::string_view(s + start, p - start) : coi::string_view();
}

inline uint32_t hex4(const char* s, uint32_t p, uint32_t len) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) {
        if (p + i >= len) return 0xFFFFFFFF;
        char c = s[p + i];
        uint32_t d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
        if (d == 16) return 0xFFFFFFFF;
        v = v * 16 + d;
    }
    return v;
}

inline void put_utf8(coi::vector<char>& out, uint32_t cp) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
}

inline coi::string ext_str(const char* s, uint32_t p, uint32_t len) {
    if (p >= len || s[p] != '"') return {};
    p++;
    coi::vector<char> r;
    while (p < len && s[p] != '"') {
        if (s[p] != '\\' || p + 1 >= len) { r.push_back(s[p++]); continue; }
        p++;
        switch (s[p]) {
            case 'n': r.push_back('\n'); break;
            case 'r': r.push_back('\r'); break;
            case 't': r.push_back('\t'); break;
            case 'b': r.push_back('\b'); break;
            case 'f': r.push_back('\f'); break;
            case 'u': {
                uint32_t cp = hex4(s, p + 1, len);
                if (cp == 0xFFFFFFFF) { r.push_back('u'); break; }
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && p + 6 < len && s[p + 1] == '\\' && s[p + 2] == 'u') {
                    uint32_t lo = hex4(s, p + 3, len);
                    if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                }
                put_utf8(r, cp);
                break;
            }
            default: r.push_back(s[p]); break;
        }
        p++;
    }
    return coi::string(r.data(), (uint32_t)r.size());
}

inline int64_t ext_int(const char* s, uint32_t p, uint32_t len, bool& ok) {
    ok = false;
    uint32_t start = p;
    if (p < len && s[p] == '-') p++;
    if (p >= len || s[p] < '0' || s[p] > '9') return 0;
    uint64_t r = 0;
    while (p < len && s[p] >= '0' && s[p] <= '9') { r = r * 10 + (uint64_t)(s[p] - '0'); p++; }
    ok = true;
    if (p < len && (s[p] == '.' || s[p] == 'e' || s[p] == 'E')) {
        uint32_t used;
        return (int64_t)webcc::parse_double(s + start, len - start, used);
    }
    return s[start] == '-' ? -(int64_t)r : (int64_t)r;
}

inline double ext_float(const char* s, uint32_t p, uint32_t len, bool& ok) {
    uint32_t used = 0;
    double v = p < len ? webcc::parse_double(s + p, len - p, used) : 0;
    ok = used > 0;
    return v;
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

} // namespace __coi_json

)";
}

void emit_json_writer_runtime(std::ostream& out) {
    out << R"(
struct __coi_json_writer {
    coi::vector<char> buf;
    void put(char c) { buf.push_back(c); }
    void put(const char* s) { while (*s) buf.push_back(*s++); }
};
inline void __coi_json_write(__coi_json_writer& w, bool v) { w.put(v ? "true" : "false"); }
inline void __coi_json_write(__coi_json_writer& w, double v) {
    if (webcc::isnan(v) || webcc::isinf(v)) { w.put("null"); return; }
    char b[32]; webcc::format_double(v, b); w.put(b);
}
inline void __coi_json_write(__coi_json_writer& w, float v) {
    if (webcc::isnan(v) || webcc::isinf(v)) { w.put("null"); return; }
    char b[32]; webcc::format_float(v, b); w.put(b);
}
inline void __coi_json_write(__coi_json_writer& w, long long v) {
    char b[24]; uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
    int n = 0; do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) w.put('-');
    while (n) w.put(b[--n]);
}
inline void __coi_json_write(__coi_json_writer& w, unsigned long long v) {
    char b[24]; int n = 0; do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) w.put(b[--n]);
}
inline void __coi_json_write(__coi_json_writer& w, int v) { __coi_json_write(w, (long long)v); }
inline void __coi_json_write(__coi_json_writer& w, long v) { __coi_json_write(w, (long long)v); }
inline void __coi_json_write(__coi_json_writer& w, short v) { __coi_json_write(w, (long long)v); }
inline void __coi_json_write(__coi_json_writer& w, signed char v) { __coi_json_write(w, (long long)v); }
inline void __coi_json_write(__coi_json_writer& w, unsigned int v) { __coi_json_write(w, (unsigned long long)v); }
inline void __coi_json_write(__coi_json_writer& w, unsigned long v) { __coi_json_write(w, (unsigned long long)v); }
inline void __coi_json_write(__coi_json_writer& w, unsigned short v) { __coi_json_write(w, (unsigned long long)v); }
inline void __coi_json_write(__coi_json_writer& w, unsigned char v) { __coi_json_write(w, (unsigned long long)v); }
inline void __coi_json_write(__coi_json_writer& w, const coi::string& v) {
    static const char hex[] = "0123456789abcdef";
    w.put('"');
    for (uint32_t i = 0; i < v.length(); i++) {
        unsigned char c = (unsigned char)v.data()[i];
        if (c == '"') w.put("\\\"");
        else if (c == '\\') w.put("\\\\");
        else if (c == '\n') w.put("\\n");
        else if (c == '\r') w.put("\\r");
        else if (c == '\t') w.put("\\t");
        else if (c < 0x20) { w.put("\\u00"); w.put(hex[c >> 4]); w.put(hex[c & 15]); }
        else w.put((char)c);
    }
    w.put('"');
}
// templates on the writer, only compiled when used
template<typename W, typename T> inline void __coi_json_write(W& w, const T& v) { __coi_json_write(w, (int)v); }
template<typename W, typename T> inline void __coi_json_write(W& w, const coi::vector<T>& v) {
    w.put('[');
    for (uint32_t i = 0; i < v.size(); i++) { if (i) w.put(','); __coi_json_write(w, v[i]); }
    w.put(']');
}
template<typename W, typename T, size_t N> inline void __coi_json_write(W& w, const coi::array<T, N>& v) {
    w.put('[');
    for (size_t i = 0; i < N; i++) { if (i) w.put(','); __coi_json_write(w, v[i]); }
    w.put(']');
}
// object keys are strings
inline void __coi_json_key(__coi_json_writer& w, const coi::string& k) { __coi_json_write(w, k); }
template<typename W, typename K> inline void __coi_json_key(W& w, const K& k) { w.put('"'); __coi_json_write(w, k); w.put('"'); }
template<typename W, typename K, typename V> inline void __coi_json_write(W& w, const coi::map<K, V>& m) {
    w.put('{');
    bool first = true;
    for (const auto& k : m) {
        if (!first) w.put(',');
        first = false;
        __coi_json_key(w, k);
        w.put(':');
        __coi_json_write(w, m[k]);
    }
    w.put('}');
}
)";
}

std::string generate_json_writers(const std::vector<JsonPod>& pods) {
    std::stringstream ss;
    auto signature = [](const JsonPod& pod) {
        std::string tmpl = "template<typename W", args;
        for (size_t i = 0; i < pod.type_params.size(); i++) {
            tmpl += ", typename " + pod.type_params[i];
            args += (i ? ", " : "<") + pod.type_params[i];
        }
        if (!args.empty()) args += ">";
        return tmpl + "> inline void __coi_json_write(W& w, const " + pod.name + args + "& v)";
    };
    for (const auto& pod : pods)
        ss << signature(pod) << ";\n";
    for (const auto& pod : pods) {
        ss << signature(pod) << " {\n";
        for (size_t i = 0; i < pod.fields.size(); i++) {
            const auto& f = pod.fields[i];
            ss << "    w.put(\"" << (i ? "," : "{") << "\\\"" << f.name << "\\\":\"); __coi_json_write(w, v." << f.name << ");\n";
        }
        ss << "    w.put(\"" << (pod.fields.empty() ? "{}" : "}") << "\");\n";
        ss << "}\n";
    }
    ss << "template<typename T> inline coi::string __coi_json_stringify(const T& v) {\n";
    ss << "    __coi_json_writer w; __coi_json_write(w, v);\n";
    ss << "    return coi::string(w.buf.data(), (uint32_t)w.buf.size());\n";
    ss << "}\n";
    return ss.str();
}
