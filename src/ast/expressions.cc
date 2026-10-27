#include "expressions.h"
#include "../codegen/codegen_utils.h"
#include "codegen_state.h"
#include "formatter.h"
#include "node.h" 
#include "../defs/def_parser.h"
#include "../codegen/json_codegen.h"
#include "../cli/error.h"
#include "../analysis/type_checker.h"
#include <cctype>
#include <algorithm>

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

// an expression's C++; a bare brace list the checker typed gets its vector type, so it can be
// passed, stringified or called on
static std::string typed_literal(Expression* e) {
    std::string code = e->to_webcc();
    auto* arr = dynamic_cast<ArrayLiteral*>(e);
    if (arr && !arr->element_type.empty())
        return convert_type(arr->element_type + "[]") + code;
    return code;
}

// b => ...: an array method given an inline function uses its ...Where form (defs/core/array.d.coi)
static bool has_inline_function(const std::vector<CallArg>& args) {
    return args.size() == 1 && dynamic_cast<LambdaExpr*>(args[0].value.get()) != nullptr;
}
static std::string predicate_method(const std::string& method, const std::vector<CallArg>& args) {
    if (has_inline_function(args) && (method == "indexOf" || method == "contains" || method == "remove"))
        return method + "Where";
    return method;
}


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
    if (intrinsic_name == "is_prerender" && args.empty()) {
        return "(webcc::system::is_prerender() != 0)";
    }
    
    if (intrinsic_name == "json_stringify" && args.size() == 1)
        return "__coi_json_stringify(" + typed_literal(args[0].value.get()) + ")";

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
        // another module's pod: Geo::Point is Geo_Point in C++
        for (size_t dc; (dc = data_type.find("::")) != std::string::npos;)
            data_type.replace(dc, 2, "_");
        
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

