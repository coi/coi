#include "expressions.h"
#include "formatter.h"
#include "../schema_loader.h"
#include <cctype>

// Reference to per-component context for reference props
extern std::set<std::string> g_ref_props;

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
        result += args[i]->to_webcc();
    }
    result += ")";
    return result;
}

std::string FunctionCall::to_webcc() {
    // Handle System.random() - built-in wasm random number generator
    if (name == "System.random") {
        if (args.empty()) {
            return "webcc::random()";
        } else if (args.size() == 1) {
            // Seed and return random: random_seed(seed); return random()
            return "(webcc::random_seed(" + args[0]->to_webcc() + "), webcc::random())";
        }
    }
    
    // Handle Input state methods (isKeyDown, isKeyUp, isMouseDown, etc.)
    {
        size_t dot_pos = name.rfind('.');
        if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < name.length() - 1) {
            std::string obj = name.substr(0, dot_pos);
            std::string method = name.substr(dot_pos + 1);
            
            if (obj == "Input") {
                if (method == "isKeyDown" && args.size() == 1) {
                    return "g_key_state[" + args[0]->to_webcc() + "]";
                }
                if (method == "isKeyUp" && args.size() == 1) {
                    return "!g_key_state[" + args[0]->to_webcc() + "]";
                }
            }
        }
    }

    // Handle string methods
    {
        size_t dot_pos = name.rfind('.');
        if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < name.length() - 1) {
            std::string obj = name.substr(0, dot_pos);
            std::string method = name.substr(dot_pos + 1);
            
            // Enum.size()
            if (method == "size" && args.size() == 0 && !obj.empty() && std::isupper(obj[0])) {
                size_t first_dot = obj.find('.');
                if (first_dot != std::string::npos) {
                    std::string comp = obj.substr(0, first_dot);
                    std::string enum_name = obj.substr(first_dot + 1);
                    return "static_cast<int>(" + comp + "::" + enum_name + "::_COUNT)";
                }
                return "static_cast<int>(" + obj + "::_COUNT)";
            }
            
            // String methods
            if (method == "length") return obj + ".length()";
            if (method == "at" && args.size() == 1) return obj + ".at(" + args[0]->to_webcc() + ")";
            if (method == "substr" && args.size() >= 1) {
                if (args.size() == 1) return obj + ".substr(" + args[0]->to_webcc() + ")";
                return obj + ".substr(" + args[0]->to_webcc() + ", " + args[1]->to_webcc() + ")";
            }
            if (method == "contains" && args.size() == 1) return obj + ".contains(" + args[0]->to_webcc() + ")";
            if (method == "isEmpty" && args.size() == 0) return obj + ".empty()";
            
            // Array/vector methods
            if (method == "push" && args.size() == 1) return obj + ".push_back(" + args[0]->to_webcc() + ")";
            if (method == "pop" && args.size() == 0) return obj + ".pop_back()";
            if (method == "size" && args.size() == 0) return "(int)" + obj + ".size()";
            if (method == "clear" && args.size() == 0) return obj + ".clear()";
        }
    }

    // Schema-based transformation
    size_t dot_pos = name.rfind('.');
    const coi::SchemaEntry* entry = nullptr;
    std::string obj_arg = "";
    bool pass_obj = false;

    if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < name.length() - 1) {
        std::string obj = name.substr(0, dot_pos);
        std::string method = name.substr(dot_pos + 1);
        
        std::string snake_method = SchemaLoader::to_snake_case(method);
        entry = SchemaLoader::instance().lookup(snake_method);
        
        if (entry && !entry->params.empty()) {
            const std::string& first_param_type = entry->params[0].type;
            if (SchemaLoader::instance().is_handle(first_param_type) && 
                args.size() == entry->params.size() - 1) {
                 pass_obj = true;
                 obj_arg = obj;
            }
        }
    } else {
        std::string snake_name = SchemaLoader::to_snake_case(name);
        entry = SchemaLoader::instance().lookup(snake_name);
    }

    if (entry) {
        // Check for string concat argument - use formatter block
        bool has_string_concat_arg = false;
        int string_concat_arg_idx = -1;
        for (size_t i = 0; i < args.size(); i++) {
            if (is_string_expr(args[i].get()) && dynamic_cast<BinaryOp*>(args[i].get())) {
                has_string_concat_arg = true;
                string_concat_arg_idx = i;
                break;
            }
        }
        
        if (has_string_concat_arg) {
            std::vector<Expression*> parts;
            flatten_string_concat(args[string_concat_arg_idx].get(), parts);
            
            std::string call_prefix = "webcc::" + entry->ns + "::" + entry->func_name + "(";
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
                        call_prefix += ", " + args[i]->to_webcc();
                    } else {
                        call_suffix += ", " + args[i]->to_webcc();
                    }
                } else {
                    if ((int)i != string_concat_arg_idx) {
                        call_prefix += args[i]->to_webcc();
                    }
                }
                first_arg = false;
            }
            call_suffix += ")";
            
            return generate_formatter_block(parts, call_prefix, call_suffix);
        }
        
        std::string code = "webcc::" + entry->ns + "::" + entry->func_name + "(";
        bool first_arg = true;

        if (pass_obj) {
            code += obj_arg;
            first_arg = false;
        }

        for(size_t i = 0; i < args.size(); i++){
            if (!first_arg) code += ", ";
            code += args[i]->to_webcc();
            first_arg = false;
        }
        code += ")";
        
        if (entry->return_type == "int32") {
            code = "(int32_t)(" + code + ")";
        }
        
        return code;
    }

    std::string result = name + "(";
    for(size_t i = 0; i < args.size(); i++){
        if(i > 0) result += ", ";
        result += args[i]->to_webcc();
    }
    result += ")";
    return result;
}

void FunctionCall::collect_dependencies(std::set<std::string>& deps) {
    size_t dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        deps.insert(name.substr(0, dot_pos));
    }
    for(auto& arg : args) arg->collect_dependencies(deps);
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
    return component_name + "()";
}

void ComponentConstruction::collect_dependencies(std::set<std::string>& deps) {
    for (auto& arg : args) arg.value->collect_dependencies(deps);
}
