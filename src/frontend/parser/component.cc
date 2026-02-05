#include "parser.h"
#include "defs/def_parser.h"
#include "cli/error.h"
#include <stdexcept>
#include <cctype>

std::unique_ptr<DataDef> Parser::parse_data()
{
    expect(TokenType::POD, "Expected 'pod'");
    std::string name = current().value;
    int name_line = current().line;
    expect(TokenType::IDENTIFIER, "Expected pod name");

    // Pod type names must start with uppercase (convention for type names)
    if (!name.empty() && !std::isupper(name[0]))
    {
        ErrorHandler::compiler_error("Pod type name '" + name + "' must start with an uppercase letter", name_line);
    }

    expect(TokenType::LBRACE, "Expected '{'");

    auto def = std::make_unique<DataDef>();
    def->name = name;

    while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
    {
        std::string type = current().value;
        // Handle types (excluding VOID - not valid for data fields)
        if (current().type == TokenType::INT || current().type == TokenType::STRING ||
            current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
            current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER)
        {
            advance();
        }
        else
        {
            ErrorHandler::compiler_error("Expected type in pod field", current().line);
        }

        std::string fieldName = current().value;
        expect(TokenType::IDENTIFIER, "Expected field name");
        expect(TokenType::SEMICOLON, "Expected ';'");

        def->fields.push_back({type, fieldName});
    }
    expect(TokenType::RBRACE, "Expected '}'");
    return def;
}

std::unique_ptr<EnumDef> Parser::parse_enum()
{
    expect(TokenType::ENUM, "Expected 'enum'");
    std::string name = current().value;
    int name_line = current().line;
    expect(TokenType::IDENTIFIER, "Expected enum name");

    // Enum type names must start with uppercase (convention for type names)
    if (!name.empty() && !std::isupper(name[0]))
    {
        ErrorHandler::compiler_error("Enum type name '" + name + "' must start with an uppercase letter", name_line);
    }

    expect(TokenType::LBRACE, "Expected '{'");

    auto def = std::make_unique<EnumDef>();
    def->name = name;

    while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
    {
        std::string valueName = current().value;
        expect(TokenType::IDENTIFIER, "Expected enum value name");
        def->values.push_back(valueName);

        // Allow optional comma between values
        if (current().type == TokenType::COMMA)
        {
            advance();
        }
    }
    expect(TokenType::RBRACE, "Expected '}'");
    return def;
}

std::unique_ptr<RouterDef> Parser::parse_router()
{
    auto router = std::make_unique<RouterDef>();
    router->line = current().line;

    expect(TokenType::ROUTER, "Expected 'router'");
    expect(TokenType::LBRACE, "Expected '{'");

    while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
    {
        RouteEntry entry;
        entry.line = current().line;

        // Parse route path (string literal)
        if (current().type != TokenType::STRING_LITERAL)
        {
            throw std::runtime_error("Expected route path string at line " + std::to_string(current().line));
        }
        entry.path = current().value;
        advance();

        // Expect =>
        if (current().type != TokenType::ARROW)
        {
            throw std::runtime_error("Expected '=>' after route path at line " + std::to_string(current().line));
        }
        advance();

        // Parse component name
        if (current().type != TokenType::IDENTIFIER)
        {
            throw std::runtime_error("Expected component name after '=>' at line " + std::to_string(current().line));
        }
        entry.component_name = current().value;
        advance();

        // Optional: parse component arguments (ComponentName(arg1, arg2))
        // Uses same syntax as component construction: &ref, :move, name = value
        if (current().type == TokenType::LPAREN)
        {
            advance();
            entry.args = parse_call_args(TokenType::RPAREN);
            expect(TokenType::RPAREN, "Expected ')' after component arguments");
        }

        router->routes.push_back(std::move(entry));

        // Optional comma between entries
        if (current().type == TokenType::COMMA)
        {
            advance();
        }
    }

    expect(TokenType::RBRACE, "Expected '}'");

    if (router->routes.empty())
    {
        throw std::runtime_error("Router block must have at least one route at line " + std::to_string(router->line));
    }

    return router;
}

