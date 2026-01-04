#include "ast.h"
#include <cctype>
#include <algorithm>

std::string convert_type(const std::string& type) {
    if (type == "string") return "webcc::string";
    if (type.ends_with("[]")) {
        std::string inner = type.substr(0, type.length() - 2);
        return "SimpleVector<" + convert_type(inner) + ">";
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

std::string Identifier::to_webcc() {return name;}
void Identifier::collect_dependencies(std::set<std::string>& deps) {
    deps.insert(name);
}

BinaryOp::BinaryOp(std::unique_ptr<Expression> l, const std::string& o, std::unique_ptr<Expression> r)
    : left(std::move(l)), op(o), right(std::move(r)){}

std::string BinaryOp::to_webcc() {
    return "(" + left->to_webcc() + " " + op + " " + right->to_webcc() + ")";
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

    std::string result = name + "(";
    for(size_t i = 0; i < args.size(); i++){
        if(i > 0) result += ", ";
        result += args[i]->to_webcc();
    }
    result += ")";
    return result;
}
void FunctionCall::collect_dependencies(std::set<std::string>& deps) {
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

std::string VarDeclaration::to_webcc() {
    std::string result = convert_type(type) + " " + name;
    if(initializer) {
        result += " = " + initializer->to_webcc();
    }
    result += ";";
    return result;
}

std::string PropDeclaration::to_webcc() {
    return "";
}

std::string Assignment::to_webcc() {
    return name + " = " + value->to_webcc() + ";";
}
void Assignment::collect_dependencies(std::set<std::string>& deps) {
    value->collect_dependencies(deps);
}

std::string ReturnStatement::to_webcc() {
    return "return " + value->to_webcc() + ";";
}
void ReturnStatement::collect_dependencies(std::set<std::string>& deps) {
    value->collect_dependencies(deps);
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

void collect_mods_recursive(Statement* stmt, std::set<std::string>& mods) {
    if(auto assign = dynamic_cast<Assignment*>(stmt)) {
        mods.insert(assign->name);
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
}

std::string FunctionDef::to_webcc(const std::string& injected_code){
    std::string result = convert_type(return_type) + " " + name + "(";
    for(size_t i = 0; i < params.size(); i++){
        if(i > 0) result += ", ";
        result += convert_type(params[i].first) + " " + params[i].second;
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

void FunctionDef::collect_modifications(std::set<std::string>& mods) {
    for(auto& stmt : body) {
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
                  std::vector<std::pair<int, std::string>>& click_handlers,
                  std::vector<Binding>& bindings,
                  std::map<std::string, int>& component_counters,
                  const std::set<std::string>& method_names,
                  const std::string& parent_component_name) {
    
    int id = component_counters[component_name]++;
    std::string instance_name = component_name + "_" + std::to_string(id);
    
    // Set props
    for(auto& prop : props) {
        std::string val = prop.second->to_webcc();
        // Check if val is a method name
        if(method_names.count(val)) {
            // Wrap in function
            ss << "        " << instance_name << "." << prop.first << " = [this]() { this->" << val << "(); };\n";
        } else {
            ss << "        " << instance_name << "." << prop.first << " = " << val << ";\n";
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
        prop.second->collect_dependencies(deps);
    }
}

std::string HTMLElement::to_webcc() { return ""; }

void HTMLElement::generate_code(std::stringstream& ss, const std::string& parent, int& counter, 
                  std::vector<std::pair<int, std::string>>& click_handlers,
                  std::vector<Binding>& bindings,
                  std::map<std::string, int>& component_counters,
                  const std::set<std::string>& method_names,
                  const std::string& parent_component_name) {
    int my_id = counter++;
    std::string var = "el_" + std::to_string(my_id);
    
    // Creation block
    ss << "        " << var << " = webcc::dom::create_element(\"" << tag << "\");\n";
    ss << "        webcc::dom::set_attribute(" << var << ", \"coi-scope\", \"" << parent_component_name << "\");\n";
    
    // Attributes
    for(auto& attr : attributes){
        if(attr.name == "onclick"){
             ss << "        webcc::dom::add_click_listener(" << var << ");\n";
             // Store handler for later generation
             click_handlers.push_back({my_id, attr.value->to_webcc()});
        } else {
             std::string val = attr.value->to_webcc();
             ss << "        webcc::dom::set_attribute(" << var << ", \"" << attr.name << "\", " << val << ");\n";
             
             if(!attr.value->is_static()) {
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
        if(dynamic_cast<HTMLElement*>(child.get()) || dynamic_cast<ComponentInstantiation*>(child.get())) has_elements = true;
    }

    if(has_elements){
         for(auto& child : children){
             if(auto el = dynamic_cast<HTMLElement*>(child.get())){
                 el->generate_code(ss, var, counter, click_handlers, bindings, component_counters, method_names, parent_component_name);
             } else if(auto comp = dynamic_cast<ComponentInstantiation*>(child.get())){
                 comp->generate_code(ss, var, counter, click_handlers, bindings, component_counters, method_names, parent_component_name);
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
             
             if(!all_static) {
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

void Component::collect_child_components(ASTNode* node, std::map<std::string, int>& counts) {
    if(auto comp = dynamic_cast<ComponentInstantiation*>(node)) {
        counts[comp->component_name]++;
    }
    if(auto el = dynamic_cast<HTMLElement*>(node)) {
        for(auto& child : el->children) {
            collect_child_components(child.get(), counts);
        }
    }
}

std::string Component::to_webcc() {
    std::stringstream ss;
    std::vector<std::pair<int, std::string>> click_handlers;
    std::vector<Binding> bindings;
    std::map<std::string, int> component_counters; // For generating code
    std::map<std::string, int> component_members; // For declaring members
    int element_count = 0;
    
    // Collect child components to declare members
    if(render_root) {
        collect_child_components(render_root.get(), component_members);
    }

    // Collect method names
    std::set<std::string> method_names;
    for(auto& m : methods) method_names.insert(m.name);

    std::stringstream ss_render;
    if(render_root){
        if(auto el = dynamic_cast<HTMLElement*>(render_root.get())){
            // Auto-bind onclick if not present
            bool has_onclick_attr = false;
            for(auto& attr : el->attributes) if(attr.name == "onclick") has_onclick_attr = true;
            
            if(!has_onclick_attr) {
                HTMLAttribute attr;
                attr.name = "onclick";
                attr.value = std::make_unique<Identifier>("onclick");
                el->attributes.push_back(std::move(attr));
            }

            el->generate_code(ss_render, "parent", element_count, click_handlers, bindings, component_counters, method_names, name);
        } else if(auto comp = dynamic_cast<ComponentInstantiation*>(render_root.get())){
            comp->generate_code(ss_render, "parent", element_count, click_handlers, bindings, component_counters, method_names, name);
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
            ss << "    " << convert_type(prop->type) << " " << prop->name;
            if(prop->default_value){
                ss << " = " << prop->default_value->to_webcc();
            }
            ss << ";\n";
    }
    
    // Special prop for onclick if not present
    bool has_onclick = false;
    for(auto& prop : props) if(prop->name == "onclick") has_onclick = true;
    if(!has_onclick) {
        ss << "    webcc::function<void()> onclick;\n";
    }

    // State variables
    for(auto& var : state){
        ss << "    " << convert_type(var->type) << " " << var->name;
        if(var->initializer){
            ss << " = " << var->initializer->to_webcc();
        }
        ss << ";\n";
    }

    ss << "private:\n";

    // Element handles
    for(int i=0; i<element_count; ++i) {
        ss << "    webcc::handle el_" << i << ";\n";
    }
    
    // Child component members
    for(auto const& [comp_name, count] : component_members) {
        for(int i=0; i<count; ++i) {
            ss << "    " << comp_name << " " << comp_name << "_" << i << ";\n";
        }
    }

    ss << "\npublic:\n";

    // Methods
    for(auto& method : methods){
        std::set<std::string> modified_vars;
        method.collect_modifications(modified_vars);
        
        std::string updates;
        // Deduplicate updates by iterating over bindings and checking if any dependency is modified
        for(const auto& binding : bindings) {
            bool needs_update = false;
            for(const auto& mod : modified_vars) {
                if(binding.dependencies.count(mod)) {
                    needs_update = true;
                    break;
                }
            }
            
            if(needs_update) {
                std::string el_var = "el_" + std::to_string(binding.element_id);
                
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
                            updates += "        " + fmt_code + "\n";
                            optimized = true;
                    }
                }

                if(!optimized) {
                    if(binding.type == "attr") {
                        updates += "        webcc::dom::set_attribute(" + el_var + ", \"" + binding.name + "\", " + binding.value_code + ");\n";
                    } else if(binding.type == "text") {
                        updates += "        webcc::dom::set_inner_text(" + el_var + ", " + binding.value_code + ");\n";
                    }
                }
            }
        }
        
        std::string original_name = method.name;
        if (method.name == "tick") {
            method.name = "_user_tick";
        }
        ss << "    " << method.to_webcc(updates);
        if (original_name == "tick") {
            method.name = original_name;
        }
    }

    // Generated handlers
    for(auto& handler : click_handlers) {
        ss << "    void _handler_" << handler.first << "() {\n";
        ss << "        " << handler.second << "();\n";
        ss << "    }\n";
    }

    // View method (Initialization only)
    ss << "    void view(webcc::handle parent = webcc::dom::get_body()) {\n";
    if(render_root){
        ss << ss_render.str();
    }
    // Register handlers
    for(auto& handler : click_handlers) {
        ss << "        g_dispatcher.register_click(el_" << handler.first << ", [this]() { this->_handler_" << handler.first << "(); });\n";
    }
    ss << "        webcc::flush();\n";
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

    return ss.str();
}
