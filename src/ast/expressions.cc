#include "expressions.h"
#include "formatter.h"
#include "node.h" 
#include "../defs/def_parser.h"
#include "../codegen/json_codegen.h"
#include <cctype>

// Reference to per-component context for reference props
extern std::set<std::string> g_ref_props;

// Current assignment target (set by Assignment::to_webcc for WebSocket lifetime tracking)
std::string g_ws_assignment_target;

// Helper to expand @inline templates like "${this}.length()" or "${this}.substr(${0}, ${1})"
static std::string expand_inline_template(const std::string& tmpl, const std::string& receiver,
                                          const std::vector<CallArg>& args) {
    std::string result;
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '$' && i + 1 < tmpl.size() && tmpl[i + 1] == '{') {
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
        result += tmpl[i];
    }
    return result;
}

// Helper to generate WebSocket dispatcher registration code
// ws_member is the member variable name (e.g., "ws") for invalidation on close/error
static std::string generate_ws_dispatcher(const std::string& event_type,
                                          const std::string& ws_obj,
                                          const std::string& callback,
                                          const std::string& ws_member = "") {
    int param_count = ComponentTypeContext::instance().get_method_param_count(callback);
    
    if (event_type == "onMessage") {
        // onMessage can accept 0 or 1 (string) parameter
        if (param_count > 0) {
            return "g_ws_message_dispatcher.set(" + ws_obj + ", [this](const webcc::string& msg) { this->" + callback + "(msg); })";
        } else {
            return "g_ws_message_dispatcher.set(" + ws_obj + ", [this](const webcc::string& msg) { this->" + callback + "(); })";
        }
    } else if (event_type == "onOpen") {
        return "g_ws_open_dispatcher.set(" + ws_obj + ", [this]() { this->" + callback + "(); })";
    } else if (event_type == "onClose") {
        // Invalidate the handle after calling the user's callback
        std::string invalidate = ws_member.empty() ? "" : " this->" + ws_member + " = webcc::WebSocket(-1);";
        return "g_ws_close_dispatcher.set(" + ws_obj + ", [this]() { this->" + callback + "();" + invalidate + " })";
    } else if (event_type == "onError") {
        // Invalidate the handle after calling the user's callback
        std::string invalidate = ws_member.empty() ? "" : " this->" + ws_member + " = webcc::WebSocket(-1);";
        return "g_ws_error_dispatcher.set(" + ws_obj + ", [this]() { this->" + callback + "();" + invalidate + " })";
    }
    return "";
}

