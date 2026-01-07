#include "ast.h"
#include "schema_loader.h"
#include <cctype>
#include <algorithm>

// Global context for tracking reference props (which are stored as pointers)
static std::set<std::string> g_ref_props;

std::string convert_type(const std::string& type) {
    if (type == "string") return "webcc::string";
    // Handle dynamic arrays: T[]
    if (type.ends_with("[]")) {
        std::string inner = type.substr(0, type.length() - 2);
        return "webcc::vector<" + convert_type(inner) + ">";
    }
    // Handle fixed-size arrays: T[N]
    size_t bracket_pos = type.rfind('[');
    if (bracket_pos != std::string::npos && type.back() == ']') {
        std::string size_str = type.substr(bracket_pos + 1, type.length() - bracket_pos - 2);
        // Check if it's a number (fixed-size array)
        bool is_number = !size_str.empty() && std::all_of(size_str.begin(), size_str.end(), ::isdigit);
        if (is_number) {
            std::string inner = type.substr(0, bracket_pos);
            return "webcc::array<" + convert_type(inner) + ", " + size_str + ">";
        }
    }
    // Check if type is a webcc handle type and add prefix
    if (SchemaLoader::instance().is_handle(type)) {
        return "webcc::" + type;
    }
    return type;
}

std::string IntLiteral::to_webcc() {return std::to_string(value);}

std::string FloatLiteral::to_webcc() {
    std::string s = std::to_string(value);
    if(s.find('.') != std::string::npos){
        s = s.substr(0, s.find_last_not_of('0')+1);
        if(s.back() == '.') s += "0";
    }
    return s + "f";
}

