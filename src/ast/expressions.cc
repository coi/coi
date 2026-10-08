#include "expressions.h"
#include "codegen_state.h"
#include "formatter.h"
#include "node.h" 
#include "../defs/def_parser.h"
#include "../codegen/json_codegen.h"
#include "../cli/error.h"
#include <cctype>

// Helper to expand @inline templates like "${this}.length()" or "$self.is_valid()" or "${0}"
static std::string expand_inline_template(const std::string& tmpl, const std::string& receiver,
                                          const std::vector<CallArg>& args) {
    std::string result;
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '$' && i + 1 < tmpl.size()) {
            // Check for $self (simple identifier form)
            if (tmpl.substr(i, 5) == "$self") {
                result += receiver;
                i += 4;  // Skip "self" (the loop will handle $ and move to next char)
                continue;
            }
            // Check for ${...} (brace form)
            if (tmpl[i + 1] == '{') {
                size_t end = tmpl.find('}', i + 2);
                if (end != std::string::npos) {
                    std::string var = tmpl.substr(i + 2, end - i - 2);
                    if (var == "this") {
                        result += receiver;
                    } else {
                        // Numeric index like ${0}, ${1}
                        int idx = std::stoi(var);
                        if (idx >= 0 && idx < (int)args.size()) {
                            result += args[idx].value->to_webcc();
                        }
                    }
                    i = end;
                    continue;
                }
            }
        }
        result += tmpl[i];
    }
    return result;
}

static std::string to_webcc_arg(const std::string& code, const std::string& coi_type);
static std::string from_webcc_value(const std::string& code, const std::string& coi_type);


// Helper to generate intrinsic code
static std::string generate_intrinsic(const std::string& intrinsic_name,
                                      const std::vector<CallArg>& args) {                           
    if (intrinsic_name == "random") {
        return "webcc::random()";
    }
    if (intrinsic_name == "random_seeded" && args.size() == 1) {
        return "(webcc::random_seed(" + args[0].value->to_webcc() + "), webcc::random())";
    }
    if (intrinsic_name == "key_down" && args.size() == 1) {
        return "g_key_state[" + args[0].value->to_webcc() + "]";
    }
    if (intrinsic_name == "key_up" && args.size() == 1) {
        return "!g_key_state[" + args[0].value->to_webcc() + "]";
    }
    
    // Router navigation intrinsics
    if (intrinsic_name == "navigate" && args.size() == 1) {
        return "g_app_navigate(" + args[0].value->to_webcc() + ")";
    }
    if (intrinsic_name == "get_route" && args.empty()) {
        return "g_app_get_route()";
    }
    if (intrinsic_name == "is_hidden" && args.empty()) {
        return "(webcc::system::is_hidden() != 0)";
    }
    
    if (intrinsic_name == "json_stringify" && args.size() == 1) {
        std::string value = args[0].value->to_webcc();
        auto* arr = dynamic_cast<ArrayLiteral*>(args[0].value.get());
        if (arr && !arr->element_type.empty())
            value = "coi::vector<" + convert_type(arr->element_type) + ">" + value;
        return "__coi_json_stringify(" + value + ")";
    }

    if (intrinsic_name == "json_parse") {
        if (args.size() != 2) {
            ErrorHandler::compiler_error(
                "Json.parse now takes exactly 2 arguments: Json.parse(Type, json). "
                "Callback arguments (&onSuccess/&onError) were removed.");
        }

        for (const auto& arg : args) {
            if (!arg.name.empty()) {
                ErrorHandler::compiler_error(
                    "Json.parse does not support named arguments. Use: Json.parse(Type, json)");
            }
            if (arg.is_reference) {
                ErrorHandler::compiler_error(
                    "Json.parse callback/reference arguments are not supported. "
                    "Use match(Json.parse(...)) with Success(...) / Error(...).");
            }
        }
        
        // First arg is data type identifier (e.g., "User" or "User[]")
        // Resolve component-local types (e.g., "TestStruct" -> "App_TestStruct")
        std::string data_type = args[0].value->to_webcc();
        
        // Handle array types: resolve the element type, then add [] back
        bool is_array = data_type.size() > 2 && data_type.substr(data_type.size() - 2) == "[]";
        if (is_array) {
            std::string elem_type = data_type.substr(0, data_type.size() - 2);
            elem_type = ComponentTypeContext::instance().resolve(elem_type);
            data_type = elem_type + "[]";
        } else {
            data_type = ComponentTypeContext::instance().resolve(data_type);
        }
        
        // Second arg is JSON string expression
        std::string json_expr = args[1].value->to_webcc();
        return generate_json_parse(data_type, json_expr);
    }
    
    return "";  // Unknown intrinsic
}

std::string IntLiteral::to_webcc() {
    if (value >= INT32_MIN && value <= INT32_MAX) return std::to_string(value);
    return std::to_string(value) + "LL";
}

std::string FloatLiteral::to_webcc() {
    std::string s = std::to_string(value);
    if(s.find('.') != std::string::npos){
        s = s.substr(0, s.find_last_not_of('0')+1);
        if(s.back() == '.') s += "0";
    }
    return s;  // No 'f' suffix - using double (64-bit)
}

static std::string escape_cpp(const std::string& text) {
    std::string escaped;
    for (char c : text) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\t') escaped += "\\t";
        else escaped += c;
    }
    return escaped;
}

// end of a quoted string or nested template starting at i
static size_t skip_quoted(const std::string& v, size_t i) {
    char q = v[i++];
    while (i < v.size() && v[i] != q) {
        if (v[i] == '\\') i++;
        else if (q == '`' && v[i] == '$' && i + 1 < v.size() && v[i + 1] == '{') {
            int depth = 0;
            for (i += 1; i < v.size(); i++) {
                if (v[i] == '"' || v[i] == '\'' || v[i] == '`') i = skip_quoted(v, i);
                else if (v[i] == '{') depth++;
                else if (v[i] == '}' && --depth == 0) break;
            }
        }
        i++;
    }
    return i;
}