// Helper to generate intrinsic code
static std::string generate_intrinsic(const std::string& intrinsic_name,
                                      const std::vector<CallArg>& args) {

    if (intrinsic_name == "flush") {
        return "webcc::flush()";
    }                                  
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
    
    // WebSocket.connect with callback arguments
    // Usage: WebSocket.connect("url", msgHandler, openHandler, closeHandler, errorHandler)
    //    or: WebSocket.connect("url", &onMessage = handler, &onOpen = handler, ...)
    if (intrinsic_name == "ws_connect") {
        if (args.empty()) return "";
        
        std::string url = args[0].value->to_webcc();
        std::string ws_member = g_ws_assignment_target;  // Capture the assignment target for invalidation
        std::string code = "[&]() {\n";
        code += "            auto _ws = webcc::websocket::connect(" + url + ");\n";
        
        // Process callback arguments - support both positional and named
        // Positional order: onMessage, onOpen, onClose, onError
        const char* positional_names[] = {"onMessage", "onOpen", "onClose", "onError"};
        for (size_t i = 1; i < args.size(); i++) {
            const auto& arg = args[i];
            std::string callback = arg.value->to_webcc();
            std::string event_name;
            
            if (!arg.name.empty()) {
                event_name = arg.name;
            } else if (i - 1 < 4) {
                event_name = positional_names[i - 1];
            }
            
            std::string dispatcher_code = generate_ws_dispatcher(event_name, "_ws", callback, ws_member);
            if (!dispatcher_code.empty()) {
                code += "            " + dispatcher_code + ";\n";
            }
        }
        
        code += "            return _ws;\n";
        code += "        }()";
        return code;
    }
    
    // FetchRequest.get with callback arguments
    // Usage: FetchRequest.get("url", successHandler, errorHandler)
    //    or: FetchRequest.get("url", &onSuccess = handler, &onError = handler)
    if (intrinsic_name == "fetch_get") {
        if (args.empty()) return "";
        
        std::string url = args[0].value->to_webcc();
        std::string code = "[&]() {\n";
        code += "            auto _req = webcc::fetch::get(" + url + ");\n";
        
        // Process callback arguments - support both positional and named
        for (size_t i = 1; i < args.size(); i++) {
            const auto& arg = args[i];
            std::string callback = arg.value->to_webcc();
            std::string event_name;
            
            if (!arg.name.empty()) {
                event_name = arg.name;
            } else {
                // Positional: arg[1] = onSuccess, arg[2] = onError
                event_name = (i == 1) ? "onSuccess" : "onError";
            }
            
            int param_count = ComponentTypeContext::instance().get_method_param_count(callback);
            if (event_name == "onSuccess") {
                // onSuccess can accept 0 or 1 (string) parameter
                if (param_count > 0) {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string& data) { this->" + callback + "(data); });\n";
                } else {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string& data) { this->" + callback + "(); });\n";
                }
            } else if (event_name == "onError") {
                // onError can accept 0 or 1 (string) parameter
                if (param_count > 0) {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string& error) { this->" + callback + "(error); });\n";
                } else {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string& error) { this->" + callback + "(); });\n";
                }
            }
        }
        
        code += "            return _req;\n";
        code += "        }()";
        return code;
    }
    
    // FetchRequest.post with callback arguments
    // Usage: FetchRequest.post("url", "body", successHandler, errorHandler)
    //    or: FetchRequest.post("url", "body", &onSuccess = handler, &onError = handler)
    if (intrinsic_name == "fetch_post") {
        if (args.size() < 2) return "";
        
        std::string url = args[0].value->to_webcc();
        std::string body = args[1].value->to_webcc();
        std::string code = "[&]() {\n";
        code += "            auto _req = webcc::fetch::post(" + url + ", " + body + ");\n";
        
        // Process callback arguments - support both positional and named
        for (size_t i = 2; i < args.size(); i++) {
            const auto& arg = args[i];
            std::string callback = arg.value->to_webcc();
            std::string event_name;
            
            if (!arg.name.empty()) {
                event_name = arg.name;
            } else {
                // Positional: arg[2] = onSuccess, arg[3] = onError
                event_name = (i == 2) ? "onSuccess" : "onError";
            }
            
            int param_count = ComponentTypeContext::instance().get_method_param_count(callback);
            if (event_name == "onSuccess") {
                // onSuccess can accept 0 or 1 (string) parameter
                if (param_count > 0) {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string& data) { this->" + callback + "(data); });\n";
                } else {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string& data) { this->" + callback + "(); });\n";
                }
            } else if (event_name == "onError") {
                // onError can accept 0 or 1 (string) parameter
                if (param_count > 0) {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string& error) { this->" + callback + "(error); });\n";
                } else {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string& error) { this->" + callback + "(); });\n";
                }
            }
        }
        
        code += "            return _req;\n";
        code += "        }()";
        return code;
    }
    
    // Json.parse - supports both positional and named callback arguments
    // Usage: Json.parse(User, jsonStr, successHandler, errorHandler)
    //    or: Json.parse(User, jsonStr, &onSuccess = handler, &onError = errorHandler)
    if (intrinsic_name == "json_parse") {
        if (args.size() < 2) return "";
        
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
        
        // Find callbacks - support both positional and named arguments
        std::string on_success, on_error;
        for (size_t i = 2; i < args.size(); i++) {
            const auto& arg = args[i];
            if (!arg.name.empty()) {
                // Named argument
                if (arg.name == "onSuccess") {
                    on_success = arg.value->to_webcc();
                } else if (arg.name == "onError") {
                    on_error = arg.value->to_webcc();
                }
            } else {
                // Positional argument: arg[2] = onSuccess, arg[3] = onError
                if (i == 2) {
                    on_success = arg.value->to_webcc();
                } else if (i == 3) {
                    on_error = arg.value->to_webcc();
                }
            }
        }
        
        return generate_json_parse(data_type, json_expr, on_success, on_error);
    }
    
    return "";  // Unknown intrinsic
}

std::string IntLiteral::to_webcc() { return std::to_string(value); }

std::string FloatLiteral::to_webcc() {
    std::string s = std::to_string(value);
    if(s.find('.') != std::string::npos){
        s = s.substr(0, s.find_last_not_of('0')+1);
        if(s.back() == '.') s += "0";
    }
    return s;  // No 'f' suffix - using double (64-bit)
}