std::vector<StringLiteral::Part> StringLiteral::split(const std::string& value, bool is_template) {
    std::vector<Part> parts;
    std::string text;
    for (size_t i = 0; i < value.size(); ++i) {
        // \x1d: an escaped $ from the lexer ("..." strings only)
        if (!is_template && value[i] == '\x1d') {
            text += '$';
            continue;
        }
        if (is_template && value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '$') {
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

std::string Identifier::raw_name() { return name; }
std::string MemberAccess::raw_name() { return object->raw_name() + "." + member; }
std::string IndexAccess::raw_name() { return array->raw_name() + "[" + index->to_webcc() + "]"; }

std::string Identifier::to_webcc() {
    if(g_ref_props.count(name)) {
        return "(*" + cpp_name(name) + ")";
    }
    return cpp_name(name);
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
    if (op == "==" || op == "!=")
        return "(" + comparison_side(left.get()) + " " + op + " " + comparison_side(right.get()) + ")";
    return "(" + left->to_webcc() + " " + op + " " + right->to_webcc() + ")";
}

// a side of == or !=: an array literal is a brace list in C++, which can't be compared until it
// names its vector type
std::string comparison_side(Expression *e) {
    auto *lit = dynamic_cast<ArrayLiteral *>(e);
    if (lit && !lit->element_type.empty())
        return convert_type(lit->element_type + "[]") + lit->to_webcc();
    return e->to_webcc();
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
        return convert_type(coi_type) + code;
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

// the event as a handler's arguments from its param `skip` on: the event's pod when that one
// param takes it, else its fields in order, as many as the params left (fewer is fine)
std::string event_call_args(const MethodDef& event_method, const std::string& handle_type,
                            const std::string& callback, size_t skip, const std::string& evt) {
    SchemaEventSpec spec = SchemaEventSpec::parse(event_method.mapping_value);
    std::vector<std::string> field_types;
    for (const auto& p : event_method.params)
        if (is_callback_param(p)) field_types = callback_param_types(p.type);
    auto* sig = ComponentTypeContext::instance().get_method_signature(callback);
    int n = sig ? (int)sig->param_types.size() - (int)skip : ComponentTypeContext::instance().get_method_param_count(callback);
    if (n < 0 || n > (int)spec.fields.size()) n = (int)spec.fields.size();
    bool as_pod = !spec.pod.empty() && sig && sig->param_types.size() == skip + 1 &&
                  DefSchema::instance().resolve_alias(sig->param_types[skip]) == spec.pod;
    if (as_pod) n = (int)spec.fields.size();
    std::string code;
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
    std::string full = "webcc::" + spec.struct_name();
    g_used_events[spec.ns + "::" + spec.name] = {spec.ns, spec.name, spec.key, full, handle_type, spec.last};
    return code;
}

// handler may take fewer params than the event has fields
std::string generate_event_call(const MethodDef& event_method, const std::string& handle_type,
                                const std::string& callback, const std::string& evt) {
    return "this->" + cpp_name(callback) + "(" + event_call_args(event_method, handle_type, callback, 0, evt) + ")";
}

// page-wide event, turned on with the +listen values
static std::string generate_page_event_registration(const MethodDef& event_method, const CallArg& arg) {
    SchemaEventSpec spec = SchemaEventSpec::parse(event_method.mapping_value);
    std::string full = "webcc::" + spec.struct_name();
    std::string call = generate_event_call(event_method, "", arg.value->raw_name(), "_e");
    std::string listen;
    if (!spec.listen.empty()) {
        listen = "webcc::" + spec.ns + "::" + spec.listen + "(";
        for (size_t i = 0; i < spec.listen_params.size(); i++) {
            const auto& lp = spec.listen_params[i];
            listen += (i ? ", " : "") + to_webcc_arg(lp.value, lp.type);
        }
        listen += "), ";
    }
    return "(" + listen + "coi_events<" + full + ">.set(_page_key(), [this](const " + full +
           "& _e) { (void)_e; " + call + "; }, this))";
}

// coi_events<E>.set(handle, [this](const E& _e) { this->handler(...); }, this)
static std::string generate_event_registration(const MethodDef& event_method, const std::string& handle_type,
                                               const std::string& handle_expr, const CallArg& arg) {
    SchemaEventSpec spec = SchemaEventSpec::parse(event_method.mapping_value);
    std::string full = "webcc::" + spec.struct_name();
    std::string call = generate_event_call(event_method, handle_type, arg.value->raw_name(), "_e");
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


// a method called on a receiver whose type the codegen doesn't know (a match binding, a
// field path): the compiler's own if some def type has it, the user's otherwise
static bool def_method_named(const std::string& method) {
    const auto& defs = DefSchema::instance();
    return defs.lookup_method("string", method) || defs.lookup_method("array", method) || defs.lookup_method("Meta", method) ||
           defs.lookup_func(DefSchema::to_snake_case(method));
}

std::string FunctionCall::to_webcc() {
    // mount(Counter()) in a test: the component on the heap, it stays where the page's handlers point
    if (name == "mount" && !g_test_file.empty() && args.size() == 1 && FreeFunctionRegistry::instance().resolve_call("mount").empty())
        if (auto *cc = dynamic_cast<ComponentConstruction *>(args[0].value.get())) {
            std::string t = cc->cpp_type();
            return "coi_test_mount(new (webcc::malloc(sizeof(" + t + "))) " + t + "(" + cc->to_webcc() + "))";
        }
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

    resolved_receiver = mangle_path(resolved_receiver);

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
        // a known component or pod receiver has its own methods, not string's or array's; so does
        // a type called by name (Storage.clear() is Storage's, not an array's clear)
        bool type_by_name = receiver_type.empty() && std::isupper(static_cast<unsigned char>(type_or_obj[0])) &&
                            DefSchema::instance().lookup_type(type_or_obj) != nullptr;
        bool builtin_receiver = !type_by_name && (receiver_type.empty() || receiver_type == "string" || receiver_type.back() == ']');
        if (!handle_receiver && builtin_receiver) {
            // string methods, unless the receiver is an array (both have append)
            bool array_receiver = !receiver_type.empty() && receiver_type.back() == ']';
            if (!array_receiver && !has_inline_function(args))
                if (auto* method_def = DefSchema::instance().lookup_method("string", method, args.size())) {
                    if (method_def->mapping_type == MappingType::Inline) {
                        return expand_inline_template(method_def->mapping_value, resolved_receiver, args);
                    }
                }

            // Check array methods (an inline function argument picks the ...Where form)
            if (auto* method_def = DefSchema::instance().lookup_method("array", predicate_method(method, args), args.size())) {
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
                            arg_code += ") { this->" + cpp_name(id->name) + "(";
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

    std::string call_name = name.find('.') == std::string::npos && name.find("::") == std::string::npos ? cpp_name(name) : name;
    std::string free_fn = FreeFunctionRegistry::instance().resolve_call(name);
    if (!free_fn.empty())
    {
        call_name = free_fn;
    }
    else if (!type_or_obj.empty())
    {
        // the user's own method gets the C++ name; a string, array, map, handle or def type keeps its own
        std::string rt = DefSchema::instance().resolve_alias(ComponentTypeContext::instance().get_symbol_type(type_or_obj));
        bool meta_type = rt.size() > 4 && rt.compare(rt.size() - 4, 4, "Meta") == 0;
        bool builtin_method = rt.empty() || rt == "unknown"
                                  ? def_method_named(method)
                                  : (rt == "string" || rt.back() == ']' || meta_type || DefSchema::instance().is_handle(rt) || DefSchema::instance().lookup_type(rt));
        call_name = resolved_receiver + "." + (builtin_method ? method : cpp_name(method));
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
                    arg_code += ") { this->" + cpp_name(id->name) + "(";
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
    return object->to_webcc() + "." + cpp_name(member);
}

void FunctionCall::collect_member_dependencies(std::set<MemberDependency>& member_deps) {
    if (auto it = g_method_member_reads.find(name); it != g_method_member_reads.end())
        member_deps.insert(it->second.begin(), it->second.end());
    if (auto it = g_child_method_reads.find(name); it != g_child_method_reads.end())
        member_deps.insert(it->second.begin(), it->second.end());
    for (auto* child : get_children()) {
        if (child) child->collect_member_dependencies(member_deps);
    }
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
            result += ") { this->" + cpp_name(method_name) + "(";
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
    return "(" + condition->to_webcc() + " ? " + typed_literal(true_expr.get()) + " : " + typed_literal(false_expr.get()) + ")";
}

bool TernaryOp::is_static() {
    return condition->is_static() && true_expr->is_static() && false_expr->is_static();
}

std::string ArrayLiteral::to_webcc() {
    // [[1.0, 2.0], ...]: an inner brace list names its vector type, a bare one can't be deduced
    std::string inner = element_type.size() > 2 && element_type.ends_with("[]") ? element_type.substr(0, element_type.size() - 2) : "";
    std::string code = "{";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) code += ", ";
        auto *nested = dynamic_cast<ArrayLiteral *>(elements[i].get());
        if (nested && !inner.empty()) {
            if (nested->element_type.empty()) nested->element_type = inner;
            code += convert_type(inner + "[]");
        }
        code += elements[i]->to_webcc();
    }
    code += "}";
    if (named && !element_type.empty()) code = convert_type(element_type + "[]") + code;
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
    if (enum_name.find("::") != std::string::npos)
        return convert_type(enum_name) + "::" + value_name;
    return ComponentTypeContext::instance().resolve(enum_name) + "::" + value_name;
}

std::string ComponentConstruction::cpp_type() {
    // Resolve component-local data types (e.g., Body -> App_Body)
    const auto &ctx = ComponentTypeContext::instance();
    std::string resolved_name = ctx.resolve(component_name);
    if (resolved_name == component_name &&
        component_name.find("::") == std::string::npos &&
        !component_name.empty() &&
        std::isupper(component_name[0])) {
        const std::string &current_component = ctx.component_name;
        size_t module_sep = current_component.find('_');
        if (module_sep != std::string::npos) {
            std::string module_name = current_component.substr(0, module_sep);
            std::string qualified_ctor = module_name + "::" + component_name;
            resolved_name = convert_type(qualified_ctor);
        }
        // a module's component built in a top-level function
        else if (current_component.empty() && !ctx.module_name.empty() && is_component_name(ctx.module_name + "_" + component_name)) {
            resolved_name = ctx.module_name + "_" + component_name;
        }
    }
    // Explicit namespaced constructors (e.g., Pdf::Writer(...))
    // must be lowered to C++ type names (Pdf_Writer(...)).
    if (resolved_name.find("::") != std::string::npos) {
        resolved_name = convert_type(resolved_name);
    }
    return resolved_name;
}

std::string ComponentConstruction::to_webcc() {
    // {} for a map or an array: an empty one
    if (component_name.ends_with("]"))
        return convert_type(component_name) + "{}";
    std::string resolved_name = cpp_type();
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
                    result += (first ? "." : ", .") + cpp_name(field.name) + " = " + value;
                    first = false;
                }
            }
            return result + "}";
        }
    }

    auto arg_code = [&](size_t i) {
        std::string value = args[i].value->to_webcc();
        auto* id = dynamic_cast<Identifier*>(args[i].value.get());
        const auto& sigs = ComponentTypeContext::instance().method_signatures;
        std::string free_fn = id ? FreeFunctionRegistry::instance().resolve_call(id->name) : "";
        if (args[i].is_reference && id && (sigs.count(id->name) || !free_fn.empty())) {
            // &method as a callback: Box(1, &changed)
            if (!sigs.count(id->name))
                return "[](auto&&... _a) { return " + free_fn + "(_a...); }";
            std::string params, fwd;
            const auto& types = sigs.at(id->name).param_types;
            for (size_t k = 0; k < types.size(); k++) {
                params += (k ? ", " : "") + convert_type(types[k]) + " _a" + std::to_string(k);
                fwd += (k ? ", _a" : "_a") + std::to_string(k);
            }
            return "[this](" + params + ") { return this->" + cpp_name(id->name) + "(" + fwd + "); }";
        }
        if (args[i].is_reference) return "&(" + value + ")";
        if (args[i].is_move) return "coi::move(" + value + ")";
        return value;
    };

    // a component with props by name: each set by name, in the order the props are declared
    if (any_named && arg_props.size() == args.size()) {
        std::vector<size_t> order;
        for (size_t i = 0; i < args.size(); i++) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return arg_props[a] < arg_props[b]; });
        std::string result = resolved_name + "{";
        for (size_t k = 0; k < order.size(); k++)
            result += (k ? ", ." : ".") + cpp_name(props[arg_props[order[k]]]) + " = " + arg_code(order[k]);
        return result + "}";
    }

    std::string result = resolved_name + "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) result += ", ";
        result += arg_code(i);
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
                    conditions.push_back("_match_subject." + cpp_name(field.name) + " == " + field.value->to_webcc());
                } else {
                    // Binding pattern: capture the field into a local variable
                    bindings += "            const auto& " + cpp_name(field.name) + " = _match_subject." + cpp_name(field.name) + ";\n";
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
                    bindings += "            const auto& " + cpp_name(arm.pattern.variant_bindings[i].name) +
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

// a generic lambda: the element type is whatever the array holds, the loop is in the helper
// (coi_index_where and friends in the generated prelude)
std::string LambdaExpr::to_webcc() {
    return "[&](const auto& " + cpp_name(param) + ") { return (" + body->to_webcc() + "); }";
}

// what the body reads from outside, which is what a view binding using it depends on
void LambdaExpr::collect_dependencies(std::set<std::string>& deps) {
    std::set<std::string> inner;
    body->collect_dependencies(inner);
    inner.erase(param);
    deps.insert(inner.begin(), inner.end());
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

// what an expression the checker left untyped returns, as far as the defs tell: a literal,
// Type.method(...), or a method on one of those (xs.size() in a view, whose receiver is a
// field: an array's or a string's method, the same fallback to_webcc takes). "" when unknown
static std::string untyped_result(Expression* e) {
    if (dynamic_cast<StringLiteral*>(e)) return "string";
    if (dynamic_cast<IntLiteral*>(e)) return "int32";
    if (dynamic_cast<FloatLiteral*>(e)) return "float64";
    if (dynamic_cast<BoolLiteral*>(e)) return "bool";
    if (auto* call = dynamic_cast<FunctionCall*>(e)) {
        size_t dot = call->name.rfind('.');
        if (dot == std::string::npos) return "";
        std::string owner = call->name.substr(0, dot), method = call->name.substr(dot + 1);
        // xs.size() with xs a field of the component: the field's type owns the method
        std::string field = call->receiver_type.empty() ? ComponentTypeContext::instance().get_symbol_type(owner) : call->receiver_type;
        if (!field.empty() && field != "unknown")
            owner = field.back() == ']' ? "array" : DefSchema::instance().resolve_alias(field);
        const auto* m = DefSchema::instance().lookup_method(owner, method, call->args.size());
        if (!m) m = DefSchema::instance().lookup_method(owner, method);
        if (!m) return "";
        const std::string& rt = m->return_type;
        return rt == "T" || rt.rfind("T[", 0) == 0 ? "" : rt;
    }
    if (auto* mc = dynamic_cast<MethodCall*>(e)) {
        std::string t = mc->receiver_type == "unknown" ? "" : mc->receiver_type;
        if (t.empty()) t = untyped_result(mc->receiver.get());
        const auto* m = DefSchema::instance().lookup_method("", "", 0);
        if (!t.empty()) m = DefSchema::instance().lookup_method(t.back() == ']' ? "array" : DefSchema::instance().resolve_alias(t), mc->method, mc->args.size());
        else {
            m = DefSchema::instance().lookup_method("string", mc->method, mc->args.size());
            if (!m) m = DefSchema::instance().lookup_method("array", mc->method, mc->args.size());
        }
        if (!m) return "";
        // an array's element type in its return (T, T[]) isn't known here
        const std::string& rt = m->return_type;
        return rt == "T" || rt.rfind("T[", 0) == 0 ? "" : rt;
    }
    return "";
}

std::string MethodCall::to_webcc() {
    std::string type = receiver_type == "unknown" ? "" : receiver_type;
    // System.getDateNow().toInt64() or xs.size().toInt64() in a view: the checker didn't type it, the defs say
    if (type.empty()) type = untyped_result(receiver.get());
    std::string def_type = !type.empty() && type.back() == ']' ? "array" : DefSchema::instance().resolve_alias(type);
    std::string recv = typed_literal(receiver.get());
    if (dynamic_cast<StringLiteral*>(receiver.get()) && receiver->is_static())
        recv = "coi::string(" + recv + ")";
    bool inline_fn = has_inline_function(args);
    std::string want = predicate_method(method, args);
    const auto* m = DefSchema::instance().lookup_method(inline_fn ? "array" : def_type, want, args.size());
    if (!m && (type.empty() || type == "unknown")) {
        if (!inline_fn) m = DefSchema::instance().lookup_method("string", method, args.size());
        if (!m) m = DefSchema::instance().lookup_method("array", want, args.size());
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
    // a pod's generated <Pod>Meta struct counts as a def type: its has() and has_<field>() are the compiler's
    bool meta_type = type.size() > 4 && type.compare(type.size() - 4, 4, "Meta") == 0;
    bool builtin_method = type.empty() || type == "unknown"
                              ? def_method_named(method)
                              : (type == "string" || type.back() == ']' || meta_type || DefSchema::instance().is_handle(def_type) || DefSchema::instance().lookup_type(def_type));
    std::string call = "(" + recv + ")." + (builtin_method ? method : cpp_name(method)) + "(";
    for (size_t i = 0; i < args.size(); i++) call += (i ? ", " : "") + args[i].value->to_webcc();
    return call + ")";
}
