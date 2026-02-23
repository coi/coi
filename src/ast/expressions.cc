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

// Helper to expand @inline templates with raw string arguments (for string template embedded expressions)
static std::string expand_inline_template_raw(const std::string& tmpl, const std::string& receiver,
                                              const std::vector<std::string>& raw_args) {
    std::string result;
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '$' && i + 1 < tmpl.size()) {
            if (tmpl[i + 1] == '{') {
                size_t end = tmpl.find('}', i + 2);
                if (end != std::string::npos) {
                    std::string var = tmpl.substr(i + 2, end - i - 2);
                    if (var == "this") {
                        result += receiver;
                    } else {
                        int idx = std::stoi(var);
                        if (idx >= 0 && idx < (int)raw_args.size()) {
                            result += raw_args[idx];
                        }
                    }
                    i = end;
                    continue;
                }
            } else if (tmpl.substr(i, 5) == "$self") {
                // Handle $self (used in WebSocket @inline)
                result += receiver;
                i += 4;  // Skip "self" ($ already consumed by i+=1 after continue isn't called)
                continue;
            }
        }
        result += tmpl[i];
    }
    return result;
}

// Forward declaration for transform_embedded_expression
static std::string transform_embedded_expression(const std::string& expr);

// Helper to parse raw argument strings from a method call expression
// e.g., "arg1, arg2" -> ["arg1", "arg2"]
static std::vector<std::string> parse_raw_args(const std::string& args_str) {
    std::vector<std::string> result;
    int depth = 0;
    std::string current;
    for (size_t i = 0; i < args_str.size(); ++i) {
        char c = args_str[i];
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        else if (c == ',' && depth == 0) {
            // Trim whitespace
            size_t start = current.find_first_not_of(" \t");
            size_t end = current.find_last_not_of(" \t");
            if (start != std::string::npos) {
                result.push_back(current.substr(start, end - start + 1));
            }
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) {
        size_t start = current.find_first_not_of(" \t");
        size_t end = current.find_last_not_of(" \t");
        if (start != std::string::npos) {
            result.push_back(current.substr(start, end - start + 1));
        }
    }
    // Recursively transform nested expressions in arguments
    for (auto& arg : result) {
        arg = transform_embedded_expression(arg);
    }
    return result;
}

// Transform a raw expression string by applying DefSchema @inline templates
// This handles method calls like "obj.method(args)" embedded in string templates
static std::string transform_embedded_expression(const std::string& expr) {
    // Find the last dot before the opening paren (for method call)
    size_t paren_pos = expr.find('(');
    if (paren_pos == std::string::npos) {
        // No method call, return as-is
        return expr;
    }
    
    // Find the matching closing paren
    int depth = 1;
    size_t close_paren = paren_pos + 1;
    while (close_paren < expr.size() && depth > 0) {
        if (expr[close_paren] == '(') depth++;
        else if (expr[close_paren] == ')') depth--;
        close_paren++;
    }
    if (depth != 0) return expr;  // Unbalanced parens
    close_paren--;  // Point to the actual close paren
    
    // Find the dot before the method name
    size_t dot_pos = expr.rfind('.', paren_pos);
    if (dot_pos == std::string::npos || dot_pos == 0) {
        return expr;  // No object.method pattern
    }
    
    std::string obj = expr.substr(0, dot_pos);
    std::string method = expr.substr(dot_pos + 1, paren_pos - dot_pos - 1);
    std::string args_str = expr.substr(paren_pos + 1, close_paren - paren_pos - 1);
    std::string suffix = (close_paren + 1 < expr.size()) ? expr.substr(close_paren + 1) : "";
    
    // Recursively transform the object part (handles chained calls)
    obj = transform_embedded_expression(obj);
    
    // Parse arguments
    std::vector<std::string> raw_args = parse_raw_args(args_str);
    
    // Try to find @inline method in DefSchema
    // First check string methods
    if (auto* method_def = DefSchema::instance().lookup_method("string", method, raw_args.size())) {
        if (method_def->mapping_type == MappingType::Inline) {
            return expand_inline_template_raw(method_def->mapping_value, obj, raw_args) + suffix;
        }
    }
    
    // Check array methods
    if (auto* method_def = DefSchema::instance().lookup_method("array", method, raw_args.size())) {
        if (method_def->mapping_type == MappingType::Inline) {
            return expand_inline_template_raw(method_def->mapping_value, obj, raw_args) + suffix;
        }
    }
    
    // Check WebSocket and other typed methods by resolving symbol type
    std::string obj_type = ComponentTypeContext::instance().get_symbol_type(obj);
    if (!obj_type.empty()) {
        obj_type = ComponentTypeContext::instance().resolve(obj_type);
        obj_type = DefSchema::instance().resolve_alias(obj_type);
        
        if (auto* method_def = DefSchema::instance().lookup_method(obj_type, method, raw_args.size())) {
            if (method_def->mapping_type == MappingType::Inline) {
                return expand_inline_template_raw(method_def->mapping_value, obj, raw_args) + suffix;
            }
        }
    }
    
    // No transformation found, reconstruct with transformed parts
    std::string result = obj + "." + method + "(";
    for (size_t i = 0; i < raw_args.size(); ++i) {
        if (i > 0) result += ", ";
        result += raw_args[i];
    }
    result += ")" + suffix;
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
        // onMessage can accept 0 or 1 (string) param
        if (param_count >= 1) {
            return "g_ws_message_dispatcher.set(" + ws_obj + ", [this](const webcc::string& msg) { this->" + callback + "(msg); })";
        } else {
            return "g_ws_message_dispatcher.set(" + ws_obj + ", [this](const webcc::string&) { this->" + callback + "(); })";
        }
    } else if (event_type == "onOpen") {
        return "g_ws_open_dispatcher.set(" + ws_obj + ", [this]() { this->" + callback + "(); })";
    } else if (event_type == "onClose") {
        std::string invalidate = ws_member.empty() ? "" : " this->" + ws_member + " = webcc::WebSocket(-1);";
        return "g_ws_close_dispatcher.set(" + ws_obj + ", [this]() { this->" + callback + "();" + invalidate + " })";
    } else if (event_type == "onError") {
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
            // Enforce & prefix for callback arguments
            if (!arg.is_reference) {
                ErrorHandler::compiler_error("Callback argument must use '&' prefix (e.g., &" + arg.value->to_webcc() + ")");
            }
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
    if (intrinsic_name == "fetch_get") {
        if (args.empty()) return "";
        
        std::string url = args[0].value->to_webcc();
        std::string headers = "\"\"";
        bool headers_set = false;
        std::string code = "[&]() {\n";
        
        size_t callback_position = 0;
        for (size_t i = 1; i < args.size(); i++) {
            const auto& arg = args[i];
            if (!arg.is_reference) {
                if (headers_set) {
                    ErrorHandler::compiler_error("fetch.get accepts a single headers argument (JSON string). Combine all headers into one object string, e.g. '{\"Authorization\":\"Bearer ...\",\"Content-Type\":\"application/json\"}'.");
                }
                if (!arg.name.empty() && arg.name != "headers") {
                    ErrorHandler::compiler_error("Invalid named argument '" + arg.name + "' for fetch.get");
                }
                headers = arg.value->to_webcc();
                headers_set = true;
                continue;
            }
            callback_position++;
        }

        code += "            auto _req = webcc::fetch::get(" + url + ", " + headers + ");\n";
        
        callback_position = 0;
        for (size_t i = 1; i < args.size(); i++) {
            const auto& arg = args[i];
            if (!arg.is_reference) {
                continue;
            }
            // Enforce & prefix for callback arguments
            if (!arg.is_reference) {
                ErrorHandler::compiler_error("Callback argument must use '&' prefix (e.g., &" + arg.value->to_webcc() + ")");
            }
            std::string callback = arg.value->to_webcc();
            std::string event_name = !arg.name.empty() ? arg.name : (callback_position == 0 ? "onSuccess" : "onError");
            callback_position++;
            int param_count = ComponentTypeContext::instance().get_method_param_count(callback);
            
            if (event_name == "onSuccess") {
                if (param_count >= 1) {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string& data) { this->" + callback + "(data); });\n";
                } else {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string&) { this->" + callback + "(); });\n";
                }
            } else if (event_name == "onError") {
                if (param_count >= 1) {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string& error) { this->" + callback + "(error); });\n";
                } else {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string&) { this->" + callback + "(); });\n";
                }
            } else {
                ErrorHandler::compiler_error("Invalid callback name '" + event_name + "' for fetch.get (expected onSuccess or onError)");
            }
        }
        
        code += "            return _req;\n";
        code += "        }()";
        return code;
    }
    
    // FetchRequest.post with callback arguments
    if (intrinsic_name == "fetch_post") {
        if (args.size() < 2) return "";
        
        std::string url = args[0].value->to_webcc();
        std::string body = args[1].value->to_webcc();
        std::string headers = "\"\"";
        bool headers_set = false;
        std::string code = "[&]() {\n";

        size_t callback_position = 0;
        for (size_t i = 2; i < args.size(); i++) {
            const auto& arg = args[i];
            if (!arg.is_reference) {
                if (headers_set) {
                    ErrorHandler::compiler_error("fetch.post accepts a single headers argument (JSON string). Combine all headers into one object string, e.g. '{\"Authorization\":\"Bearer ...\",\"Content-Type\":\"application/json\"}'.");
                }
                if (!arg.name.empty() && arg.name != "headers") {
                    ErrorHandler::compiler_error("Invalid named argument '" + arg.name + "' for fetch.post");
                }
                headers = arg.value->to_webcc();
                headers_set = true;
                continue;
            }
            callback_position++;
        }

        code += "            auto _req = webcc::fetch::post(" + url + ", " + body + ", " + headers + ");\n";
        
        callback_position = 0;
        for (size_t i = 2; i < args.size(); i++) {
            const auto& arg = args[i];
            if (!arg.is_reference) {
                continue;
            }
            // Enforce & prefix for callback arguments
            if (!arg.is_reference) {
                ErrorHandler::compiler_error("Callback argument must use '&' prefix (e.g., &" + arg.value->to_webcc() + ")");
            }
            std::string callback = arg.value->to_webcc();
            std::string event_name = !arg.name.empty() ? arg.name : (callback_position == 0 ? "onSuccess" : "onError");
            callback_position++;
            int param_count = ComponentTypeContext::instance().get_method_param_count(callback);
            
            if (event_name == "onSuccess") {
                if (param_count >= 1) {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string& data) { this->" + callback + "(data); });\n";
                } else {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string&) { this->" + callback + "(); });\n";
                }
            } else if (event_name == "onError") {
                if (param_count >= 1) {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string& error) { this->" + callback + "(error); });\n";
                } else {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string&) { this->" + callback + "(); });\n";
                }
            } else {
                ErrorHandler::compiler_error("Invalid callback name '" + event_name + "' for fetch.post (expected onSuccess or onError)");
            }
        }
        
        code += "            return _req;\n";
        code += "        }()";
        return code;
    }

    // FetchRequest.patch with callback arguments
    if (intrinsic_name == "fetch_patch") {
        if (args.size() < 2) return "";

        std::string url = args[0].value->to_webcc();
        std::string body = args[1].value->to_webcc();
        std::string headers = "\"\"";
        bool headers_set = false;
        std::string code = "[&]() {\n";

        size_t callback_position = 0;
        for (size_t i = 2; i < args.size(); i++) {
            const auto& arg = args[i];
            if (!arg.is_reference) {
                if (headers_set) {
                    ErrorHandler::compiler_error("fetch.patch accepts a single headers argument (JSON string). Combine all headers into one object string, e.g. '{\"Authorization\":\"Bearer ...\",\"Content-Type\":\"application/json\"}'.");
                }
                if (!arg.name.empty() && arg.name != "headers") {
                    ErrorHandler::compiler_error("Invalid named argument '" + arg.name + "' for fetch.patch");
                }
                headers = arg.value->to_webcc();
                headers_set = true;
                continue;
            }
            callback_position++;
        }

        code += "            auto _req = webcc::fetch::patch(" + url + ", " + body + ", " + headers + ");\n";

        callback_position = 0;
        for (size_t i = 2; i < args.size(); i++) {
            const auto& arg = args[i];
            if (!arg.is_reference) {
                continue;
            }
            // Enforce & prefix for callback arguments
            if (!arg.is_reference) {
                ErrorHandler::compiler_error("Callback argument must use '&' prefix (e.g., &" + arg.value->to_webcc() + ")");
            }
            std::string callback = arg.value->to_webcc();
            std::string event_name = !arg.name.empty() ? arg.name : (callback_position == 0 ? "onSuccess" : "onError");
            callback_position++;
            int param_count = ComponentTypeContext::instance().get_method_param_count(callback);

            if (event_name == "onSuccess") {
                if (param_count >= 1) {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string& data) { this->" + callback + "(data); });\n";
                } else {
                    code += "            g_fetch_success_dispatcher.set(_req, [this](const webcc::string&) { this->" + callback + "(); });\n";
                }
            } else if (event_name == "onError") {
                if (param_count >= 1) {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string& error) { this->" + callback + "(error); });\n";
                } else {
                    code += "            g_fetch_error_dispatcher.set(_req, [this](const webcc::string&) { this->" + callback + "(); });\n";
                }
            } else {
                ErrorHandler::compiler_error("Invalid callback name '" + event_name + "' for fetch.patch (expected onSuccess or onError)");
            }
        }

        code += "            return _req;\n";
        code += "        }()";
        return code;
    }
    
    // Json.parse - returns a result value consumed via match
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
        // Handle escaped $ (\$ becomes literal $)
        if(value[i] == '\\' && i + 1 < value.length() && value[i+1] == '$') {
            current += '$';
            i++;
        } else if(value[i] == '$' && i + 1 < value.length() && value[i+1] == '{') {
            // Found ${ - look for closing }
            size_t close_pos = value.find('}', i + 2);
            if (close_pos == std::string::npos) {
                // No closing brace found - treat as literal
                current += '$';
                current += '{';
                i++; // skip the {
            } else {
                // Extract expression between ${ and }
                if(!current.empty()) parts.push_back({false, current});
                current = "";
                i += 2; // skip ${
                while(i < value.length() && value[i] != '}') {
                    current += value[i];
                    i++;
                }
                // Only treat as expression if content is non-empty
                if (!current.empty()) {
                    parts.push_back({true, current});
                } else {
                    // Empty ${} - treat as literal
                    parts.push_back({false, "${}"});
                }
                current = "";
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
            // Transform embedded expressions to apply @inline templates (e.g., subStr -> substr)
            code += transform_embedded_expression(parts[i].content);
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
    // Wrap in parentheses to preserve operator precedence
    return "(" + left->to_webcc() + " " + op + " " + right->to_webcc() + ")";
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
            // Instance call: obj.method() - resolve using known symbol type only.
            // This avoids false-positive remapping based solely on method name
            // (e.g., auth.configure() incorrectly mapping to wgpu::configure).
            std::string obj_type = ComponentTypeContext::instance().get_symbol_type(obj);
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
                            return expand_inline_template(map_method->mapping_value, obj, args);
                        } else if (map_method->mapping_type == MappingType::Map) {
                            pass_obj = true;
                            obj_arg = obj;
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
            }
            code += arg_code;
            first_arg = false;
        }
        code += ")";

        // Check return type from method definition
        if (map_method->return_type == "int") {
            code = "(int32_t)(" + code + ")";
        }

        return code;
    }

    std::string call_name = name;
    if (name.find('.') == std::string::npos &&
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
    for (auto& arg : args) children.push_back(arg.value.get());
    return children;
}

void FunctionCall::collect_dependencies(std::set<std::string>& deps) {
    // Handle object.method() calls - extract the object name
    size_t dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        deps.insert(name.substr(0, dot_pos));
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
    }
    return operand->to_webcc();  // References are handled at call sites
}

// MoveExpression - generates webcc::move() for explicit ownership transfer
std::string MoveExpression::to_webcc() {
    return "webcc::move(" + operand->to_webcc() + ")";
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
    return enum_name + "::" + value_name;
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
    std::string result = resolved_name + "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) result += ", ";
        result += args[i].value->to_webcc();
    }
    result += ")";
    return result;
}

std::vector<Expression*> ComponentConstruction::get_children() {
    std::vector<Expression*> children;
    for (auto& arg : args) children.push_back(arg.value.get());
    return children;
}

// Match expression code generation
// Generates a lambda (IIFE) with if-else chain
std::string MatchExpr::to_webcc() {
    std::string code = "[&]() {\n";
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