std::vector<StringLiteral::Part> StringLiteral::parse() {
    std::vector<Part> parts;
    std::string current;
    for(size_t i=0; i<value.length(); ++i) {
        if(value[i] == '\\' && i + 1 < value.length() && (value[i+1] == '{' || value[i+1] == '}')) {
            current += value[i+1];
            i++;
        } else if(value[i] == '{') {
            // Look ahead to find closing brace
            size_t close_pos = value.find('}', i + 1);
            if (close_pos == std::string::npos) {
                // No closing brace found - treat as literal
                current += '{';
            } else {
                // Check if content looks like a valid expression (starts with letter/underscore)
                // This allows JSON-like content with braces to pass through as literals
                char first_char = (i + 1 < value.length()) ? value[i + 1] : '\0';
                bool looks_like_expr = (first_char >= 'a' && first_char <= 'z') ||
                                       (first_char >= 'A' && first_char <= 'Z') ||
                                       first_char == '_';
                
                if (!looks_like_expr) {
                    // Not an expression - treat braces as literal
                    current += '{';
                } else {
                    // Found closing brace - extract expression
                    if(!current.empty()) parts.push_back({false, current});
                    current = "";
                    i++;
                    while(i < value.length() && value[i] != '}') {
                        current += value[i];
                        i++;
                    }
                    // Only treat as expression if content is non-empty
                    if (!current.empty()) {
                        parts.push_back({true, current});
                    } else {
                        // Empty braces {} - treat as literal
                        parts.push_back({false, "{}"});
                    }
                    current = "";
                }
            }
        } else {
            current += value[i];
        }
    }
    if(!current.empty()) parts.push_back({false, current});
    return parts;
}

std::string StringLiteral::to_webcc() {
    auto parts = parse();
    if(parts.empty()) return "\"\"";
    bool has_expr = false;
    for(auto& p : parts) if(p.is_expr) has_expr = true;

    if(!has_expr) {
        std::string content;
        for(auto& p : parts) content += p.content;

        std::string escaped;
        for(char c : content) {
            if(c == '"') escaped += "\\\"";
            else if(c == '\\') escaped += "\\\\";
            else if(c == '\n') escaped += "\\n";
            else if(c == '\t') escaped += "\\t";
            else escaped += c;
        }
        return "\"" + escaped + "\"";
    }

    std::string code = "webcc::string::concat(";
    for(size_t i=0; i<parts.size(); ++i) {
        if(i > 0) code += ", ";
        if(parts[i].is_expr) {
            code += parts[i].content;
        } else {
            std::string escaped;
            for(char c : parts[i].content) {
                if(c == '"') escaped += "\\\"";
                else if(c == '\\') escaped += "\\\\";
                else if(c == '\n') escaped += "\\n";
                else if(c == '\t') escaped += "\\t";
                else escaped += c;
            }
            code += "\"" + escaped + "\"";
        }
    }
    code += ")";
    return code;
}

bool StringLiteral::is_static() {
    auto parts = parse();
    for(auto& p : parts) if(p.is_expr) return false;
    return true;
}

void StringLiteral::collect_dependencies(std::set<std::string>& deps) {
    auto parts = parse();
    for(auto& p : parts) {
        if(p.is_expr) {
            std::string expr = p.content;
            std::string id;
            for(char c : expr) {
                if(isalnum(c) || c == '_') {
                    id += c;
                } else {
                    if(!id.empty()) {
                        if(!isdigit(id[0])) deps.insert(id);
                        id = "";
                    }
                }
            }
            if(!id.empty()) {
                if(!isdigit(id[0])) deps.insert(id);
            }
        }
    }
}

void StringLiteral::collect_member_dependencies(std::set<MemberDependency>& member_deps) {
    auto parts = parse();
    for(auto& p : parts) {
        if(p.is_expr) {
            // Parse expressions like "pos.x" to extract object.member pairs
            std::string expr = p.content;
            size_t pos = 0;
            while (pos < expr.length()) {
                // Skip non-identifier chars
                while (pos < expr.length() && !isalnum(expr[pos]) && expr[pos] != '_') pos++;
                if (pos >= expr.length()) break;

                // Extract identifier
                std::string obj;
                while (pos < expr.length() && (isalnum(expr[pos]) || expr[pos] == '_')) {
                    obj += expr[pos++];
                }

                // Check if followed by '.'
                if (pos < expr.length() && expr[pos] == '.') {
                    pos++; // skip '.'
                    std::string member;
                    while (pos < expr.length() && (isalnum(expr[pos]) || expr[pos] == '_')) {
                        member += expr[pos++];
                    }
                    if (!obj.empty() && !member.empty() && !isdigit(obj[0])) {
                        member_deps.insert({obj, member});
                    }
                }
            }
        }
    }
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
    return left->to_webcc() + " " + op + " " + right->to_webcc();
}