std::vector<StringLiteral::Part> StringLiteral::split(const std::string& value) {
    std::vector<Part> parts;
    std::string text;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '$') {
            text += '$';
            i++;
            continue;
        }
        if (value[i] != '$' || i + 1 >= value.size() || value[i + 1] != '{') {
            text += value[i];
            continue;
        }
        int depth = 0;
        size_t close = i + 1;
        for (; close < value.size(); close++) {
            char c = value[close];
            if (c == '"' || c == '\'' || c == '`') close = skip_quoted(value, close);
            else if (c == '{') depth++;
            else if (c == '}' && --depth == 0) break;
        }
        std::string inner = close < value.size() ? value.substr(i + 2, close - i - 2) : "";
        if (close >= value.size() || inner.find_first_not_of(" \t\n") == std::string::npos) {
            text += value[i];
            continue;
        }
        if (!text.empty()) parts.push_back({false, text, nullptr});
        text.clear();
        parts.push_back({true, inner, nullptr});
        i = close;
    }
    if (!text.empty()) parts.push_back({false, text, nullptr});
    return parts;
}

std::string StringLiteral::to_webcc() {
    if (parts.empty()) return "\"\"";
    if (is_static()) {
        std::string content;
        for (auto& p : parts) content += p.content;
        return "\"" + escape_cpp(content) + "\"";
    }
    std::string code = "coi::string::concat(";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) code += ", ";
        code += parts[i].is_expr ? parts[i].expr->to_webcc() : "\"" + escape_cpp(parts[i].content) + "\"";
    }
    return code + ")";
}

bool StringLiteral::is_static() {
    for (auto& p : parts) if (p.is_expr) return false;
    return true;
}

std::vector<Expression*> StringLiteral::get_children() {
    std::vector<Expression*> children;
    for (auto& p : parts) if (p.expr) children.push_back(p.expr.get());
    return children;
}

std::string Identifier::to_webcc() {
    if(g_ref_props.count(name)) {
        return "(*" + name + ")";
    }
    return name;
}

void Identifier::collect_dependencies(std::set<std::string>& deps) {
    deps.insert(name);
}

BinaryOp::BinaryOp(std::unique_ptr<Expression> l, const std::string& o, std::unique_ptr<Expression> r)
    : left(std::move(l)), op(o), right(std::move(r)){}

std::string BinaryOp::to_webcc() {
    // Optimize string concatenation chains to use formatter
    if (op == "+" && is_string_expr(left.get())) {
        std::vector<Expression*> parts;
        flatten_string_concat(this, parts);
        return generate_formatter_expr(parts);
    }
    // Wrap in parentheses to preserve operator precedence
    return "(" + left->to_webcc() + " " + op + " " + right->to_webcc() + ")";
}

std::string FunctionCall::args_to_string() {
    if (args.empty()) return "\"\"";

    std::string result = "coi::string::concat(";
    for(size_t i = 0; i < args.size(); i++){
        if(i > 0) result += ", ";
        result += args[i].value->to_webcc();
    }
    result += ")";
    return result;
}

// platform enum/flags -> webcc enum class
static std::string to_webcc_arg(const std::string& code, const std::string& coi_type) {
    // array literal needs its vector type
    if (coi_type.ends_with("[]") && !code.empty() && code[0] == '{')
        return "coi::vector<" + convert_type(coi_type.substr(0, coi_type.size() - 2)) + ">" + code;
    std::string cpp = DefSchema::instance().webcc_cast_type(coi_type);
    return cpp.empty() ? code : "static_cast<" + cpp + ">((int)(" + code + "))";
}

// and back
static std::string from_webcc_value(const std::string& code, const std::string& coi_type) {
    const TypeDef* td = DefSchema::instance().lookup_type(coi_type);
    if (!td) return code;
    if (!td->enum_cpp.empty()) return "static_cast<" + coi_type + ">((int)(" + code + "))";
    if (!td->flags_cpp.empty()) return "static_cast<" + convert_type(td->alias_of) + ">(" + code + ")";
    return code;
}

// plain values and callbacks of a schema call
struct SplitArgs {
    std::vector<const CallArg*> values;
    std::vector<std::pair<const MethodParam*, const CallArg*>> callbacks;
};

static bool is_callback_param(const MethodParam& p) { return p.type.rfind("def", 0) == 0; }

static SplitArgs split_call_args(const MethodDef& method, const std::vector<CallArg>& args, const std::string& call_name) {
    SplitArgs out;
    std::vector<const MethodParam*> callback_params;
    for (const auto& p : method.params)
        if (is_callback_param(p)) callback_params.push_back(&p);
    size_t next_positional = 0;
    for (const auto& arg : args) {
        if (!arg.is_reference && arg.name.empty()) { out.values.push_back(&arg); continue; }
        const MethodParam* param = nullptr;
        if (!arg.name.empty()) {
            for (const auto* p : callback_params) if (p->name == arg.name) param = p;
            if (!param) ErrorHandler::compiler_error("'" + call_name + "' has no callback named '" + arg.name + "'");
        } else {
            while (next_positional < callback_params.size()) {
                const auto* p = callback_params[next_positional++];
                bool taken = false;
                for (auto& [used, _] : out.callbacks) if (used == p) taken = true;
                if (!taken) { param = p; break; }
            }
            if (!param) ErrorHandler::compiler_error("Too many callback arguments for '" + call_name + "'");
        }
        if (!arg.is_reference)
            ErrorHandler::compiler_error("Callback argument must use '&' prefix (e.g., &" + arg.value->to_webcc() + ")");
        out.callbacks.push_back({param, &arg});
    }
    return out;
}