void Parser::parse_app()
{
    expect(TokenType::LBRACE, "Expected '{'");

    while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
    {
        std::string key = current().value;
        expect(TokenType::IDENTIFIER, "Expected key");
        expect(TokenType::ASSIGN, "Expected '='");

        if (key == "root")
        {
            app_config.root_component = current().value;
            expect(TokenType::IDENTIFIER, "Expected component name");
        }
        else if (key == "title")
        {
            app_config.title = current().value;
            expect(TokenType::STRING_LITERAL, "Expected string");
        }
        else if (key == "description")
        {
            app_config.description = current().value;
            expect(TokenType::STRING_LITERAL, "Expected string");
        }
        else if (key == "lang")
        {
            app_config.lang = current().value;
            expect(TokenType::STRING_LITERAL, "Expected string");
        }
        else if (key == "routes")
        {
            expect(TokenType::LBRACE, "Expected '{'");
            while (current().type != TokenType::RBRACE)
            {
                std::string route = current().value;
                expect(TokenType::STRING_LITERAL, "Expected route string");
                expect(TokenType::COLON, "Expected ':'");
                std::string comp = current().value;
                expect(TokenType::IDENTIFIER, "Expected component name");
                app_config.routes[route] = comp;

                if (current().type == TokenType::COMMA)
                    advance();
            }
            expect(TokenType::RBRACE, "Expected '}'");
        }
        expect(TokenType::SEMICOLON, "Expected ';'");
    }
    expect(TokenType::RBRACE, "Expected '}'");
}