std::vector<StringLiteral::Part> StringLiteral::parse() {
    std::vector<Part> parts;
    std::string current;
    for(size_t i=0; i<value.length(); ++i) {
        if(value[i] == '\\' && i + 1 < value.length() && (value[i+1] == '{' || value[i+1] == '}')) {
            current += value[i+1];
            i++;
        } else if(value[i] == '{') {
            if(!current.empty()) parts.push_back({false, current});
            current = "";
            i++;
            while(i < value.length() && value[i] != '}') {
                current += value[i];
                i++;
            }
            parts.push_back({true, current});
            current = "";
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
    // Handle string methods: str.length(), str.at(i), str.substr(pos, len), str.contains(text)
    {
        size_t dot_pos = name.rfind('.');
        if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < name.length() - 1) {
            std::string obj = name.substr(0, dot_pos);
            std::string method = name.substr(dot_pos + 1);
            
            // String methods - map to webcc::string methods
            if (method == "length") {
                return obj + ".length()";
            }
            if (method == "at" && args.size() == 1) {
                // Returns a single character as a string
                return obj + ".at(" + args[0]->to_webcc() + ")";
            }
            if (method == "substr" && args.size() >= 1) {
                // substr(pos) or substr(pos, len)
                if (args.size() == 1) {
                    return obj + ".substr(" + args[0]->to_webcc() + ")";
                } else {
                    return obj + ".substr(" + args[0]->to_webcc() + ", " + args[1]->to_webcc() + ")";
                }
            }
            if (method == "contains" && args.size() == 1) {
                // Check if string contains substring
                return obj + ".contains(" + args[0]->to_webcc() + ")";
            }
            if (method == "isEmpty" && args.size() == 0) {
                // Check if string is empty
                return obj + ".empty()";
            }
            
            // Array/vector methods - map to webcc::vector methods
            if (method == "push" && args.size() == 1) {
                return obj + ".push_back(" + args[0]->to_webcc() + ")";
            }
            if (method == "pop" && args.size() == 0) {
                return obj + ".pop_back()";
            }
            if (method == "size" && args.size() == 0) {
                return "(int)" + obj + ".size()";
            }
            if (method == "clear" && args.size() == 0) {
                return obj + ".clear()";
            }
            if (method == "isEmpty" && args.size() == 0) {
                return obj + ".empty()";
            }
        }
    }

    if (name == "log" || name == "log.info" || name == "log.warn" || name == "log.error" || name == "log.debug" || name == "log.event") {
        std::vector<Expression*> parts;
        std::function<void(Expression*)> flatten = [&](Expression* e) {
            if (auto bin = dynamic_cast<BinaryOp*>(e)) {
                if (bin->op == "+") {
                    flatten(bin->left.get());
                    flatten(bin->right.get());
                    return;
                }
            }
            parts.push_back(e);
        };

        for(auto& arg : args) {
            flatten(arg.get());
        }

        std::string code = "{ webcc::formatter<256> _fmt; ";
        for(auto* p : parts) {
            code += "_fmt << (" + p->to_webcc() + "); ";
        }

        if (name == "log" || name == "log.info") {
            code += "webcc::system::log(_fmt.c_str()); }";
        } else if (name == "log.warn") {
            code += "webcc::system::warn(_fmt.c_str()); }";
        } else if (name == "log.error") {
            code += "webcc::system::error(_fmt.c_str()); }";
        } else if (name == "log.debug") {
            code += "webcc::system::log(webcc::string::concat(\"[DEBUG] \", _fmt.c_str())); }";
        } else if (name == "log.event") {
            code += "webcc::system::log(webcc::string::concat(\"[EVENT] \", _fmt.c_str())); }";
        }
        return code;
    }

    // Check for Schema-based transformation (e.g. canvas.setSize -> webcc::canvas::set_size(canvas, ...))
    size_t dot_pos = name.rfind('.');
    const coi::SchemaEntry* entry = nullptr;
    std::string obj_arg = "";
    bool pass_obj = false;

    if (dot_pos != std::string::npos && dot_pos > 0 && dot_pos < name.length() - 1) {
        std::string obj = name.substr(0, dot_pos);
        std::string method = name.substr(dot_pos + 1);
        
        std::string snake_method = SchemaLoader::to_snake_case(method);
        entry = SchemaLoader::instance().lookup(snake_method);
        
        if (entry) {
            // Check if we should pass 'obj' as the first argument to support OOP-style calls
            // e.g. canvas.drawRect(...) -> webcc::canvas::draw_rect(canvas, ...)
            //
            // Conditions:
            // 1. Function has parameters
            // 2. First parameter is a Handle type (e.g. Canvas, DOMElement)
            // 3. The actual arguments count is ONE LESS than expected parameters count
            if (!entry->params.empty()) {
                const std::string& first_param_type = entry->params[0].type;
                if (SchemaLoader::instance().is_handle(first_param_type) && 
                    args.size() == entry->params.size() - 1) {
                     pass_obj = true;
                     obj_arg = obj;
                }
            }
        }
    } else {
        // No dot, try global lookup
        std::string snake_name = SchemaLoader::to_snake_case(name);
        entry = SchemaLoader::instance().lookup(snake_name);
    }


    if (entry) {
        std::string code = "webcc::" + entry->ns + "::" + entry->func_name + "(";
        
        size_t param_idx = 0;
        bool first_arg = true;

        if (pass_obj) {
            // typed_handle types implicitly convert to webcc::handle, no cast needed
            code += obj_arg;
            first_arg = false;
            param_idx++;
        }

        for(size_t i = 0; i < args.size(); i++){
            if (!first_arg) code += ", ";
            // typed_handle types implicitly convert to webcc::handle, no cast needed
            std::string arg_val = args[i]->to_webcc();
            
            code += arg_val;
            first_arg = false;
            param_idx++;
        }
        code += ")";
        
        // Cast return type if it is int32 (handles are returned as webcc::handle which has explicit cast)
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
    // If name contains '.', the object part is a dependency (e.g., "todos.size" -> "todos")
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
    for (auto& elem : elements) {
        elem->collect_dependencies(deps);
    }
}

bool ArrayLiteral::is_static() {
    for (auto& elem : elements) {
        if (!elem->is_static()) return false;
    }
    return true;
}

std::string ArrayRepeatLiteral::to_webcc() {
    // Generate brace-initialization for webcc::array
    // e.g., [0; 3] becomes {0, 0, 0}
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

std::string VarDeclaration::to_webcc() {
    // Special handling for ArrayRepeatLiteral - generates webcc::array<T, N>
    if (auto repeat = dynamic_cast<ArrayRepeatLiteral*>(initializer.get())) {
        // Extract element type from array type (e.g., "int[]" -> "int")
        std::string elem_type = type;
        if (elem_type.ends_with("[]")) {
            elem_type = elem_type.substr(0, elem_type.length() - 2);
        }
        std::string result = (is_mutable ? "" : "const ");
        result += "webcc::array<" + convert_type(elem_type) + ", " + std::to_string(repeat->count) + ">";
        result += " " + name + " = " + repeat->to_webcc() + ";";
        return result;
    }

    // Dynamic array literal initializer -> build vector via lambda (webcc::vector has no initializer_list ctor)
    if (auto arr_lit = dynamic_cast<ArrayLiteral*>(initializer.get())) {
        if (type.ends_with("[]")) {
            std::string elem_type = type.substr(0, type.length() - 2);
            std::string vec_type = "webcc::vector<" + convert_type(elem_type) + ">";

            std::string result = (is_mutable ? "" : "const ") + vec_type;
            if (is_reference) result += "&";
            result += " " + name + " = [](){ " + vec_type + " _v; ";
            for (size_t i = 0; i < arr_lit->elements.size(); ++i) {
                result += "_v.push_back(" + arr_lit->elements[i]->to_webcc() + "); ";
            }
            result += "return _v; }();";
            return result;
        }
    }
    
    std::string result = (is_mutable ? "" : "const ") + convert_type(type);
    if(is_reference) result += "&";
    result += " " + name;
    if(initializer) {
        // Use brace initialization for handle types to work with typed_handle's explicit constructor
        if (SchemaLoader::instance().is_handle(type)) {
            result += "{" + initializer->to_webcc() + "}";
        } else {
            result += " = " + initializer->to_webcc();
        }
    }
    result += ";";
    return result;
}

std::string PropDeclaration::to_webcc() {
    return "";
}

std::string Assignment::to_webcc() {
    std::string lhs = name;
    if(g_ref_props.count(name)) {
        lhs = "(*" + name + ")";
    }
    
    std::string rhs = value->to_webcc();
    
    // For handle downcasts (e.g., DOMElement -> Canvas), we need to cast via int32_t
    // Only allowed when target_type extends the source type (checked during type validation)
    // e.g., canvas = webcc::Canvas((int32_t)webcc::dom::get_element_by_id("id"));
    if (!target_type.empty() && SchemaLoader::instance().is_handle(target_type)) {
        // Check if this is a downcast (target extends some base type)
        // The type checker already validated this is a valid cast via is_assignable_to
        rhs = convert_type(target_type) + "((int32_t)" + rhs + ")";
    }
    
    return lhs + " = " + rhs + ";";
}
void Assignment::collect_dependencies(std::set<std::string>& deps) {
    value->collect_dependencies(deps);
}

std::string IndexAssignment::to_webcc() {
    return array->to_webcc() + "[" + index->to_webcc() + "] = " + value->to_webcc() + ";";
}
void IndexAssignment::collect_dependencies(std::set<std::string>& deps) {
    array->collect_dependencies(deps);
    index->collect_dependencies(deps);
    value->collect_dependencies(deps);
}

std::string ReturnStatement::to_webcc() {
    if (value) {
        return "return " + value->to_webcc() + ";";
    }
    return "return;";
}
void ReturnStatement::collect_dependencies(std::set<std::string>& deps) {
    if (value) {
        value->collect_dependencies(deps);
    }
}

std::string ExpressionStatement::to_webcc() {
    return expression->to_webcc() + ";\n";
}
void ExpressionStatement::collect_dependencies(std::set<std::string>& deps) {
    expression->collect_dependencies(deps);
}

std::string BlockStatement::to_webcc() {
    std::string code = "{\n";
    for(auto& stmt : statements) code += stmt->to_webcc();
    code += "}\n";
    return code;
}
void BlockStatement::collect_dependencies(std::set<std::string>& deps) {
    for(auto& stmt : statements) stmt->collect_dependencies(deps);
}

std::string IfStatement::to_webcc() {
    std::string code = "if(" + condition->to_webcc() + ") ";
    code += then_branch->to_webcc();
    if(else_branch){
        code += " else ";
        code += else_branch->to_webcc();
    }
    return code;
}
void IfStatement::collect_dependencies(std::set<std::string>& deps) {
    condition->collect_dependencies(deps);
    then_branch->collect_dependencies(deps);
    if(else_branch) else_branch->collect_dependencies(deps);
}

std::string ForStatement::to_webcc() {
    std::string code = "for(";
    if(init) {
        // For loop init variables must be mutable (not const) for increment to work
        if(auto varDecl = dynamic_cast<VarDeclaration*>(init.get())) {
            varDecl->is_mutable = true;
        }
        // Remove trailing semicolon from init statement since for() adds it
        std::string init_code = init->to_webcc();
        if(!init_code.empty() && init_code.back() == ';') {
            init_code.pop_back();
        }
        code += init_code;
    }
    code += "; ";
    if(condition) {
        code += condition->to_webcc();
    }
    code += "; ";
    if(update) {
        code += update->to_webcc();
    }
    code += ") ";
    code += body->to_webcc();
    return code;
}
void ForStatement::collect_dependencies(std::set<std::string>& deps) {
    if(init) init->collect_dependencies(deps);
    if(condition) condition->collect_dependencies(deps);
    if(update) update->collect_dependencies(deps);
    body->collect_dependencies(deps);
}

std::string ForRangeStatement::to_webcc() {
    // Generates: for(int var_name = start; var_name < end; var_name++)
    std::string code = "for(int " + var_name + " = " + start->to_webcc() + "; ";
    code += "(" + var_name + " < " + end->to_webcc() + "); ";
    code += var_name + "++) ";
    code += body->to_webcc();
    return code;
}
void ForRangeStatement::collect_dependencies(std::set<std::string>& deps) {
    start->collect_dependencies(deps);
    end->collect_dependencies(deps);
    body->collect_dependencies(deps);
}

std::string ForEachStatement::to_webcc() {
    // Generates: for(auto& var_name : iterable)
    std::string code = "for(auto& " + var_name + " : " + iterable->to_webcc() + ") ";
    code += body->to_webcc();
    return code;
}
void ForEachStatement::collect_dependencies(std::set<std::string>& deps) {
    iterable->collect_dependencies(deps);
    body->collect_dependencies(deps);
}

void collect_mods_recursive(Statement* stmt, std::set<std::string>& mods) {
    if(auto assign = dynamic_cast<Assignment*>(stmt)) {
        mods.insert(assign->name);
    }
    else if(auto idxAssign = dynamic_cast<IndexAssignment*>(stmt)) {
        // arr[i] = value modifies arr
        if(auto id = dynamic_cast<Identifier*>(idxAssign->array.get())) {
            mods.insert(id->name);
        }
    }
    else if(auto exprStmt = dynamic_cast<ExpressionStatement*>(stmt)) {
        if(auto postfix = dynamic_cast<PostfixOp*>(exprStmt->expression.get())) {
            if(auto id = dynamic_cast<Identifier*>(postfix->operand.get())) {
                mods.insert(id->name);
            }
        }
        else if(auto unary = dynamic_cast<UnaryOp*>(exprStmt->expression.get())) {
             if(unary->op == "++" || unary->op == "--") {
                 if(auto id = dynamic_cast<Identifier*>(unary->operand.get())) {
                     mods.insert(id->name);
                 }
             }
        }
        // Detect mutating method calls like arr.push(), arr.pop(), arr.clear()
        else if(auto call = dynamic_cast<FunctionCall*>(exprStmt->expression.get())) {
            // Check if it's a method call on an object (name contains '.')
            size_t dot_pos = call->name.rfind('.');
            if(dot_pos != std::string::npos) {
                std::string method = call->name.substr(dot_pos + 1);
                // These methods mutate the array
                if(method == "push" || method == "push_back" || 
                   method == "pop" || method == "pop_back" || 
                   method == "clear") {
                    std::string obj = call->name.substr(0, dot_pos);
                    mods.insert(obj);
                }
            }
        }
    }
    else if(auto block = dynamic_cast<BlockStatement*>(stmt)) {
        for(auto& s : block->statements) {
            collect_mods_recursive(s.get(), mods);
        }
    }
    else if(auto ifStmt = dynamic_cast<IfStatement*>(stmt)) {
        collect_mods_recursive(ifStmt->then_branch.get(), mods);
        if(ifStmt->else_branch) {
            collect_mods_recursive(ifStmt->else_branch.get(), mods);
        }
    }
    else if(auto forStmt = dynamic_cast<ForStatement*>(stmt)) {
        if(forStmt->init) collect_mods_recursive(forStmt->init.get(), mods);
        collect_mods_recursive(forStmt->body.get(), mods);
    }
    else if(auto forRange = dynamic_cast<ForRangeStatement*>(stmt)) {
        collect_mods_recursive(forRange->body.get(), mods);
    }
    else if(auto forEach = dynamic_cast<ForEachStatement*>(stmt)) {
        collect_mods_recursive(forEach->body.get(), mods);
    }
}

std::string FunctionDef::to_webcc(const std::string& injected_code){
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

std::string TextNode::to_webcc() { return "\"" + text + "\""; }

std::string ComponentInstantiation::to_webcc() { return ""; }

void ComponentInstantiation::generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                  std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                  std::vector<Binding>& bindings,
                  std::map<std::string, int>& component_counters,
                  const std::set<std::string>& method_names,
                  const std::string& parent_component_name,
                  bool in_loop,
                  std::vector<LoopRegion>* loop_regions,
                  int* loop_counter,
                  std::vector<IfRegion>* if_regions,
                  int* if_counter) {
    
    int id = component_counters[component_name]++;
    std::string instance_name;
    
    if (in_loop) {
        // In a loop, allocate on heap and store in the vector member
        // The vector is named _loop_<ComponentName>s
        std::string vector_name = "_loop_" + component_name + "s";
        instance_name = vector_name + "[" + vector_name + ".size() - 1]";
        ss << "        " << vector_name << ".push_back(" << component_name << "());\n";
        ss << "        auto& _inst = " << instance_name << ";\n";
        instance_name = "_inst";
    } else {
        // Outside a loop, use the pre-declared member
        instance_name = component_name + "_" + std::to_string(id);
    }
    
    // Set props
    for(auto& prop : props) {
        std::string val = prop.value->to_webcc();
        // Check if val is a method name
        if(method_names.count(val)) {
            // Wrap in function
            ss << "        " << instance_name << "." << prop.name << " = [this]() { this->" << val << "(); };\n";
        } else if (prop.is_reference) {
            // Pass address for reference props
            ss << "        " << instance_name << "." << prop.name << " = &(" << val << ");\n";
        } else {
            ss << "        " << instance_name << "." << prop.name << " = " << val << ";\n";
        }
    }
    
    // For reference props, set up the onChange callback to call _update_<varname>()
    // Skip this for in_loop since we don't support reactive updates for dynamically created components
    if (!in_loop) {
        for(auto& prop : props) {
            if(prop.is_reference && prop.is_mutable_def) {
                std::string callback_name = "on" + std::string(1, std::toupper(prop.name[0])) + prop.name.substr(1) + "Change";
                
                // Collect dependencies from the prop value to know which variable is being passed
                std::set<std::string> prop_deps;
                prop.value->collect_dependencies(prop_deps);
                
                // Call _update_<varname>() for each variable that has dependent bindings
                std::string update_calls;
                for(const auto& dep : prop_deps) {
                    // Check if any binding depends on this variable
                    bool has_dependent_binding = false;
                    for(const auto& binding : bindings) {
                        if(binding.dependencies.count(dep)) {
                            has_dependent_binding = true;
                            break;
                        }
                    }
                    if(has_dependent_binding) {
                        update_calls += "_update_" + dep + "(); ";
                    }
                }
                
                if(!update_calls.empty()) {
                    ss << "        " << instance_name << "." << callback_name << " = [this]() { " << update_calls << "};\n";
                }
            }
        }
    }
    
    // Call view
    if(!parent.empty()) {
        ss << "        " << instance_name << ".view(" << parent << ");\n";
    } else {
        ss << "        " << instance_name << ".view();\n";
    }
}

void ComponentInstantiation::collect_dependencies(std::set<std::string>& deps) {
    for(auto& prop : props) {
        prop.value->collect_dependencies(deps);
    }
}

std::string HTMLElement::to_webcc() { return ""; }

void HTMLElement::generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                  std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                  std::vector<Binding>& bindings,
                  std::map<std::string, int>& component_counters,
                  const std::set<std::string>& method_names,
                  const std::string& parent_component_name,
                  bool in_loop,
                  std::vector<LoopRegion>* loop_regions,
                  int* loop_counter,
                  std::vector<IfRegion>* if_regions,
                  int* if_counter) {
    int my_id = counter++;
    std::string var;
    
    // In a loop, use local variables instead of members
    if (in_loop) {
        var = "_el_" + std::to_string(my_id);
        ss << "        webcc::handle " << var << " = webcc::dom::create_element(\"" << tag << "\");\n";
    } else {
        var = "el_" + std::to_string(my_id);
        ss << "        " << var << " = webcc::dom::create_element(\"" << tag << "\");\n";
    }
    
    ss << "        webcc::dom::set_attribute(" << var << ", \"coi-scope\", \"" << parent_component_name << "\");\n";
    
    // Bind element to variable if ref_binding is set (e.g., &={canvas})
    if (!ref_binding.empty()) {
        ss << "        " << ref_binding << " = " << var << ";\n";
    }
    
    // Attributes
    for(auto& attr : attributes){
        if(attr.name == "onclick"){
             ss << "        webcc::dom::add_click_listener(" << var << ");\n";
             // Store handler for later generation
             bool is_call = dynamic_cast<FunctionCall*>(attr.value.get()) != nullptr;
             click_handlers.push_back({my_id, attr.value->to_webcc(), is_call});
        } else {
             std::string val = attr.value->to_webcc();
             ss << "        webcc::dom::set_attribute(" << var << ", \"" << attr.name << "\", " << val << ");\n";
             
             if(!attr.value->is_static() && !in_loop) {
                 Binding b;
                 b.element_id = my_id;
                 b.type = "attr";
                 b.name = attr.name;
                 b.value_code = val;
                 b.expr = attr.value.get();
                 attr.value->collect_dependencies(b.dependencies);
                 bindings.push_back(b);
             }
        }
    }

    // Append to parent
    if(!parent.empty()){
        ss << "        webcc::dom::append_child(" << parent << ", " << var << ");\n";
    }

    // Children
    bool has_elements = false;
    for(auto& child : children) {
        if(dynamic_cast<HTMLElement*>(child.get()) || dynamic_cast<ComponentInstantiation*>(child.get()) || 
           dynamic_cast<ViewIfStatement*>(child.get()) || dynamic_cast<ViewForRangeStatement*>(child.get()) ||
           dynamic_cast<ViewForEachStatement*>(child.get())) has_elements = true;
    }

    if(has_elements){
         for(auto& child : children){
             if(auto el = dynamic_cast<HTMLElement*>(child.get())){
                 el->generate_code(ss, var, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
             } else if(auto comp = dynamic_cast<ComponentInstantiation*>(child.get())){
                 comp->generate_code(ss, var, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
             } else if(auto viewIf = dynamic_cast<ViewIfStatement*>(child.get())){
                 viewIf->generate_code(ss, var, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
             } else if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(child.get())){
                 viewFor->generate_code(ss, var, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
             } else if(auto viewForEach = dynamic_cast<ViewForEachStatement*>(child.get())){
                 viewForEach->generate_code(ss, var, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
             }
         }
    } else {
         // Text content
         std::string code;
         bool all_static = true;
         
         if (children.size() == 1) {
             code = children[0]->to_webcc();
             if (!(code.size() >= 2 && code.front() == '"' && code.back() == '"')) {
                 all_static = false;
                 code = "webcc::string::concat(" + code + ")";
             }
         } else if (children.size() > 1) {
             std::string args;
             bool first = true;
             for(auto& child : children){
                 if(!first) args += ", ";
                 std::string c = child->to_webcc();
                 args += c;
                 if (!(c.size() >= 2 && c.front() == '"' && c.back() == '"')) all_static = false;
                 first = false;
             }
             code = "webcc::string::concat(" + args + ")";
         }

         if(!code.empty()) {
             ss << "        webcc::dom::set_inner_text(" << var << ", " << code << ");\n";
             
             if(!all_static && !in_loop) {
                 Binding b;
                 b.element_id = my_id;
                 b.type = "text";
                 b.value_code = code;
                 for(auto& child : children) child->collect_dependencies(b.dependencies);
                 bindings.push_back(b);
             }
         }
    }
}

void HTMLElement::collect_dependencies(std::set<std::string>& deps) {
    for(auto& attr : attributes) {
        if(attr.value) attr.value->collect_dependencies(deps);
    }
    for(auto& child : children) {
        child->collect_dependencies(deps);
    }
}

// ViewIfStatement - conditional rendering in view
void ViewIfStatement::generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                  std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                  std::vector<Binding>& bindings,
                  std::map<std::string, int>& component_counters,
                  const std::set<std::string>& method_names,
                  const std::string& parent_component_name,
                  bool in_loop,
                  std::vector<LoopRegion>* loop_regions,
                  int* loop_counter,
                  std::vector<IfRegion>* if_regions,
                  int* if_counter) {
    
    // If in a loop or no if_regions tracking, use simple static if
    if (in_loop || !if_regions || !if_counter) {
        // Track loop IDs created before this if statement
        int loop_id_before = loop_counter ? *loop_counter : 0;
        
        ss << "        if (" << condition->to_webcc() << ") {\n";
        for(auto& child : then_children) {
            if(auto el = dynamic_cast<HTMLElement*>(child.get())){
                el->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
            } else if(auto comp = dynamic_cast<ComponentInstantiation*>(child.get())){
                comp->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
            } else if(auto viewIf = dynamic_cast<ViewIfStatement*>(child.get())){
                viewIf->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
            } else if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(child.get())){
                viewFor->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
            }
        }
        if (!else_children.empty()) {
            ss << "        } else {\n";
            for(auto& child : else_children) {
                if(auto el = dynamic_cast<HTMLElement*>(child.get())){
                    el->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
                } else if(auto comp = dynamic_cast<ComponentInstantiation*>(child.get())){
                    comp->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
                } else if(auto viewIf = dynamic_cast<ViewIfStatement*>(child.get())){
                    viewIf->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
                } else if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(child.get())){
                    viewFor->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
                }
            }
        }
        ss << "        }\n";
        
        // After the if/else, ensure all loop parents created inside are assigned
        if (loop_counter && loop_regions) {
            int loop_id_after = *loop_counter;
            for (int lid = loop_id_before; lid < loop_id_after; lid++) {
                ss << "        _loop_" << lid << "_parent = " << parent << ";\n";
            }
        }
        return;
    }
    
    // Reactive if/else - track for dynamic updates
    int my_if_id = (*if_counter)++;
    if_id = my_if_id;
    
    IfRegion region;
    region.if_id = my_if_id;
    region.condition_code = condition->to_webcc();
    condition->collect_dependencies(region.dependencies);
    
    // Track element IDs and counters before and after each branch
    int counter_before_then = counter;
    int loop_id_before = loop_counter ? *loop_counter : 0;
    int if_id_before = *if_counter;
    std::map<std::string, int> comp_counters_before_then = component_counters;
    
    // Generate then branch code into a separate stream
    std::stringstream then_ss;
    std::vector<Binding> then_bindings;
    for(auto& child : then_children) {
        if(auto el = dynamic_cast<HTMLElement*>(child.get())){
            el->generate_code(then_ss, parent, counter, click_handlers, then_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter);
        } else if(auto comp = dynamic_cast<ComponentInstantiation*>(child.get())){
            comp->generate_code(then_ss, parent, counter, click_handlers, then_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter);
        } else if(auto viewIf = dynamic_cast<ViewIfStatement*>(child.get())){
            viewIf->generate_code(then_ss, parent, counter, click_handlers, then_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter);
        } else if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(child.get())){
            viewFor->generate_code(then_ss, parent, counter, click_handlers, then_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter);
        }
    }
    int counter_after_then = counter;
    int loop_id_after_then = loop_counter ? *loop_counter : 0;
    int if_id_after_then = *if_counter;
    
    // Record element IDs in then branch
    for (int i = counter_before_then; i < counter_after_then; i++) {
        region.then_element_ids.push_back(i);
    }
    // Record loop IDs in then branch
    for (int i = loop_id_before; i < loop_id_after_then; i++) {
        region.then_loop_ids.push_back(i);
    }
    // Record nested if IDs in then branch
    for (int i = if_id_before; i < if_id_after_then; i++) {
        region.then_if_ids.push_back(i);
    }
    // Record components in then branch
    for (auto& [comp_name, count] : component_counters) {
        int before = comp_counters_before_then.count(comp_name) ? comp_counters_before_then[comp_name] : 0;
        for (int i = before; i < count; i++) {
            region.then_components.push_back({comp_name, i});
        }
    }
    
    region.then_creation_code = then_ss.str();
    
    // Generate else branch code
    int counter_before_else = counter;
    int loop_id_before_else = loop_counter ? *loop_counter : 0;
    int if_id_before_else = *if_counter;
    std::map<std::string, int> comp_counters_before_else = component_counters;
    
    std::stringstream else_ss;
    std::vector<Binding> else_bindings;
    if (!else_children.empty()) {
        for(auto& child : else_children) {
            if(auto el = dynamic_cast<HTMLElement*>(child.get())){
                el->generate_code(else_ss, parent, counter, click_handlers, else_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter);
            } else if(auto comp = dynamic_cast<ComponentInstantiation*>(child.get())){
                comp->generate_code(else_ss, parent, counter, click_handlers, else_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter);
            } else if(auto viewIf = dynamic_cast<ViewIfStatement*>(child.get())){
                viewIf->generate_code(else_ss, parent, counter, click_handlers, else_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter);
            } else if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(child.get())){
                viewFor->generate_code(else_ss, parent, counter, click_handlers, else_bindings, component_counters, method_names, parent_component_name, false, loop_regions, loop_counter, if_regions, if_counter);
            }
        }
    }
    int counter_after_else = counter;
    int loop_id_after_else = loop_counter ? *loop_counter : 0;
    int if_id_after_else = *if_counter;
    
    // Record element IDs in else branch
    for (int i = counter_before_else; i < counter_after_else; i++) {
        region.else_element_ids.push_back(i);
    }
    // Record loop IDs in else branch
    for (int i = loop_id_before_else; i < loop_id_after_else; i++) {
        region.else_loop_ids.push_back(i);
    }
    // Record nested if IDs in else branch
    for (int i = if_id_before_else; i < if_id_after_else; i++) {
        region.else_if_ids.push_back(i);
    }
    // Record components in else branch
    for (auto& [comp_name, count] : component_counters) {
        int before = comp_counters_before_else.count(comp_name) ? comp_counters_before_else[comp_name] : 0;
        for (int i = before; i < count; i++) {
            region.else_components.push_back({comp_name, i});
        }
    }
    
    region.else_creation_code = else_ss.str();
    
    // Merge bindings (only add if not in loop)
    for (auto& b : then_bindings) bindings.push_back(b);
    for (auto& b : else_bindings) bindings.push_back(b);
    
    // Generate the actual if statement with initial rendering
    ss << "        _if_" << my_if_id << "_parent = " << parent << ";\n";
    ss << "        if (" << region.condition_code << ") {\n";
    ss << "        _if_" << my_if_id << "_state = true;\n";
    ss << then_ss.str();
    if (!else_children.empty()) {
        ss << "        } else {\n";
        ss << "        _if_" << my_if_id << "_state = false;\n";
        ss << else_ss.str();
    } else {
        ss << "        } else {\n";
        ss << "        _if_" << my_if_id << "_state = false;\n";
    }
    ss << "        }\n";
    
    // Assign loop parents for all loops in both branches
    if (loop_counter && loop_regions) {
        for (int lid = loop_id_before; lid < loop_id_after_else; lid++) {
            ss << "        _loop_" << lid << "_parent = " << parent << ";\n";
        }
    }
    
    if_regions->push_back(region);
}

void ViewIfStatement::collect_dependencies(std::set<std::string>& deps) {
    condition->collect_dependencies(deps);
    for(auto& child : then_children) {
        child->collect_dependencies(deps);
    }
    for(auto& child : else_children) {
        child->collect_dependencies(deps);
    }
}

// Helper to generate code for a view child node
static void generate_view_child(ASTNode* child, std::stringstream& ss, const std::string& parent, int& counter,
                                std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                                std::vector<Binding>& bindings,
                                std::map<std::string, int>& component_counters,
                                const std::set<std::string>& method_names,
                                const std::string& parent_component_name,
                                bool in_loop = false,
                                std::vector<LoopRegion>* loop_regions = nullptr,
                                int* loop_counter = nullptr,
                                std::vector<IfRegion>* if_regions = nullptr,
                                int* if_counter = nullptr) {
    if(auto el = dynamic_cast<HTMLElement*>(child)){
        el->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
    } else if(auto comp = dynamic_cast<ComponentInstantiation*>(child)){
        comp->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
    } else if(auto viewIf = dynamic_cast<ViewIfStatement*>(child)){
        viewIf->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
    } else if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(child)){
        viewFor->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
    } else if(auto viewForEach = dynamic_cast<ViewForEachStatement*>(child)){
        viewForEach->generate_code(ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, in_loop, loop_regions, loop_counter, if_regions, if_counter);
    }
}