// handler may take fewer params than the event has fields
std::string generate_event_call(const MethodDef& event_method, const std::string& handle_type,
                                const std::string& callback, const std::string& evt) {
    SchemaEventSpec spec = SchemaEventSpec::parse(event_method.mapping_value);
    std::vector<std::string> field_types;
    for (const auto& p : event_method.params)
        if (is_callback_param(p)) field_types = callback_param_types(p.type);
    int n = ComponentTypeContext::instance().get_method_param_count(callback);
    if (n < 0 || n > (int)spec.fields.size()) n = (int)spec.fields.size();
    // whole event as one pod
    bool as_pod = false;
    if (!spec.pod.empty()) {
        auto* sig = ComponentTypeContext::instance().get_method_signature(callback);
        as_pod = sig && sig->param_types.size() == 1 && DefSchema::instance().resolve_alias(sig->param_types[0]) == spec.pod;
    }
    if (as_pod) n = (int)spec.fields.size();
    std::string code = "this->" + callback + "(";
    if (as_pod) code += spec.pod + "{";
    for (int i = 0; i < n; i++) {
        if (i) code += ", ";
        std::string ft = i < (int)field_types.size() ? field_types[i] : "";
        std::string f = evt + "." + spec.fields[i];
        if (ft == "string") code += "coi::string(" + f + ")";
        else if (ft == "uint8[]") code += "coi_bytes(" + f + ")"; // a view into the event buffer
        else code += from_webcc_value(f, ft);
    }
    if (as_pod) code += "}";
    code += ")";
    std::string full = "webcc::" + spec.struct_name();
    g_used_events[spec.ns + "::" + spec.name] = {spec.ns, spec.name, spec.key, full, handle_type, spec.last};
    return code;
}

// page-wide event, turned on with the +listen values
static std::string generate_page_event_registration(const MethodDef& event_method, const CallArg& arg) {
    SchemaEventSpec spec = SchemaEventSpec::parse(event_method.mapping_value);
    std::string full = "webcc::" + spec.struct_name();
    std::string call = generate_event_call(event_method, "", arg.value->to_webcc(), "_e");
    std::string listen;
    if (!spec.listen.empty()) {
        listen = "webcc::" + spec.ns + "::" + spec.listen + "(";
        for (size_t i = 0; i < spec.listen_params.size(); i++) {
            const auto& lp = spec.listen_params[i];
            listen += (i ? ", " : "") + to_webcc_arg(lp.value, lp.type);
        }
        listen += "), ";
    }
    return "(" + listen + "coi_events<" + full + ">.set(webcc::handle((int32_t)(uintptr_t)this), [this](const " + full +
           "& _e) { (void)_e; " + call + "; }, this))";
}

// coi_events<E>.set(handle, [this](const E& _e) { this->handler(...); }, this)
static std::string generate_event_registration(const MethodDef& event_method, const std::string& handle_type,
                                               const std::string& handle_expr, const CallArg& arg) {
    SchemaEventSpec spec = SchemaEventSpec::parse(event_method.mapping_value);
    std::string full = "webcc::" + spec.struct_name();
    std::string call = generate_event_call(event_method, handle_type, arg.value->to_webcc(), "_e");
    return "coi_events<" + full + ">.set(" + handle_expr + ", [this](const " + full + "& _e) { (void)_e; " + call + "; }, this)";
}

// callbacks on a call that returns a handle
static std::string wrap_with_callbacks(const std::string& call, const MethodDef& method, const SplitArgs& split, const std::string& call_name) {
    if (split.callbacks.empty()) return call;
    std::string code = "[&]() {\n            auto _h = " + call + ";\n";
    for (const auto& [param, arg] : split.callbacks) {
        const MethodDef* ev = DefSchema::instance().lookup_method(method.return_type, param->name);
        if (!ev || ev->mapping_type != MappingType::Event)
            ErrorHandler::compiler_error("'" + call_name + "': '" + param->name + "' is not an event of " + method.return_type);
        code += "            " + generate_event_registration(*ev, method.return_type, "_h", *arg) + ";\n";
    }
    return code + "            return _h;\n        }()";
}