void BinaryOp::collect_dependencies(std::set<std::string>& deps) {
    left->collect_dependencies(deps);
    right->collect_dependencies(deps);
}

std::string FunctionCall::args_to_string() {
    if (args.empty()) return "\"\"";

    std::string result = "webcc::string::concat(";
    for(size_t i = 0; i < args.size(); i++){
        if(i > 0) result += ", ";
        result += args[i].value->to_webcc();
    }
    result += ")";
    return result;
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

    // Try DefSchema lookup first (handles @intrinsic, @inline, @map)
    if (!type_or_obj.empty()) {
        // Check for static type call (e.g., System.random, Input.isKeyDown)
        if (std::isupper(type_or_obj[0])) {
            if (auto* method_def = DefSchema::instance().lookup_method(type_or_obj, method)) {
                // Check arg count matches (for overloads)
                if (method_def->params.size() == args.size()) {
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
                    }
                }
            }
        }

        // Check for builtin type instance methods (string, array)
        // For string methods, we need to check against the "string" type
        // Use arg_count to find the correct overload
        if (auto* method_def = DefSchema::instance().lookup_method("string", method, args.size())) {
            if (method_def->mapping_type == MappingType::Inline) {
                return expand_inline_template(method_def->mapping_value, type_or_obj, args);
            }
        }

        // Check array methods
        if (auto* method_def = DefSchema::instance().lookup_method("array", method, args.size())) {
            if (method_def->mapping_type == MappingType::Inline) {
                return expand_inline_template(method_def->mapping_value, type_or_obj, args);
            }
        }
    }

    // Handle Enum.size() - special case not in def files
    if (!type_or_obj.empty() && method == "size" && args.size() == 0 && std::isupper(type_or_obj[0])) {
        size_t first_dot = type_or_obj.find('.');
        if (first_dot != std::string::npos) {
            std::string comp = type_or_obj.substr(0, first_dot);
            std::string enum_name = type_or_obj.substr(first_dot + 1);
            return "static_cast<int>(" + comp + "::" + enum_name + "::_COUNT)";
        }
        return "static_cast<int>(" + type_or_obj + "::_COUNT)";
    }

    // DefSchema-based transformation for @map methods (webcc API calls)
    std::string obj_arg = "";
    bool pass_obj = false;
    const MethodDef* map_method = nullptr;
    std::string map_ns = "";
    std::string map_func = "";

    if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < name.length() - 1) {
        std::string obj = name.substr(0, dot_pos);
        std::string method_name = name.substr(dot_pos + 1);

        // Check if obj is a type name (static call) or instance
        bool is_static_call = !obj.empty() && std::isupper(obj[0]);

        if (is_static_call) {
            // Static call: Type.method() - look up directly
            map_method = DefSchema::instance().lookup_method(obj, method_name);
        } else {
            // Instance call: obj.method() - need to find type
            // Try common handle types that have instance methods
            for (const auto& [type_name, type_def] : DefSchema::instance().types()) {
                if (!type_def.is_builtin && !type_def.methods.empty()) {
                    for (const auto& m : type_def.methods) {
                        if (m.name == method_name && !m.is_shared) {
                            if (m.mapping_type == MappingType::Map) {
                                map_method = &m;
                                pass_obj = true;
                                obj_arg = obj;
                                break;
                            }
                        }
                    }
                    if (map_method) break;
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
            code += args[i].value->to_webcc();
            first_arg = false;
        }
        code += ")";

        // Check return type from method definition
        if (map_method->return_type == "int") {
            code = "(int32_t)(" + code + ")";
        }

        return code;
    }

    std::string result = name + "(";
    for(size_t i = 0; i < args.size(); i++){
        if(i > 0) result += ", ";
        result += args[i].value->to_webcc();
    }
    result += ")";
    return result;
}

void FunctionCall::collect_dependencies(std::set<std::string>& deps) {
    size_t dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        deps.insert(name.substr(0, dot_pos));
    }
    for(auto& arg : args) arg.value->collect_dependencies(deps);
}