// ViewForRangeStatement - for i in 0:10 in view
void ViewForRangeStatement::generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                  std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                  std::vector<Binding>& bindings,
                  std::map<std::string, int>& component_counters,
                  const std::set<std::string>& method_names,
                  const std::string& parent_component_name,
                  bool in_loop,
                  std::vector<LoopRegion>* loop_regions,
                  int* loop_counter,
                  std::vector<IfRegion>* if_regions,
                  int* if_counter) {
    
    // If we're already inside a loop, just generate a simple loop (no reactivity for nested loops)
    if (in_loop || !loop_regions || !loop_counter) {
        ss << "        for (int " << var_name << " = " << start->to_webcc() << "; " 
           << var_name << " < " << end->to_webcc() << "; " << var_name << "++) {\n";
        for(auto& child : children) {
            generate_view_child(child.get(), ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, true, nullptr, nullptr, nullptr, nullptr);
        }
        ss << "        }\n";
        return;
    }
    
    // Top-level loop: generate reactive loop region
    int my_loop_id = (*loop_counter)++;
    loop_id = my_loop_id;
    
    LoopRegion region;
    region.loop_id = my_loop_id;
    region.parent_element = parent;
    region.start_expr = start->to_webcc();
    region.end_expr = end->to_webcc();
    region.var_name = var_name;
    
    // Collect dependencies for this loop
    start->collect_dependencies(region.dependencies);
    end->collect_dependencies(region.dependencies);
    
    // Check if children contain a component instantiation and capture its details
    ComponentInstantiation* loop_component = nullptr;
    HTMLElement* loop_html_element = nullptr;
    for(auto& child : children) {
        if(auto comp = dynamic_cast<ComponentInstantiation*>(child.get())) {
            region.component_type = comp->component_name;
            loop_component = comp;
            break;
        }
        if(auto el = dynamic_cast<HTMLElement*>(child.get())) {
            loop_html_element = el;
            region.is_html_loop = true;
            break;
        }
    }
    
    // Generate the item creation code into a separate stream
    // Use "_loop_X_parent" as the parent variable since that's the stored handle
    std::string loop_parent_var = "_loop_" + std::to_string(my_loop_id) + "_parent";
    std::stringstream item_ss;
    int temp_counter = counter;
    std::map<std::string, int> temp_comp_counters = component_counters;
    
    // For HTML-only loops, we need to capture the root element variable name
    // The counter before generation tells us what ID the root element will get
    int root_element_id = temp_counter;
    
    for(auto& child : children) {
        generate_view_child(child.get(), item_ss, loop_parent_var, temp_counter, click_handlers, bindings, temp_comp_counters, method_names, parent_component_name, true, nullptr, nullptr);
    }
    region.item_creation_code = item_ss.str();
    
    // For HTML-only loops, store the root element variable name
    if (region.is_html_loop && loop_html_element) {
        region.root_element_var = "_el_" + std::to_string(root_element_id);
    }
    
    // Generate the item update code (just prop setting + update calls, no creation)
    // This is used when reconciling - updating existing items with new index values
    if (loop_component && !region.component_type.empty()) {
        std::stringstream update_ss;
        std::string vec_name = "_loop_" + region.component_type + "s";
        std::string inst_ref = vec_name + "[" + var_name + "]";
        
        for(auto& prop : loop_component->props) {
            std::string val = prop.value->to_webcc();
            if(method_names.count(val)) {
                // Function prop - regenerate the lambda
                update_ss << "            " << inst_ref << "." << prop.name << " = [this]() { this->" << val << "(); };\n";
            } else if (prop.is_reference) {
                update_ss << "            " << inst_ref << "." << prop.name << " = &(" << val << ");\n";
                update_ss << "            " << inst_ref << "._update_" << prop.name << "();\n";
            } else {
                update_ss << "            " << inst_ref << "." << prop.name << " = " << val << ";\n";
                update_ss << "            " << inst_ref << "._update_" << prop.name << "();\n";
            }
        }
        region.item_update_code = update_ss.str();
    }
    
    loop_regions->push_back(region);
    
    // In the view() method, store the parent handle and call the sync function
    ss << "        _loop_" << my_loop_id << "_parent = " << parent << ";\n";
    ss << "        _sync_loop_" << my_loop_id << "();\n";
}