std::string FunctionCall::to_webcc() {
    // Parse Type.method or instance.method
    size_t dot_pos = name.rfind('.');
    std::string type_or_obj = "";
    std::string method = name;

    if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < name.length() - 1) {
        type_or_obj = name.substr(0, dot_pos);
        method = name.substr(dot_pos + 1);
    }

    std::string resolved_receiver = type_or_obj;
    if (!resolved_receiver.empty() && g_ref_props.count(resolved_receiver))
    {
        resolved_receiver = "(*" + resolved_receiver + ")";
    }
    else if (!resolved_receiver.empty())
    {
        // A chained call (`store.error(x).isEmpty()`, `items[i].name.isEmpty()`) arrives with
        // its receiver already stringified by the parser, before reference params were known:
        // deref a leading reference param there too.
        size_t k = 0;
        while (k < resolved_receiver.size() && (std::isalnum(static_cast<unsigned char>(resolved_receiver[k])) || resolved_receiver[k] == '_')) k++;
        if (k > 0 && k < resolved_receiver.size() && (resolved_receiver[k] == '.' || resolved_receiver[k] == '[') && g_ref_props.count(resolved_receiver.substr(0, k)))
        {
            resolved_receiver = "(*" + resolved_receiver.substr(0, k) + ")" + resolved_receiver.substr(k);
        }
        // receiver starting with a top-level call, fmt(x).length()
        size_t paren = resolved_receiver.find('(');
        if (paren != std::string::npos && paren > 0)
        {
            std::string free_fn = FreeFunctionRegistry::instance().resolve_call(resolved_receiver.substr(0, paren));
            if (!free_fn.empty())
            {
                resolved_receiver = free_fn + resolved_receiver.substr(paren);
            }
        }
    }

    // Try DefSchema lookup first (handles @intrinsic, @inline, @map)
    if (!type_or_obj.empty()) {
        // Check for static type call (e.g., System.random, Input.isKeyDown)
        if (std::isupper(type_or_obj[0])) {
            if (auto* method_def = DefSchema::instance().lookup_method(type_or_obj, method)) {
                // For intrinsics, allow fewer args (they handle optional params internally)
                // For others, require exact match
                bool arg_count_ok = (method_def->mapping_type == MappingType::Intrinsic)
                    ? args.size() <= method_def->params.size()
                    : args.size() == method_def->params.size();
                    
                if (arg_count_ok) {
                    switch (method_def->mapping_type) {
                        case MappingType::Intrinsic: {
                            std::string code = generate_intrinsic(method_def->mapping_value, args);
                            if (!code.empty()) return code;
                            break;
                        }
                        case MappingType::Inline:
                            return expand_inline_template(method_def->mapping_value, type_or_obj, args);
                        case MappingType::Map:
                            // Handled below by existing schema lookup
                            break;
                        case MappingType::Event:
                            // Clipboard.onPasteText(&h): a page-wide event
                            if (method_def->is_shared && args.size() == 1)
                                return generate_page_event_registration(*method_def, args[0]);
                            break;
                    }
                }
            }
        }

        // Enum.size()
        if (method == "size" && args.empty() && std::isupper((unsigned char)type_or_obj[0]) &&
            ComponentTypeContext::instance().get_symbol_type(type_or_obj).empty()) {
            std::string cpp = type_or_obj.find('.') != std::string::npos ? convert_type(type_or_obj)
                                                                       : ComponentTypeContext::instance().resolve(type_or_obj);
            return "static_cast<int>(" + cpp + "::_COUNT)";
        }

        // string/array methods, not for handles
        std::string receiver_type = ComponentTypeContext::instance().get_symbol_type(type_or_obj);
        bool handle_receiver = !receiver_type.empty() && DefSchema::instance().is_handle(DefSchema::instance().resolve_alias(receiver_type));
        if (!handle_receiver) {
            // For string methods, we need to check against the "string" type
            // Use arg_count to find the correct overload
            if (auto* method_def = DefSchema::instance().lookup_method("string", method, args.size())) {
                if (method_def->mapping_type == MappingType::Inline) {
                    return expand_inline_template(method_def->mapping_value, resolved_receiver, args);
                }
            }

            // Check array methods
            if (auto* method_def = DefSchema::instance().lookup_method("array", method, args.size())) {
                if (method_def->mapping_type == MappingType::Inline) {
                    return expand_inline_template(method_def->mapping_value, resolved_receiver, args);
                }
            }
        }
    }


    // DefSchema-based transformation for @map methods (webcc API calls)
    std::string obj_arg = "";
    bool pass_obj = false;
    const MethodDef* map_method = nullptr;
    std::string map_ns = "";
    std::string map_func = "";

    if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < name.length() - 1) {
        std::string obj = name.substr(0, dot_pos);
        std::string lookup_obj = obj;
        std::string method_name = name.substr(dot_pos + 1);

        // Check if obj is a type name (static call) or instance
        bool is_static_call = !lookup_obj.empty() && std::isupper(lookup_obj[0]);

        if (is_static_call) {
            // Static call: Type.method() - look up directly
            map_method = DefSchema::instance().lookup_method(lookup_obj, method_name);
        } else {
            // Instance call: obj.method() - resolve using known symbol type only.
            // This avoids false-positive remapping based solely on method name
            // (e.g., auth.configure() incorrectly mapping to wgpu::configure).
            std::string obj_type = receiver_type.empty() ? ComponentTypeContext::instance().get_symbol_type(lookup_obj) : receiver_type;
            if (!obj_type.empty()) {
                // Array and fixed-size array variables do not have @map instance methods.
                if (obj_type.ends_with("[]")) {
                    obj_type.clear();
                } else {
                    size_t bracket_pos = obj_type.rfind('[');
                    if (bracket_pos != std::string::npos && obj_type.back() == ']') {
                        obj_type = obj_type.substr(0, bracket_pos);
                    }
                }

                if (!obj_type.empty()) {
                    // Resolve aliases/local component types before lookup.
                    obj_type = ComponentTypeContext::instance().resolve(obj_type);
                    obj_type = DefSchema::instance().resolve_alias(obj_type);

                    map_method = DefSchema::instance().lookup_method(obj_type, method_name, args.size());
                    if (map_method && !map_method->is_shared) {
                        if (map_method->mapping_type == MappingType::Inline) {
                            // Handle @inline methods for typed instance calls (e.g., socket.isConnected())
                            return expand_inline_template(map_method->mapping_value, resolved_receiver, args);
                        } else if (map_method->mapping_type == MappingType::Event) {
                            // ws.onMessage(&handler)
                            if (args.size() != 1 || !args[0].is_reference)
                                ErrorHandler::compiler_error("'" + name + "' takes one '&handler' argument");
                            return generate_event_registration(*map_method, obj_type, resolved_receiver, args[0]);
                        } else if (map_method->mapping_type == MappingType::Map) {
                            pass_obj = true;
                            obj_arg = resolved_receiver;
                        } else {
                            map_method = nullptr;
                        }
                    } else {
                        map_method = nullptr;
                    }
                }
            }
        }

        // Extract ns::func from @map value
        if (map_method && map_method->mapping_type == MappingType::Map && !map_method->mapping_value.empty()) {
            size_t sep = map_method->mapping_value.find("::");
            if (sep != std::string::npos) {
                map_ns = map_method->mapping_value.substr(0, sep);
                map_func = map_method->mapping_value.substr(sep + 2);
            }
        }
    }

    if (map_method && !map_ns.empty() && !map_func.empty()) {
        // callbacks bind the returned handle, not passed to webcc
        SplitArgs split = split_call_args(*map_method, args, name);
        if (!split.callbacks.empty()) {
            std::string call = "webcc::" + map_ns + "::" + map_func + "(";
            bool first_arg = true;
            if (pass_obj) { call += obj_arg; first_arg = false; }
            std::vector<const MethodParam*> value_params;
            for (const auto& p : map_method->params) if (!is_callback_param(p)) value_params.push_back(&p);
            for (size_t vi = 0; vi < split.values.size(); vi++) {
                if (!first_arg) call += ", ";
                std::string v = split.values[vi]->value->to_webcc();
                call += vi < value_params.size() ? to_webcc_arg(v, value_params[vi]->type) : v;
                first_arg = false;
            }
            call += ")";
            return wrap_with_callbacks(call, *map_method, split, name);
        }

        // Check for string concat argument - use formatter block
        bool has_string_concat_arg = false;
        int string_concat_arg_idx = -1;
        for (size_t i = 0; i < args.size(); i++) {
            if (is_string_expr(args[i].value.get()) && dynamic_cast<BinaryOp*>(args[i].value.get())) {
                has_string_concat_arg = true;
                string_concat_arg_idx = i;
                break;
            }
        }

        if (has_string_concat_arg) {
            std::vector<Expression*> parts;
            flatten_string_concat(args[string_concat_arg_idx].value.get(), parts);

            std::string call_prefix = "webcc::" + map_ns + "::" + map_func + "(";
            std::string call_suffix;

            bool first_arg = true;
            if (pass_obj) {
                call_prefix += obj_arg;
                first_arg = false;
            }

            for (size_t i = 0; i < args.size(); i++) {
                if (!first_arg) {
                    if ((int)i == string_concat_arg_idx) {
                        call_prefix += ", ";
                    } else if ((int)i < string_concat_arg_idx) {
                        call_prefix += ", " + args[i].value->to_webcc();
                    } else {
                        call_suffix += ", " + args[i].value->to_webcc();
                    }
                } else {
                    if ((int)i != string_concat_arg_idx) {
                        call_prefix += args[i].value->to_webcc();
                    }
                }
                first_arg = false;
            }
            call_suffix += ")";

            return generate_formatter_block(parts, call_prefix, call_suffix);
        }

        std::string code = "webcc::" + map_ns + "::" + map_func + "(";
        bool first_arg = true;

        if (pass_obj) {
            code += obj_arg;
            first_arg = false;
        }

        for(size_t i = 0; i < args.size(); i++){
            if (!first_arg) code += ", ";
            
            // Check if this parameter expects a function type and the arg is a member function identifier
            std::string arg_code;
            bool wrapped = false;
            if (i < map_method->params.size()) {
                const std::string& param_type = map_method->params[i].type;
                if (param_type.starts_with("function<")) {
                    if (auto* id = dynamic_cast<Identifier*>(args[i].value.get())) {
                        auto* sig = ComponentTypeContext::instance().get_method_signature(id->name);
                        if (sig) {
                            // Generate lambda wrapper for member function
                            arg_code = "[this](";
                            for (size_t j = 0; j < sig->param_types.size(); ++j) {
                                if (j > 0) arg_code += ", ";
                                arg_code += "const " + convert_type(sig->param_types[j]) + "& _arg" + std::to_string(j);
                            }
                            arg_code += ") { this->" + id->name + "(";
                            for (size_t j = 0; j < sig->param_types.size(); ++j) {
                                if (j > 0) arg_code += ", ";
                                arg_code += "_arg" + std::to_string(j);
                            }
                            arg_code += "); }";
                            wrapped = true;
                        }
                    }
                }
            }
            if (!wrapped) {
                arg_code = args[i].value->to_webcc();
                if (i < map_method->params.size())
                    arg_code = to_webcc_arg(arg_code, map_method->params[i].type);
            }
            code += arg_code;
            first_arg = false;
        }
        code += ")";

        // Check return type from method definition
        if (map_method->return_type == "int") {
            code = "(int32_t)(" + code + ")";
        }
        code = from_webcc_value(code, map_method->return_type);

        return code;
    }

    std::string call_name = name;
    std::string free_fn = FreeFunctionRegistry::instance().resolve_call(name);
    if (!free_fn.empty())
    {
        call_name = free_fn;
    }
    else if (!type_or_obj.empty())
    {
        call_name = resolved_receiver + "." + method;
    }
    if (free_fn.empty() &&
        name.find('.') == std::string::npos &&
        name.find("::") == std::string::npos &&
        !name.empty() &&
        std::isupper(name[0])) {
        std::string resolved_local = ComponentTypeContext::instance().resolve(name);
        if (resolved_local != name) {
            call_name = resolved_local;
        } else {
            const std::string &current_component = ComponentTypeContext::instance().component_name;
            size_t module_sep = current_component.find('_');
            if (module_sep != std::string::npos) {
                std::string module_name = current_component.substr(0, module_sep);
                std::string qualified_ctor = module_name + "::" + name;
                call_name = convert_type(qualified_ctor);
            }
        }
    }

    std::string result = call_name + "(";
    for(size_t i = 0; i < args.size(); i++){
        if(i > 0) result += ", ";
        
        // If this argument is passed by reference (&) and is an identifier that's a member function,
        // generate a lambda wrapper for C++ compatibility
        std::string arg_code;
        if (args[i].is_reference) {
            if (auto* id = dynamic_cast<Identifier*>(args[i].value.get())) {
                auto* sig = ComponentTypeContext::instance().get_method_signature(id->name);
                if (sig) {
                    // Generate lambda wrapper for member function reference
                    arg_code = "[this](";
                    for (size_t j = 0; j < sig->param_types.size(); ++j) {
                        if (j > 0) arg_code += ", ";
                        arg_code += "const " + convert_type(sig->param_types[j]) + "& _arg" + std::to_string(j);
                    }
                    arg_code += ") { this->" + id->name + "(";
                    for (size_t j = 0; j < sig->param_types.size(); ++j) {
                        if (j > 0) arg_code += ", ";
                        arg_code += "_arg" + std::to_string(j);
                    }
                    arg_code += "); }";
                }
            }
        }
        if (arg_code.empty()) {
            arg_code = args[i].value->to_webcc();
        }
        result += arg_code;
    }
    result += ")";
    return result;
}