Component Parser::parse_component()
{
    Component comp;

    // Clear component member types from previous component
    component_member_types.clear();
    component_array_types.clear();

    expect(TokenType::COMPONENT, "Expected 'component'");
    comp.name = current().value;
    comp.line = current().line;

    // Check for collisions with built-in types
    if (DefSchema::instance().is_handle(comp.name))
    {
        throw std::runtime_error("Component name '" + comp.name + "' conflicts with a built-in type name at line " + std::to_string(current().line));
    }

    // Validate component name starts with uppercase
    if (!comp.name.empty() && !std::isupper(comp.name[0]))
    {
        throw std::runtime_error("Component name '" + comp.name + "' must start with an uppercase letter at line " + std::to_string(current().line));
    }

    expect(TokenType::IDENTIFIER, "Expected component name");

    // Parse component parameters (constructor-style): component Name(pub mut int& value = 0)
    if (match(TokenType::LPAREN))
    {
        while (current().type != TokenType::RPAREN)
        {
            auto param = std::make_unique<ComponentParam>();

            // Check for pub keyword (makes param accessible from outside)
            if (current().type == TokenType::PUB)
            {
                param->is_public = true;
                advance();
            }

            // Check for mut keyword
            if (current().type == TokenType::MUT)
            {
                param->is_mutable = true;
                advance();
            }

            // Parse type
            if (current().type == TokenType::DEF)
            {
                // Function parameter: def onclick : void  OR  def onRemove(int) : void
                advance();
                param->is_callback = true;
                param->name = current().value;
                if (is_identifier_token())
                {
                    advance();
                }
                else
                {
                    expect(TokenType::IDENTIFIER, "Expected param name");
                }

                // Check for optional parameter list: (type1, type2, ...)
                std::vector<std::string> callback_params;
                if (current().type == TokenType::LPAREN)
                {
                    advance();
                    while (current().type != TokenType::RPAREN && current().type != TokenType::END_OF_FILE)
                    {
                        std::string param_type = current().value;
                        if (current().type == TokenType::INT || current().type == TokenType::STRING ||
                            current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
                            current().type == TokenType::BOOL ||
                            current().type == TokenType::IDENTIFIER || current().type == TokenType::VOID)
                        {
                            advance();
                        }
                        else
                        {
                            throw std::runtime_error("Expected parameter type in callback definition");
                        }

                        // Handle array type
                        if (current().type == TokenType::LBRACKET)
                        {
                            advance();
                            expect(TokenType::RBRACKET, "Expected ']'");
                            param_type += "[]";
                        }

                        callback_params.push_back(param_type);
                        param->callback_param_types.push_back(param_type);

                        if (current().type == TokenType::COMMA)
                        {
                            advance();
                        }
                        else
                        {
                            break;
                        }
                    }
                    expect(TokenType::RPAREN, "Expected ')' after callback parameters");
                }

                expect(TokenType::COLON, "Expected ':'");

                std::string retType = current().value;
                if (is_type_token())
                {
                    advance();
                }
                else
                {
                    throw std::runtime_error("Expected return type");
                }

                // Build the webcc::function type with parameter types
                std::string params_str;
                for (size_t i = 0; i < callback_params.size(); ++i)
                {
                    if (i > 0)
                        params_str += ", ";
                    params_str += convert_type(callback_params[i]);
                }
                param->type = "webcc::function<" + retType + "(" + params_str + ")>";
            }
            else
            {
                param->type = current().value;
                if (is_type_token())
                {
                    advance();
                }
                else
                {
                    throw std::runtime_error("Expected param type");
                }

                // Handle reference type
                if (current().type == TokenType::AMPERSAND)
                {
                    param->is_reference = true;
                    advance();
                }

                // Handle array type
                if (current().type == TokenType::LBRACKET)
                {
                    advance();
                    expect(TokenType::RBRACKET, "Expected ']'");
                    param->type += "[]";
                }

                param->name = current().value;
                if (is_identifier_token())
                {
                    advance();
                }
                else
                {
                    expect(TokenType::IDENTIFIER, "Expected param name");
                }
            }

            // Parse default value
            if (match(TokenType::ASSIGN))
            {
                param->default_value = parse_expression();
            }

            comp.params.push_back(std::move(param));

            if (current().type == TokenType::COMMA)
            {
                advance();
            }
        }
        expect(TokenType::RPAREN, "Expected ')'");
    }

    expect(TokenType::LBRACE, "Expected '{'");

    // Parse state variables, methods, view, style, and router blocks
    while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
    {
        bool is_public = false;
        bool is_mutable = false;
        bool is_shared = false;

        // Check for shared keyword (only if followed by declaration keyword - not when used as variable name)
        // If followed by = or . then it's a variable name, otherwise it's the shared modifier
        if (current().type == TokenType::SHARED &&
            peek().type != TokenType::ASSIGN && peek().type != TokenType::DOT)
        {
            is_shared = true;
            advance();
        }

        // Check for pub keyword
        if (current().type == TokenType::PUB)
        {
            is_public = true;
            advance();
        }

        // Check for mut keyword
        if (current().type == TokenType::MUT)
        {
            is_mutable = true;
            advance();
        }

        // Variable declaration (note: VOID not valid here, only in return types)
        if (current().type == TokenType::INT || current().type == TokenType::STRING ||
            current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
            current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER)
        {
            auto var_decl = std::make_unique<VarDeclaration>();
            var_decl->type = current().value;
            var_decl->is_public = is_public;
            advance();

            // Handle Component.EnumName type syntax for shared enums
            if (current().type == TokenType::DOT)
            {
                advance();
                var_decl->type += "." + current().value;
                expect(TokenType::IDENTIFIER, "Expected enum name after '.'");
            }

            // Handle reference type
            if (current().type == TokenType::AMPERSAND)
            {
                var_decl->is_reference = true;
                advance();
            }

            if (current().type == TokenType::LBRACKET)
            {
                advance();
                if (current().type == TokenType::INT_LITERAL)
                {
                    // Fixed-size array: Type[N]
                    std::string size = current().value;
                    advance();
                    expect(TokenType::RBRACKET, "Expected ']'");
                    var_decl->type += "[" + size + "]";
                }
                else
                {
                    // Dynamic array: Type[]
                    expect(TokenType::RBRACKET, "Expected ']'");
                    var_decl->type += "[]";
                }
            }

            var_decl->name = current().value;
            if (is_identifier_token())
            {
                advance();
            }
            else
            {
                expect(TokenType::IDENTIFIER, "Expected variable name");
            }
            var_decl->is_mutable = is_mutable;

            if (match(TokenType::ASSIGN))
            {
                var_decl->initializer = parse_expression();
            }

            if (var_decl->is_reference && !var_decl->initializer)
            {
                throw std::runtime_error("Reference variable '" + var_decl->name + "' must be initialized immediately.");
            }

            // Track component-type members for view parsing (e.g., "mut Test a;" -> can use <a/> in view)
            // Component types start with uppercase and are not arrays
            if (!var_decl->type.empty() && std::isupper(var_decl->type[0]) &&
                var_decl->type.find('[') == std::string::npos)
            {
                component_member_types[var_decl->name] = var_decl->type;
            }

            // Track component array types (e.g., "Row[] rows" -> can use <row/> in for loops)
            if (!var_decl->type.empty() && var_decl->type.ends_with("[]"))
            {
                std::string elem_type = var_decl->type.substr(0, var_decl->type.length() - 2);
                if (!elem_type.empty() && std::isupper(elem_type[0]))
                {
                    component_array_types[var_decl->name] = elem_type;
                }
            }

            expect(TokenType::SEMICOLON, "Expected ';'");
            comp.state.push_back(std::move(var_decl));
        }
        else if (is_mutable && !is_public && current().type != TokenType::DEF)
        {
            throw std::runtime_error("Expected variable declaration after 'mut'");
        }
        // Pod definition
        else if (current().type == TokenType::POD)
        {
            comp.data.push_back(parse_data());
        }
        // Enum definition (with optional shared prefix)
        else if (current().type == TokenType::ENUM)
        {
            auto enum_def = parse_enum();
            enum_def->is_shared = is_shared;
            if (is_shared)
            {
                enum_def->owner_component = comp.name;
            }
            comp.enums.push_back(std::move(enum_def));
        }
        // Function definition (with optional pub prefix)
        else if (current().type == TokenType::DEF)
        {
            advance();
            FunctionDef func;
            func.is_public = is_public;
            func.name = current().value;
            int func_line = current().line;
            expect(TokenType::IDENTIFIER, "Expected function name");

            // Method names must start with lowercase (to distinguish from component/type construction)
            if (!func.name.empty() && std::isupper(func.name[0]))
            {
                ErrorHandler::compiler_error("Method name '" + func.name + "' must start with a lowercase letter", func_line);
            }

            expect(TokenType::LPAREN, "Expected '('");

            // Parse parameters
            while (current().type != TokenType::RPAREN)
            {
                bool is_mutable_param = false;
                if (current().type == TokenType::MUT)
                {
                    is_mutable_param = true;
                    advance();
                }

                std::string paramType = current().value;
                if (current().type == TokenType::INT || current().type == TokenType::FLOAT ||
                    current().type == TokenType::FLOAT32 ||
                    current().type == TokenType::STRING || current().type == TokenType::BOOL ||
                    current().type == TokenType::IDENTIFIER)
                {
                    advance();
                    // Check for array type: Type[]
                    if (current().type == TokenType::LBRACKET)
                    {
                        advance();
                        expect(TokenType::RBRACKET, "Expected ']' for array type");
                        paramType += "[]";
                    }
                }
                else
                {
                    throw std::runtime_error("Expected parameter type");
                }

                bool is_reference = false;
                if (current().type == TokenType::AMPERSAND)
                {
                    is_reference = true;
                    advance();
                }

                std::string paramName = current().value;
                if (is_identifier_token())
                {
                    advance();
                }
                else
                {
                    throw std::runtime_error("Expected parameter name at line " + std::to_string(current().line));
                }

                func.params.push_back({paramType, paramName, is_mutable_param, is_reference});

                if (current().type == TokenType::COMMA)
                {
                    advance();
                }
            }

            expect(TokenType::RPAREN, "Expected ')'");
            if (current().type == TokenType::LBRACE)
            {
                throw std::runtime_error("Missing return type for function '" + func.name + "'. Expected ':' followed by return type at line " + std::to_string(current().line));
            }
            expect(TokenType::COLON, "Expected ':' for return type");
            func.return_type = current().value;
            advance();
            expect(TokenType::LBRACE, "Expected '{'");

            while (current().type != TokenType::RBRACE)
            {
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
        }
        // Init block
        else if (current().type == TokenType::INIT)
        {
            advance();
            FunctionDef func;
            func.name = "init";
            func.return_type = "void";
            expect(TokenType::LBRACE, "Expected '{'");

            while (current().type != TokenType::RBRACE)
            {
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
        }
        // Mount block (runs after view is created)
        else if (current().type == TokenType::MOUNT)
        {
            advance();
            FunctionDef func;
            func.name = "mount";
            func.return_type = "void";
            expect(TokenType::LBRACE, "Expected '{'");

            while (current().type != TokenType::RBRACE)
            {
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
        }
        // Tick definition
        else if (current().type == TokenType::TICK)
        {
            advance();
            FunctionDef func;
            func.name = "tick";
            func.return_type = "void";

            // Parameters are optional - tick {} or tick(float dt) {}
            if (current().type == TokenType::LPAREN)
            {
                advance();
                // Parse parameters
                while (current().type != TokenType::RPAREN)
                {
                    bool is_mutable_param = false;
                    if (current().type == TokenType::MUT)
                    {
                        is_mutable_param = true;
                        advance();
                    }

                    std::string paramType = current().value;
                    if (current().type == TokenType::INT || current().type == TokenType::FLOAT ||
                        current().type == TokenType::FLOAT32 ||
                        current().type == TokenType::STRING || current().type == TokenType::BOOL ||
                        current().type == TokenType::IDENTIFIER)
                    {
                        advance();
                        // Check for array type: Type[]
                        if (current().type == TokenType::LBRACKET)
                        {
                            advance();
                            expect(TokenType::RBRACKET, "Expected ']' for array type");
                            paramType += "[]";
                        }
                    }
                    else
                    {
                        throw std::runtime_error("Expected parameter type");
                    }

                    bool is_reference = false;
                    if (current().type == TokenType::AMPERSAND)
                    {
                        is_reference = true;
                        advance();
                    }

                    std::string paramName = current().value;
                    if (is_identifier_token())
                    {
                        advance();
                    }
                    else
                    {
                        throw std::runtime_error("Expected parameter name at line " + std::to_string(current().line));
                    }

                    func.params.push_back({paramType, paramName, is_mutable_param, is_reference});

                    if (current().type == TokenType::COMMA)
                    {
                        advance();
                    }
                }
                expect(TokenType::RPAREN, "Expected ')'");
            }

            expect(TokenType::LBRACE, "Expected '{'");

            while (current().type != TokenType::RBRACE)
            {
                func.body.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}'");
            comp.methods.push_back(std::move(func));
        }
        // Style block
        else if (current().type == TokenType::STYLE)
        {
            advance();
            bool is_global = false;
            if (current().type == TokenType::IDENTIFIER && current().value == "global")
            {
                is_global = true;
                advance();
            }
            std::string css = parse_style_block();
            if (is_global)
            {
                comp.global_css += css + "\n";
            }
            else
            {
                comp.css += css + "\n";
            }
        }
        // Router block
        else if (current().type == TokenType::ROUTER)
        {
            if (comp.router)
            {
                throw std::runtime_error("Component '" + comp.name + "' already has a router block at line " + std::to_string(current().line));
            }
            comp.router = parse_router();
        }
        // View block
        else if (current().type == TokenType::VIEW)
        {
            advance();
            expect(TokenType::LBRACE, "Expected '{'");
            while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
            {
                comp.render_roots.push_back(parse_view_node());
            }
            expect(TokenType::RBRACE, "Expected '}'");
        }
        else
        {
            advance();
        }
    }

    return comp;
}