void ViewForRangeStatement::collect_dependencies(std::set<std::string>& deps) {
    start->collect_dependencies(deps);
    end->collect_dependencies(deps);
    for(auto& child : children) {
        child->collect_dependencies(deps);
    }
}

// ViewForEachStatement - for item in items in view
void ViewForEachStatement::generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                  std::vector<std::tuple<int, std::string, bool>>& click_handlers,
                  std::vector<Binding>& bindings,
                  std::map<std::string, int>& component_counters,
                  const std::set<std::string>& method_names,
                  const std::string& parent_component_name,
                  bool in_loop,
                  std::vector<LoopRegion>* loop_regions,
                  int* loop_counter,
                  std::vector<IfRegion>* if_regions,
                  int* if_counter) {
    // For now, foreach loops are not reactive (would need more complex tracking)
    ss << "        for (auto& " << var_name << " : " << iterable->to_webcc() << ") {\n";
    for(auto& child : children) {
        generate_view_child(child.get(), ss, parent, counter, click_handlers, bindings, component_counters, method_names, parent_component_name, true, nullptr, nullptr, nullptr, nullptr);
    }
    ss << "        }\n";
}

void ViewForEachStatement::collect_dependencies(std::set<std::string>& deps) {
    iterable->collect_dependencies(deps);
    for(auto& child : children) {
        child->collect_dependencies(deps);
    }
}