std::vector<Expression*> FunctionCall::get_children() {
    std::vector<Expression*> children;
    children.reserve(args.size());
    for (auto& arg : args) children.push_back(arg.value.get());
    return children;
}

void FunctionCall::collect_dependencies(std::set<std::string>& deps) {
    // Handle object.method() calls - the receiver's leading variable is the dependency
    // (items.size(), items[i].name.isEmpty() both read items)
    size_t dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        size_t k = 0;
        while (k < name.size() && (std::isalnum(static_cast<unsigned char>(name[k])) || name[k] == '_')) k++;
        deps.insert(name.substr(0, k > 0 ? k : dot_pos));
    }
    else if (auto it = g_method_reads.find(name); it != g_method_reads.end()) {
        deps.insert(it->second.begin(), it->second.end());
    }
    else if (g_method_reads_building && name.find("::") == std::string::npos) {
        // a plain call while the method reads are being computed: the callee's name,
        // so a() -> b() -> member closes over every level
        deps.insert(name);
    }
    // Also traverse children via get_children()
    for (auto* child : get_children()) {
        if (child) child->collect_dependencies(deps);
    }
}

MemberAccess::MemberAccess(std::unique_ptr<Expression> obj, const std::string& mem)
    : object(std::move(obj)), member(mem) {}