MemberAccess::MemberAccess(std::unique_ptr<Expression> obj, const std::string& mem)
    : object(std::move(obj)), member(mem) {}

std::string MemberAccess::to_webcc() {
    return object->to_webcc() + "." + member;
}

void MemberAccess::collect_dependencies(std::set<std::string>& deps) {
    object->collect_dependencies(deps);
}

void MemberAccess::collect_member_dependencies(std::set<MemberDependency>& member_deps) {
    if (auto id = dynamic_cast<Identifier*>(object.get())) {
        member_deps.insert({id->name, member});
    }
    object->collect_member_dependencies(member_deps);
}

PostfixOp::PostfixOp(std::unique_ptr<Expression> expr, const std::string& o)
    : operand(std::move(expr)), op(o) {}

std::string PostfixOp::to_webcc() {
    return operand->to_webcc() + op;
}

void PostfixOp::collect_dependencies(std::set<std::string>& deps) {
    operand->collect_dependencies(deps);
}

UnaryOp::UnaryOp(const std::string& o, std::unique_ptr<Expression> expr)
    : op(o), operand(std::move(expr)) {}

std::string UnaryOp::to_webcc() {
    return op + operand->to_webcc();
}

void UnaryOp::collect_dependencies(std::set<std::string>& deps) {
    operand->collect_dependencies(deps);
}

bool UnaryOp::is_static() { return operand->is_static(); }

// ReferenceExpression - pass by reference (borrow, no ownership transfer)
std::string ReferenceExpression::to_webcc() {
    return operand->to_webcc();  // References are handled at call sites
}

void ReferenceExpression::collect_dependencies(std::set<std::string>& deps) {
    operand->collect_dependencies(deps);
}

// MoveExpression - generates webcc::move() for explicit ownership transfer
std::string MoveExpression::to_webcc() {
    return "webcc::move(" + operand->to_webcc() + ")";
}

void MoveExpression::collect_dependencies(std::set<std::string>& deps) {
    operand->collect_dependencies(deps);
}

TernaryOp::TernaryOp(std::unique_ptr<Expression> cond, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f)
    : condition(std::move(cond)), true_expr(std::move(t)), false_expr(std::move(f)) {}

std::string TernaryOp::to_webcc() {
    return "(" + condition->to_webcc() + " ? " + true_expr->to_webcc() + " : " + false_expr->to_webcc() + ")";
}

void TernaryOp::collect_dependencies(std::set<std::string>& deps) {
    condition->collect_dependencies(deps);
    true_expr->collect_dependencies(deps);
    false_expr->collect_dependencies(deps);
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

void ArrayLiteral::collect_dependencies(std::set<std::string>& deps) {
    for (auto& elem : elements) elem->collect_dependencies(deps);
}

bool ArrayLiteral::is_static() {
    for (auto& elem : elements) if (!elem->is_static()) return false;
    return true;
}

std::string ArrayRepeatLiteral::to_webcc() {
    std::string val = value->to_webcc();
    std::string code = "{";
    for (int i = 0; i < count; ++i) {
        if (i > 0) code += ", ";
        code += val;
    }
    code += "}";
    return code;
}

void ArrayRepeatLiteral::collect_dependencies(std::set<std::string>& deps) {
    value->collect_dependencies(deps);
}

bool ArrayRepeatLiteral::is_static() {
    return value->is_static();
}

IndexAccess::IndexAccess(std::unique_ptr<Expression> arr, std::unique_ptr<Expression> idx)
    : array(std::move(arr)), index(std::move(idx)) {}

std::string IndexAccess::to_webcc() {
    return array->to_webcc() + "[" + index->to_webcc() + "]";
}

void IndexAccess::collect_dependencies(std::set<std::string>& deps) {
    array->collect_dependencies(deps);
    index->collect_dependencies(deps);
}

std::string EnumAccess::to_webcc() {
    return enum_name + "::" + value_name;
}

std::string ComponentConstruction::to_webcc() {
    std::string result = component_name + "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) result += ", ";
        result += args[i].value->to_webcc();
    }
    result += ")";
    return result;
}

void ComponentConstruction::collect_dependencies(std::set<std::string>& deps) {
    for (auto& arg : args) arg.value->collect_dependencies(deps);
}