void Component::collect_child_components(ASTNode* node, std::map<std::string, int>& counts) {
    if(auto comp = dynamic_cast<ComponentInstantiation*>(node)) {
        counts[comp->component_name]++;
    }
    if(auto el = dynamic_cast<HTMLElement*>(node)) {
        for(auto& child : el->children) {
            collect_child_components(child.get(), counts);
        }
    }
    if(auto viewIf = dynamic_cast<ViewIfStatement*>(node)) {
        for(auto& child : viewIf->then_children) {
            collect_child_components(child.get(), counts);
        }
        for(auto& child : viewIf->else_children) {
            collect_child_components(child.get(), counts);
        }
    }
    // DON'T collect components inside for loops - they are created dynamically
    // if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(node)) { ... }
    // if(auto viewForEach = dynamic_cast<ViewForEachStatement*>(node)) { ... }
}

// Collect component types used inside for loops (need vector members)
static void collect_loop_components(ASTNode* node, std::set<std::string>& loop_components, bool in_loop = false) {
    if(auto comp = dynamic_cast<ComponentInstantiation*>(node)) {
        if (in_loop) {
            loop_components.insert(comp->component_name);
        }
    }
    if(auto el = dynamic_cast<HTMLElement*>(node)) {
        for(auto& child : el->children) {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
    }
    if(auto viewIf = dynamic_cast<ViewIfStatement*>(node)) {
        for(auto& child : viewIf->then_children) {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
        for(auto& child : viewIf->else_children) {
            collect_loop_components(child.get(), loop_components, in_loop);
        }
    }
    if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(node)) {
        for(auto& child : viewFor->children) {
            collect_loop_components(child.get(), loop_components, true);
        }
    }
    if(auto viewForEach = dynamic_cast<ViewForEachStatement*>(node)) {
        for(auto& child : viewForEach->children) {
            collect_loop_components(child.get(), loop_components, true);
        }
    }
}

void Component::collect_child_updates(ASTNode* node, std::map<std::string, std::vector<std::string>>& updates, std::map<std::string, int>& counters) {
    if(auto comp = dynamic_cast<ComponentInstantiation*>(node)) {
        std::string instance_name = comp->component_name + "_" + std::to_string(counters[comp->component_name]++);
        
        for(const auto& prop : comp->props) {
            if(prop.is_reference) {
                std::set<std::string> deps;
                prop.value->collect_dependencies(deps);
                for(const auto& dep : deps) {
                    updates[dep].push_back("        " + instance_name + "._update_" + prop.name + "();\n");
                }
            }
        }
    }
    if(auto el = dynamic_cast<HTMLElement*>(node)) {
        for(auto& child : el->children) {
            collect_child_updates(child.get(), updates, counters);
        }
    }
    if(auto viewIf = dynamic_cast<ViewIfStatement*>(node)) {
        for(auto& child : viewIf->then_children) {
            collect_child_updates(child.get(), updates, counters);
        }
        for(auto& child : viewIf->else_children) {
            collect_child_updates(child.get(), updates, counters);
        }
    }
    // Skip for loops - dynamically created components can't have update callbacks wired up
    // if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(node)) { ... }
    // if(auto viewForEach = dynamic_cast<ViewForEachStatement*>(node)) { ... }
}

std::string Component::to_webcc() {
    std::stringstream ss;
    std::vector<std::tuple<int, std::string, bool>> click_handlers;
    std::vector<Binding> bindings;
    std::map<std::string, int> component_counters; // For generating code
    std::map<std::string, int> component_members; // For declaring members
    std::set<std::string> loop_component_types; // Component types used in loops (need vectors)
    std::vector<LoopRegion> loop_regions; // Track reactive loop regions
    std::vector<IfRegion> if_regions; // Track reactive if/else regions
    int element_count = 0;
    int loop_counter = 0;
    int if_counter = 0;
    
    // Populate global context for reference props
    g_ref_props.clear();
    for(auto& prop : props) {
        if(prop->is_reference) {
            g_ref_props.insert(prop->name);
        }
    }
    
    // Collect child components to declare members
    for(auto& root : render_roots) {
        collect_child_components(root.get(), component_members);
        collect_loop_components(root.get(), loop_component_types);
    }

    // Collect method names
    std::set<std::string> method_names;
    for(auto& m : methods) method_names.insert(m.name);

    std::stringstream ss_render;
    for(auto& root : render_roots){
        if(auto el = dynamic_cast<HTMLElement*>(root.get())){
            el->generate_code(ss_render, "parent", element_count, click_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        } else if(auto comp = dynamic_cast<ComponentInstantiation*>(root.get())){
            comp->generate_code(ss_render, "parent", element_count, click_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        } else if(auto viewIf = dynamic_cast<ViewIfStatement*>(root.get())){
            viewIf->generate_code(ss_render, "parent", element_count, click_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        } else if(auto viewFor = dynamic_cast<ViewForRangeStatement*>(root.get())){
            viewFor->generate_code(ss_render, "parent", element_count, click_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        } else if(auto viewForEach = dynamic_cast<ViewForEachStatement*>(root.get())){
            viewForEach->generate_code(ss_render, "parent", element_count, click_handlers, bindings, component_counters, method_names, name, false, &loop_regions, &loop_counter, &if_regions, &if_counter);
        }
    }

    // Generate component as a class
    ss << "class " << name << " {\n";
    
    ss << "public:\n";

    // Structs
    for(auto& s : structs){
        ss << s->to_webcc() << "\n";
    }
    
    // Props
    for(auto& prop : props){
            ss << "    " << convert_type(prop->type);
            if(prop->is_reference) {
                // Reference props are stored as pointers
                ss << "* " << prop->name << " = nullptr";
            } else {
                ss << " " << prop->name;
                if(prop->default_value){
                    ss << " = " << prop->default_value->to_webcc();
                }
            }
            ss << ";\n";
            
            // For reference props, also declare an onChange callback
            if(prop->is_reference && prop->is_mutable) {
                std::string callback_name = "on" + std::string(1, std::toupper(prop->name[0])) + prop->name.substr(1) + "Change";
                ss << "    webcc::function<void()> " << callback_name << ";\n";
            }
    }
    
    // State variables
    for(auto& var : state){
        ss << "    " << (var->is_mutable ? "" : "const ") << convert_type(var->type);
        if(var->is_reference) ss << "&";
        ss << " " << var->name;
        if(var->initializer){
            // Use brace initialization for handle types to work with typed_handle's explicit constructor
            if (SchemaLoader::instance().is_handle(var->type)) {
                ss << "{" << var->initializer->to_webcc() << "}";
            } else {
                ss << " = " << var->initializer->to_webcc();
            }
        }
        ss << ";\n";
    }
    
    // No special constructor needed since ref props are now pointers initialized to nullptr

    ss << "private:\n";

    // Element handles
    for(int i=0; i<element_count; ++i) {
        ss << "    webcc::handle el_" << i << ";\n";
    }
    
    // Child component members (for components NOT in loops)
    for(auto const& [comp_name, count] : component_members) {
        for(int i=0; i<count; ++i) {
            ss << "    " << comp_name << " " << comp_name << "_" << i << ";\n";
        }
    }
    
    // Vector members for components in loops
    for(const auto& comp_name : loop_component_types) {
        ss << "    webcc::vector<" << comp_name << "> _loop_" << comp_name << "s;\n";
    }
    
    // Loop region tracking (parent element and current count for each reactive loop)
    for(const auto& region : loop_regions) {
        ss << "    webcc::handle _loop_" << region.loop_id << "_parent;\n";
        ss << "    int _loop_" << region.loop_id << "_count = 0;\n";
        // For HTML-only loops, add a vector to track root elements
        if (region.is_html_loop) {
            ss << "    webcc::vector<webcc::handle> _loop_" << region.loop_id << "_elements;\n";
        }
    }
    
    // If region tracking (parent element and current branch state for each reactive if)
    for(const auto& region : if_regions) {
        ss << "    webcc::handle _if_" << region.if_id << "_parent;\n";
        ss << "    bool _if_" << region.if_id << "_state = false;\n";
    }

    ss << "\npublic:\n";

    // Build a map of state variable -> update code for that variable
    std::map<std::string, std::string> var_update_code;
    for(const auto& binding : bindings) {
        for(const auto& dep : binding.dependencies) {
            std::string el_var = "el_" + std::to_string(binding.element_id);
            std::string update_line;
            
            bool optimized = false;
            if(binding.expr) {
                if(auto strLit = dynamic_cast<StringLiteral*>(binding.expr)) {
                    std::string fmt_code = "{ webcc::formatter<256> _fmt; ";
                    auto parts = strLit->parse();
                    for(auto& p : parts) {
                        if(p.is_expr) fmt_code += "_fmt << (" + p.content + "); ";
                        else fmt_code += "_fmt << \"" + p.content + "\"; ";
                    }
                    
                    if(binding.type == "attr") {
                        fmt_code += "webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", _fmt.c_str()); }";
                    } else {
                        fmt_code += "webcc::dom::set_inner_text(" + el_var + ", _fmt.c_str()); }";
                    }
                    update_line = "        " + fmt_code + "\n";
                    optimized = true;
                }
            }
            
            if(!optimized) {
                if(binding.type == "attr") {
                    update_line = "        webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", " + binding.value_code + ");\n";
                } else if(binding.type == "text") {
                    update_line = "        webcc::dom::set_inner_text(" + el_var + ", " + binding.value_code + ");\n";
                }
            }
            
            if(!update_line.empty()) {
                var_update_code[dep] += update_line;
            }
        }
    }
    
    // Generate _update_{varname}() methods for variables that have UI bindings
    std::set<std::string> generated_updaters;
    for(const auto& [var_name, update_code] : var_update_code) {
        if(!update_code.empty()) {
            ss << "    void _update_" << var_name << "() {\n";
            ss << update_code;
            ss << "    }\n";
            generated_updaters.insert(var_name);
        }
    }

    // Ensure all props have an update method, even if empty, so loop reconciliation can call them safely
    for(const auto& prop : props) {
        if(generated_updaters.find(prop->name) == generated_updaters.end()) {
             ss << "    void _update_" << prop->name << "() {}\n";
             generated_updaters.insert(prop->name);
        }
    }
    
    // Generate _sync_loop_X() methods for reactive loops
    // Map from variable to loop IDs that depend on it
    std::map<std::string, std::vector<int>> var_to_loop_ids;
    for(const auto& region : loop_regions) {
        for(const auto& dep : region.dependencies) {
            var_to_loop_ids[dep].push_back(region.loop_id);
        }
    }
    
    for(const auto& region : loop_regions) {
        ss << "    void _sync_loop_" << region.loop_id << "() {\n";
        ss << "        int new_count = " << region.end_expr << " - " << region.start_expr << ";\n";
        ss << "        int old_count = _loop_" << region.loop_id << "_count;\n";
        ss << "        if (new_count == old_count) return;\n";
        ss << "        \n";
        
        if (!region.component_type.empty()) {
            std::string vec_name = "_loop_" + region.component_type + "s";
            
            ss << "        if (new_count > old_count) {\n";
            ss << "            for (int " << region.var_name << " = old_count; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";
            
            // Insert the item creation code with proper indentation
            std::string item_code = region.item_creation_code;
            std::stringstream indented;
            std::istringstream iss(item_code);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    indented << "    " << line << "\n";
                }
            }
            ss << indented.str();
            ss << "            }\n";
            
            // After adding, rebind handlers for all items (vector may have reallocated)
            ss << "            for (int _i = 0; _i < old_count; _i++) " << vec_name << "[_i]._rebind();\n";
            
            ss << "        } else {\n";
            ss << "            while ((int)" << vec_name << ".size() > new_count) {\n";
            ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
            ss << "                " << vec_name << ".pop_back();\n";
            ss << "            }\n";
            
            // Update remaining items' props
            if (!region.item_update_code.empty()) {
                ss << "            for (int " << region.var_name << " = 0; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";
                ss << region.item_update_code;
                ss << "            }\n";
            }
            ss << "        }\n";
        } else if (region.is_html_loop) {
            // HTML-only loops
            std::string vec_name = "_loop_" + std::to_string(region.loop_id) + "_elements";
            
            ss << "        if (new_count > old_count) {\n";
            ss << "            for (int " << region.var_name << " = old_count; " << region.var_name << " < new_count; " << region.var_name << "++) {\n";
            
            std::string item_code = region.item_creation_code;
            std::stringstream indented;
            std::istringstream iss(item_code);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    indented << "    " << line << "\n";
                }
            }
            ss << indented.str();
            
            if (!region.root_element_var.empty()) {
                ss << "            " << vec_name << ".push_back(" << region.root_element_var << ");\n";
            }
            ss << "            }\n";
            ss << "        } else {\n";
            ss << "            while ((int)" << vec_name << ".size() > new_count) {\n";
            ss << "                webcc::dom::remove_element(" << vec_name << "[" << vec_name << ".size() - 1]);\n";
            ss << "                " << vec_name << ".pop_back();\n";
            ss << "            }\n";
            ss << "        }\n";
        }
        ss << "        _loop_" << region.loop_id << "_count = new_count;\n";
        ss << "    }\n";
    }
    
    // Generate _sync_if_X() methods for reactive if/else
    // Map from variable to if IDs that depend on it
    std::map<std::string, std::vector<int>> var_to_if_ids;
    for(const auto& region : if_regions) {
        for(const auto& dep : region.dependencies) {
            var_to_if_ids[dep].push_back(region.if_id);
        }
    }
    
    for(const auto& region : if_regions) {
        ss << "    void _sync_if_" << region.if_id << "() {\n";
        ss << "        bool new_state = " << region.condition_code << ";\n";
        ss << "        if (new_state == _if_" << region.if_id << "_state) return;\n";
        ss << "        _if_" << region.if_id << "_state = new_state;\n";
        ss << "        \n";
        
        // Destroy old branch elements and create new ones
        ss << "        if (new_state) {\n";
        // Destroy else branch elements
        for (int el_id : region.else_element_ids) {
            ss << "            webcc::dom::remove_element(el_" << el_id << ");\n";
        }
        // Destroy else branch components
        for (const auto& [comp_name, inst_id] : region.else_components) {
            ss << "            " << comp_name << "_" << inst_id << "._destroy();\n";
        }
        // Destroy else branch loops
        for (int loop_id : region.else_loop_ids) {
            // Check if this is a component loop or HTML loop
            bool found = false;
            for (const auto& lr : loop_regions) {
                if (lr.loop_id == loop_id) {
                    if (!lr.component_type.empty()) {
                        std::string vec_name = "_loop_" + lr.component_type + "s";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    } else if (lr.is_html_loop) {
                        std::string vec_name = "_loop_" + std::to_string(loop_id) + "_elements";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                webcc::dom::remove_element(" << vec_name << "[" << vec_name << ".size() - 1]);\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    }
                    found = true;
                    break;
                }
            }
        }
        // Recursively sync nested ifs in else branch to destroy them
        for (int nested_if_id : region.else_if_ids) {
            // Just destroy elements tracked in that if region
            for (const auto& nested_region : if_regions) {
                if (nested_region.if_id == nested_if_id) {
                    // Destroy both branches of nested if since we're removing it entirely
                    for (int el_id : nested_region.then_element_ids) {
                        ss << "            if (_if_" << nested_if_id << "_state) webcc::dom::remove_element(el_" << el_id << ");\n";
                    }
                    for (int el_id : nested_region.else_element_ids) {
                        ss << "            if (!_if_" << nested_if_id << "_state) webcc::dom::remove_element(el_" << el_id << ");\n";
                    }
                }
            }
        }
        // Create then branch 
        ss << region.then_creation_code;
        
        ss << "        } else {\n";
        // Destroy then branch elements
        for (int el_id : region.then_element_ids) {
            ss << "            webcc::dom::remove_element(el_" << el_id << ");\n";
        }
        // Destroy then branch components
        for (const auto& [comp_name, inst_id] : region.then_components) {
            ss << "            " << comp_name << "_" << inst_id << "._destroy();\n";
        }
        // Destroy then branch loops
        for (int loop_id : region.then_loop_ids) {
            bool found = false;
            for (const auto& lr : loop_regions) {
                if (lr.loop_id == loop_id) {
                    if (!lr.component_type.empty()) {
                        std::string vec_name = "_loop_" + lr.component_type + "s";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                " << vec_name << "[" << vec_name << ".size() - 1]._destroy();\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    } else if (lr.is_html_loop) {
                        std::string vec_name = "_loop_" + std::to_string(loop_id) + "_elements";
                        ss << "            while ((int)" << vec_name << ".size() > 0) {\n";
                        ss << "                webcc::dom::remove_element(" << vec_name << "[" << vec_name << ".size() - 1]);\n";
                        ss << "                " << vec_name << ".pop_back();\n";
                        ss << "            }\n";
                        ss << "            _loop_" << loop_id << "_count = 0;\n";
                    }
                    found = true;
                    break;
                }
            }
        }
        // Recursively sync nested ifs in then branch to destroy them
        for (int nested_if_id : region.then_if_ids) {
            for (const auto& nested_region : if_regions) {
                if (nested_region.if_id == nested_if_id) {
                    for (int el_id : nested_region.then_element_ids) {
                        ss << "            if (_if_" << nested_if_id << "_state) webcc::dom::remove_element(el_" << el_id << ");\n";
                    }
                    for (int el_id : nested_region.else_element_ids) {
                        ss << "            if (!_if_" << nested_if_id << "_state) webcc::dom::remove_element(el_" << el_id << ");\n";
                    }
                }
            }
        }
        // Create else branch
        if (!region.else_creation_code.empty()) {
            ss << region.else_creation_code;
        }
        
        ss << "        }\n";
        ss << "    }\n";
    }

    // Build a map of state variable -> child component updates
    std::map<std::string, std::vector<std::string>> child_updates;
    std::map<std::string, int> update_counters;
    for(auto& root : render_roots) {
        collect_child_updates(root.get(), child_updates, update_counters);
    }

    // Methods
    for(auto& method : methods){
        std::set<std::string> modified_vars;
        method.collect_modifications(modified_vars);
        
        std::string updates;
        // Call the update function for each modified variable that has one
        // Note: For "init" method, we skip _update and _sync_loop calls because init runs
        // before the DOM is built. The view() method handles initial rendering after DOM creation.
        bool is_init_method = (method.name == "init");
        for(const auto& mod : modified_vars) {
            if(generated_updaters.count(mod) && !is_init_method) {
                updates += "        _update_" + mod + "();\n";
            }
            // Add child updates
            if(child_updates.count(mod) && !is_init_method) {
                for(const auto& call : child_updates[mod]) {
                    updates += call;
                }
            }
            // Call if sync for any if statements that depend on this variable
            // Skip for init since DOM isn't ready yet
            if(var_to_if_ids.count(mod) && !is_init_method) {
                for(int if_id : var_to_if_ids[mod]) {
                    updates += "        _sync_if_" + std::to_string(if_id) + "();\n";
                }
            }
            // Call loop sync for any loops that depend on this variable
            // Skip for init since DOM isn't ready yet
            if(var_to_loop_ids.count(mod) && !is_init_method) {
                for(int loop_id : var_to_loop_ids[mod]) {
                    updates += "        _sync_loop_" + std::to_string(loop_id) + "();\n";
                }
            }
        }
        
        // For any modified reference props, call their onChange callback
        for(const auto& mod : modified_vars) {
            if(g_ref_props.count(mod)) {
                // Only call if mutable (we can check this by seeing if the callback member exists, 
                // but here we are in the component definition, so we should check the prop definition.
                // However, g_ref_props only stores names.
                // But wait, if the prop is NOT mutable, the compiler would have thrown an error 
                // in validate_mutability if we tried to modify it.
                // So if we are here, and it is modified, it MUST be mutable.
                std::string callback_name = "on" + std::string(1, std::toupper(mod[0])) + mod.substr(1) + "Change";
                updates += "        if(" + callback_name + ") " + callback_name + "();\n";
            }
        }
        
        std::string original_name = method.name;
        if (method.name == "tick") {
            method.name = "_user_tick";
        } else if (method.name == "init") {
            method.name = "_user_init";
        }
        ss << "    " << method.to_webcc(updates);
        if (original_name == "tick") {
            method.name = original_name;
        } else if (original_name == "init") {
            method.name = original_name;
        }
    }

    // Generated handlers
    for(auto& handler : click_handlers) {
        ss << "    void _handler_" << std::get<0>(handler) << "() {\n";
        if (std::get<2>(handler)) {
            ss << "        " << std::get<1>(handler) << ";\n";
        } else {
            ss << "        " << std::get<1>(handler) << "();\n";
        }
        ss << "    }\n";
    }

    // View method (Initialization only)
    ss << "    void view(webcc::handle parent = webcc::dom::get_body()) {\n";
    bool has_init = false;
    for(auto& m : methods) if(m.name == "init") has_init = true;
    if(has_init) ss << "        _user_init();\n";
    if(!render_roots.empty()){
        ss << ss_render.str();
    }
    // Register handlers
    for(auto& handler : click_handlers) {
        ss << "        g_dispatcher.set(el_" << std::get<0>(handler) << ", [this]() { this->_handler_" << std::get<0>(handler) << "(); });\n";
    }
    ss << "    }\n";
    
    // Re-bind handlers (used after vector reallocation invalidates this pointers)
    if (!click_handlers.empty()) {
        ss << "    void _rebind() {\n";
        for(auto& handler : click_handlers) {
            ss << "        g_dispatcher.set(el_" << std::get<0>(handler) << ", [this]() { this->_handler_" << std::get<0>(handler) << "(); });\n";
        }
        ss << "    }\n";
    }
    
    // Destroy method - unregisters handlers and removes the component's root element from the DOM
    ss << "    void _destroy() {\n";
    for(auto& handler : click_handlers) {
        ss << "        g_dispatcher.remove(el_" << std::get<0>(handler) << ");\n";
    }
    if (element_count > 0) {
        ss << "        webcc::dom::remove_element(el_0);\n";
    }
    ss << "    }\n";

    // Update method for event loop
    ss << "    void tick(float dt) {\n";
    
    // Call user tick if exists
    bool has_tick = false;
    for(auto& m : methods) if(m.name == "tick") has_tick = true;
    if(has_tick) ss << "        _user_tick(dt);\n";

    // Update children
    for(auto const& [comp_name, count] : component_members) {
        for(int i=0; i<count; ++i) {
            ss << "        " << comp_name << "_" << i << ".tick(dt);\n";
        }
    }
    ss << "    }\n";

    ss << "};\n";

    // Clear global context
    g_ref_props.clear();
    
    return ss.str();
}