std::string MemberAccess::to_webcc() {
    // Check if this is a shared constant access (e.g., Math.PI)
    if (auto id = dynamic_cast<Identifier*>(object.get())) {
        std::string resolved_type = ComponentTypeContext::instance().resolve(id->name);

        // Check for JSON field token access (e.g., User.name -> __coi_field_User_name)
        const std::vector<DataField>* fields = DataTypeRegistry::instance().lookup(resolved_type);
        if (!fields && resolved_type != id->name) {
            fields = DataTypeRegistry::instance().lookup(id->name);
            resolved_type = id->name;
        }
        if (fields) {
            for (const auto& field : *fields) {
                if (field.name == member) {
                    return field_token_symbol_name(resolved_type, member);
                }
            }
        }

        // Check if it's a type with a shared constant
        if (!id->name.empty() && std::isupper(id->name[0])) {
            if (auto* method_def = DefSchema::instance().lookup_method(id->name, member)) {
                if (method_def->is_shared && method_def->is_constant) {
                    // For constants, just return the inline value directly
                    if (method_def->mapping_type == MappingType::Inline) {
                        return method_def->mapping_value;
                    }
                }
            }
        }
    }
    return object->to_webcc() + "." + member;
}

void MemberAccess::collect_member_dependencies(std::set<MemberDependency>& member_deps) {
    // Add this object.member as a dependency
    if (auto id = dynamic_cast<Identifier*>(object.get())) {
        member_deps.insert({id->name, member});
    }
    // Also traverse children via get_children()
    for (auto* child : get_children()) {
        if (child) child->collect_member_dependencies(member_deps);
    }
}

PostfixOp::PostfixOp(std::unique_ptr<Expression> expr, const std::string& o)
    : operand(std::move(expr)), op(o) {}

std::string PostfixOp::to_webcc() {
    return operand->to_webcc() + op;
}

UnaryOp::UnaryOp(const std::string& o, std::unique_ptr<Expression> expr)
    : op(o), operand(std::move(expr)) {}

std::string UnaryOp::to_webcc() {
    return op + operand->to_webcc();
}

bool UnaryOp::is_static() { return operand->is_static(); }

// ReferenceExpression - pass by reference (borrow, no ownership transfer)
// When referencing a member function, generates a lambda wrapper for C++ compatibility
std::string ReferenceExpression::to_webcc() {
    // Check if this is a reference to a component method
    if (auto* id = dynamic_cast<Identifier*>(operand.get())) {
        const std::string& method_name = id->name;
        auto* sig = ComponentTypeContext::instance().get_method_signature(method_name);
        if (sig) {
            // Generate lambda wrapper: [this](const T0& _arg0, ...) { this->methodName(_arg0, ...); }
            std::string result = "[this](";
            for (size_t i = 0; i < sig->param_types.size(); ++i) {
                if (i > 0) result += ", ";
                result += "const " + convert_type(sig->param_types[i]) + "& _arg" + std::to_string(i);
            }
            result += ") { this->" + method_name + "(";
            for (size_t i = 0; i < sig->param_types.size(); ++i) {
                if (i > 0) result += ", ";
                result += "_arg" + std::to_string(i);
            }
            result += "); }";
            return result;
        }
        std::string free_fn = FreeFunctionRegistry::instance().resolve_call(method_name);
        if (!free_fn.empty()) return free_fn;
    }
    return operand->to_webcc();  // References are handled at call sites
}

// MoveExpression - generates coi::move() for explicit ownership transfer
std::string MoveExpression::to_webcc() {
    return "coi::move(" + operand->to_webcc() + ")";
}

TernaryOp::TernaryOp(std::unique_ptr<Expression> cond, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f)
    : condition(std::move(cond)), true_expr(std::move(t)), false_expr(std::move(f)) {}

std::string TernaryOp::to_webcc() {
    return "(" + condition->to_webcc() + " ? " + true_expr->to_webcc() + " : " + false_expr->to_webcc() + ")";
}

bool TernaryOp::is_static() {
    return condition->is_static() && true_expr->is_static() && false_expr->is_static();
}

std::string ArrayLiteral::to_webcc() {
    std::string code = "{";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) code += ", ";
        code += elements[i]->to_webcc();
    }
    code += "}";
    return code;
}

std::vector<Expression*> ArrayLiteral::get_children() {
    std::vector<Expression*> children;
    children.reserve(elements.size());
    for (auto& elem : elements) children.push_back(elem.get());
    return children;
}

bool ArrayLiteral::is_static() {
    for (auto& elem : elements) if (!elem->is_static()) return false;
    return true;
}

void ArrayLiteral::propagate_element_type(const std::string& type) {
    element_type = type;
    for (auto& elem : elements) {
        // If this is an anonymous struct literal (ComponentConstruction with empty name),
        // fill in the type from the array's element type
        if (auto comp = dynamic_cast<ComponentConstruction*>(elem.get())) {
            if (comp->component_name.empty()) {
                comp->component_name = type;
            }
        }
    }
}

std::string ArrayRepeatLiteral::to_webcc() {
    // Generate initialization - webcc::array constructor will fill with the value
    // The actual array type and initialization is handled by VarDeclaration::to_webcc
    return value->to_webcc();
}

bool ArrayRepeatLiteral::is_static() {
    return value->is_static();
}

IndexAccess::IndexAccess(std::unique_ptr<Expression> arr, std::unique_ptr<Expression> idx)
    : array(std::move(arr)), index(std::move(idx)) {}

std::string IndexAccess::to_webcc() {
    return array->to_webcc() + "[" + index->to_webcc() + "]";
}

std::string EnumAccess::to_webcc() {
    if (!component_name.empty())
        return convert_type(component_name + "." + enum_name) + "::" + value_name;
    return ComponentTypeContext::instance().resolve(enum_name) + "::" + value_name;
}

std::string ComponentConstruction::to_webcc() {
    // Resolve component-local data types (e.g., Body -> App_Body)
    std::string resolved_name = ComponentTypeContext::instance().resolve(component_name);
    if (resolved_name == component_name &&
        component_name.find("::") == std::string::npos &&
        !component_name.empty() &&
        std::isupper(component_name[0])) {
        const std::string &current_component = ComponentTypeContext::instance().component_name;
        size_t module_sep = current_component.find('_');
        if (module_sep != std::string::npos) {
            std::string module_name = current_component.substr(0, module_sep);
            std::string qualified_ctor = module_name + "::" + component_name;
            resolved_name = convert_type(qualified_ctor);
        }
    }
    // Explicit namespaced constructors (e.g., Supabase::Auth(...))
    // must be lowered to C++ type names (Supabase_Auth(...)).
    if (resolved_name.find("::") != std::string::npos) {
        resolved_name = convert_type(resolved_name);
    }
    // named fields go out in declaration order
    bool any_named = false;
    for (const auto& a : args) any_named = any_named || !a.name.empty();
    if (any_named) {
        if (const auto* fields = DataTypeRegistry::instance().lookup(resolved_name)) {
            std::string result = resolved_name + "{";
            bool first = true;
            for (const auto& field : *fields) {
                for (const auto& a : args) {
                    if (a.name != field.name) continue;
                    std::string value = a.value->to_webcc();
                    if (a.is_move) value = "coi::move(" + value + ")";
                    result += (first ? "." : ", .") + field.name + " = " + value;
                    first = false;
                }
            }
            return result + "}";
        }
    }

    std::string result = resolved_name + "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) result += ", ";
        std::string value = args[i].value->to_webcc();
        auto* id = dynamic_cast<Identifier*>(args[i].value.get());
        const auto& sigs = ComponentTypeContext::instance().method_signatures;
        std::string free_fn = id ? FreeFunctionRegistry::instance().resolve_call(id->name) : "";
        if (args[i].is_reference && id && (sigs.count(id->name) || !free_fn.empty())) {
            // &method as a callback: Box(1, &changed)
            std::string params, fwd;
            if (sigs.count(id->name)) {
                const auto& types = sigs.at(id->name).param_types;
                for (size_t k = 0; k < types.size(); k++) {
                    params += (k ? ", " : "") + convert_type(types[k]) + " _a" + std::to_string(k);
                    fwd += (k ? ", _a" : "_a") + std::to_string(k);
                }
                result += "[this](" + params + ") { this->" + id->name + "(" + fwd + "); }";
            } else {
                result += "[](auto&&... _a) { return " + free_fn + "(_a...); }";
            }
        }
        else if (args[i].is_reference)
            result += "&(" + value + ")";
        else if (args[i].is_move)
            result += "coi::move(" + value + ")";
        else
            result += value;
    }
    result += ")";
    return result;
}

std::vector<Expression*> ComponentConstruction::get_children() {
    std::vector<Expression*> children;
    children.reserve(args.size());
    for (auto& arg : args) children.push_back(arg.value.get());
    return children;
}

// Match expression code generation
// Generates a lambda (IIFE) with if-else chain
std::string MatchExpr::to_webcc() {
    std::string code = result_type.empty() ? "[&]() {\n" : "[&]() -> " + convert_type(result_type) + " {\n";
    code += "        const auto& _match_subject = " + subject->to_webcc() + ";\n";
    
    bool first = true;
    bool has_else = false;
    
    for (const auto& arm : arms) {
        if (arm.pattern.kind == MatchPattern::Kind::Else) {
            has_else = true;
            // else arm - will be handled at the end
            continue;
        }
        
        std::string condition;
        std::string bindings;
        
        if (arm.pattern.kind == MatchPattern::Kind::Literal) {
            // Literal pattern: compare directly with the value
            condition = "_match_subject == " + arm.pattern.literal_value->to_webcc();
        }
        else if (arm.pattern.kind == MatchPattern::Kind::Enum) {
            // Enum pattern: compare directly
            std::string resolved_type = ComponentTypeContext::instance().resolve(arm.pattern.type_name);
            condition = "_match_subject == " + resolved_type + "::" + arm.pattern.enum_value;
        }
        else if (arm.pattern.kind == MatchPattern::Kind::Pod) {
            // Pod pattern: check each field
            std::vector<std::string> conditions;
            for (const auto& field : arm.pattern.fields) {
                if (field.value) {
                    // Value match: _match_subject.field == value
                    conditions.push_back("_match_subject." + field.name + " == " + field.value->to_webcc());
                } else {
                    // Binding pattern: capture the field into a local variable
                    bindings += "            const auto& " + field.name + " = _match_subject." + field.name + ";\n";
                }
            }
            
            if (conditions.empty()) {
                condition = "true";  // Just binding, always matches
            } else {
                condition = conditions[0];
                for (size_t i = 1; i < conditions.size(); ++i) {
                    condition += " && " + conditions[i];
                }
            }
        }
        else if (arm.pattern.kind == MatchPattern::Kind::Variant) {
            condition = "_match_subject.is_" + arm.pattern.type_name + "()";
            if (!arm.pattern.variant_bindings.empty()) {
                bindings += "            const auto& __coi_variant = _match_subject.as_" + arm.pattern.type_name + "();\n";
                for (size_t i = 0; i < arm.pattern.variant_bindings.size(); ++i) {
                    bindings += "            const auto& " + arm.pattern.variant_bindings[i].name +
                                " = __coi_variant._" + std::to_string(i) + ";\n";
                }
            }
        }
        
        if (first) {
            code += "        if (" + condition + ") {\n";
            first = false;
        } else {
            code += "        } else if (" + condition + ") {\n";
        }
        
        code += bindings;
        code += "            return " + arm.body->to_webcc() + ";\n";
    }
    
    // Generate else branch
    for (const auto& arm : arms) {
        if (arm.pattern.kind == MatchPattern::Kind::Else) {
            if (first) {
                // Only else arm, no conditions
                code += "        return " + arm.body->to_webcc() + ";\n";
            } else {
                code += "        } else {\n";
                code += "            return " + arm.body->to_webcc() + ";\n";
                code += "        }\n";
            }
            has_else = true;
            break;
        }
    }
    
    if (!has_else && !first) {
        // Close the last if without else
        code += "        }\n";
    }
    
    code += "    }()";
    return code;
}


void MatchExpr::collect_dependencies(std::set<std::string>& deps) {
    subject->collect_dependencies(deps);
    for (const auto& arm : arms) {
        // Collect dependencies from literal pattern value
        if (arm.pattern.literal_value) {
            arm.pattern.literal_value->collect_dependencies(deps);
        }
        // Collect dependencies from pod pattern values
        for (const auto& field : arm.pattern.fields) {
            if (field.value) {
                field.value->collect_dependencies(deps);
            }
        }
        arm.body->collect_dependencies(deps);
    }
}

bool MatchExpr::is_static() {
    if (!subject->is_static()) return false;
    for (const auto& arm : arms) {
        if (arm.pattern.literal_value && !arm.pattern.literal_value->is_static()) return false;
        for (const auto& field : arm.pattern.fields) {
            if (field.value && !field.value->is_static()) return false;
        }
        if (!arm.body->is_static()) return false;
    }
    return true;
}

std::string BlockExpr::to_webcc() {
    std::string code = "([&]() {\n";
    for (const auto& stmt : statements) {
        code += "            " + stmt->to_webcc() + "\n";
    }
    code += "        }())";
    return code;
}

void BlockExpr::collect_dependencies(std::set<std::string>& deps) {
    for (const auto& stmt : statements) {
        stmt->collect_dependencies(deps);
    }
}


std::vector<Expression*> MethodCall::get_children() {
    std::vector<Expression*> children{receiver.get()};
    for (auto& a : args) children.push_back(a.value.get());
    return children;
}

std::string MethodCall::to_webcc() {
    std::string type = receiver_type;
    if (type.empty()) {
        if (dynamic_cast<StringLiteral*>(receiver.get())) type = "string";
        else if (dynamic_cast<IntLiteral*>(receiver.get())) type = "int32";
        else if (dynamic_cast<FloatLiteral*>(receiver.get())) type = "float64";
        else if (dynamic_cast<BoolLiteral*>(receiver.get())) type = "bool";
        else if (auto* call = dynamic_cast<FunctionCall*>(receiver.get())) {
            // System.getDateNow().toInt64() in a view: the checker didn't type it, the def says
            size_t dot = call->name.rfind('.');
            if (dot != std::string::npos) {
                const auto* m = DefSchema::instance().lookup_method(call->name.substr(0, dot), call->name.substr(dot + 1));
                if (m && !m->return_type.empty()) type = m->return_type;
            }
        }
    }
    std::string def_type = !type.empty() && type.back() == ']' ? "array" : DefSchema::instance().resolve_alias(type);
    std::string recv = receiver->to_webcc();
    if (dynamic_cast<StringLiteral*>(receiver.get()) && receiver->is_static())
        recv = "coi::string(" + recv + ")";
    const auto* m = DefSchema::instance().lookup_method(def_type, method, args.size());
    if (!m && (type.empty() || type == "unknown")) {
        m = DefSchema::instance().lookup_method("string", method, args.size());
        if (!m) m = DefSchema::instance().lookup_method("array", method, args.size());
    }
    if (m && m->mapping_type == MappingType::Inline)
        return expand_inline_template(m->mapping_value, "(" + recv + ")", args);
    if (m && m->mapping_type == MappingType::Map) {
        std::string call = "webcc::" + m->mapping_value + "(" + recv;
        for (size_t i = 0; i < args.size(); i++) {
            std::string a = args[i].value->to_webcc();
            call += ", " + (i < m->params.size() ? to_webcc_arg(a, m->params[i].type) : a);
        }
        return from_webcc_value(call + ")", m->return_type);
    }
    std::string call = "(" + recv + ")." + method + "(";
    for (size_t i = 0; i < args.size(); i++) call += (i ? ", " : "") + args[i].value->to_webcc();
    return call + ")";
}
