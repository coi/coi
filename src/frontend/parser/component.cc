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

    auto def = std::make_unique<DataDef>();
    def->name = name;

    // Parse generic type parameters: pod Result<T> { ... }
    if (current().type == TokenType::LT)
    {
        advance(); // skip '<'
        while (current().type != TokenType::GT && current().type != TokenType::END_OF_FILE)
        {
            std::string type_param = current().value;
            expect(TokenType::IDENTIFIER, "Expected type parameter name");
            
            // Type parameter names must start with uppercase
            if (!type_param.empty() && !std::isupper(type_param[0]))
            {
                ErrorHandler::compiler_error("Type parameter '" + type_param + "' must start with an uppercase letter", current().line);
            }
            
            def->type_params.push_back(type_param);
            
            if (current().type == TokenType::COMMA)
            {
                advance();
            }
            else
            {
                break;
            }
        }
        expect(TokenType::GT, "Expected '>' after type parameters");
    }

    expect(TokenType::LBRACE, "Expected '{'");

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

        // Handle generic type arguments: Result<int>, Pair<A, B>
        if (current().type == TokenType::LT)
        {
            type += "<";
            advance(); // skip '<'
            bool first = true;
            while (current().type != TokenType::GT && current().type != TokenType::END_OF_FILE)
            {
                if (!first) type += ", ";
                first = false;
                
                std::string arg_type = current().value;
                if (current().type == TokenType::INT || current().type == TokenType::STRING ||
                    current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
                    current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER)
                {
                    advance();
                }
                else
                {
                    ErrorHandler::compiler_error("Expected type argument", current().line);
                }
                type += arg_type;
                
                // Handle array type or map type as type argument: Result<int[]> or Result<int[string]>
                if (current().type == TokenType::LBRACKET)
                {
                    advance();
                    type += parse_type_bracket_suffix();
                }
                
                if (current().type == TokenType::COMMA)
                {
                    advance();
                }
                else
                {
                    break;
                }
            }
            expect(TokenType::GT, "Expected '>' after type arguments");
            type += ">";
        }

        // Handle array suffix: T[] or map: V[K]
        if (current().type == TokenType::LBRACKET)
        {
            advance();
            type += parse_type_bracket_suffix();
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

        // Check for 'else' (default route)
        if (current().type == TokenType::ELSE)
        {
            advance();
            entry.is_default = true;
            entry.path = "";  // No path for default route
        }
        else
        {
            // Parse route path (string literal)
            if (current().type != TokenType::STRING_LITERAL)
            {
                throw std::runtime_error("Expected route path string or 'else' at line " + std::to_string(current().line));
            }
            entry.path = current().value;
            advance();
        }

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

        // Require semicolon after each route entry
        expect(TokenType::SEMICOLON, "Expected ';' after route entry");
    }

    expect(TokenType::RBRACE, "Expected '}'");

    if (router->routes.empty())
    {
        throw std::runtime_error("Router block must have at least one route at line " + std::to_string(router->line));
    }

    return router;
}

SignalDef Parser::parse_signal(bool is_public)
{
    SignalDef signal;
    signal.is_public = is_public;
    signal.line = current().line;

    expect(TokenType::SIGNAL, "Expected 'signal'");

    signal.name = current().value;
    expect(TokenType::IDENTIFIER, "Expected signal name");

    expect(TokenType::LPAREN, "Expected '(' after signal name");
    int param_index = 0;
    while (current().type != TokenType::RPAREN && current().type != TokenType::END_OF_FILE)
    {
        SignalParam param;
        param.type = current().value;

        if (is_type_token())
        {
            advance();
        }
        else
        {
            throw std::runtime_error("Expected signal parameter type");
        }

        if (current().type == TokenType::LBRACKET)
        {
            advance();
            param.type += parse_type_bracket_suffix();
        }

        if (is_identifier_token())
        {
            param.name = current().value;
            advance();
        }
        else
        {
            param.name = "arg" + std::to_string(param_index);
        }

        signal.params.push_back(std::move(param));
        param_index++;

        if (current().type == TokenType::COMMA)
        {
            advance();
        }
        else
        {
            break;
        }
    }

    expect(TokenType::RPAREN, "Expected ')' after signal parameters");
    expect(TokenType::SEMICOLON, "Expected ';' after signal declaration");
    return signal;
}

void Parser::parse_listen_block(Component &comp)
{
    expect(TokenType::LISTEN, "Expected 'listen'");
    expect(TokenType::LBRACE, "Expected '{' after listen");

    while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
    {
        ListenEntry entry;
        entry.line = current().line;

        if (current().type != TokenType::IDENTIFIER)
        {
            throw std::runtime_error("Expected component instance name in listen entry at line " + std::to_string(current().line));
        }

        entry.target_name = current().value;
        advance();

        for (const auto &param : comp.params)
        {
            if (param->name == entry.target_name && param->is_reference)
            {
                entry.target_is_reference = true;
                break;
            }
        }
        if (!entry.target_is_reference)
        {
            for (const auto &var : comp.state)
            {
                if (var->name == entry.target_name && var->is_reference)
                {
                    entry.target_is_reference = true;
                    break;
                }
            }
        }

        expect(TokenType::DOT, "Expected '.' before signal name in listen entry");
        entry.signal_name = current().value;
        expect(TokenType::IDENTIFIER, "Expected signal name in listen entry");

        expect(TokenType::ARROW, "Expected '=>' in listen entry");

        expect(TokenType::LPAREN, "Expected '(' for listener parameters");
        std::vector<FunctionDef::Param> listener_params;
        while (current().type != TokenType::RPAREN && current().type != TokenType::END_OF_FILE)
        {
            bool is_mutable = false;
            if (current().type == TokenType::MUT)
            {
                is_mutable = true;
                advance();
            }

            std::string param_type = current().value;
            if (is_type_token())
            {
                advance();
            }
            else
            {
                throw std::runtime_error("Expected listener parameter type at line " + std::to_string(current().line));
            }

            if (current().type == TokenType::LBRACKET)
            {
                advance();
                param_type += parse_type_bracket_suffix();
            }

            std::string param_name = current().value;
            if (is_identifier_token())
            {
                advance();
            }
            else
            {
                throw std::runtime_error("Expected listener parameter name at line " + std::to_string(current().line));
            }

            listener_params.push_back({param_type, param_name, is_mutable, false});
            entry.param_types.push_back(param_type);

            if (current().type == TokenType::COMMA)
            {
                advance();
            }
            else
            {
                break;
            }
        }
        expect(TokenType::RPAREN, "Expected ')' after listener parameters");

        std::string method_name = "_listen_" + std::to_string(comp.listen_entries.size());
        entry.handler_method_name = method_name;

        FunctionDef handler;
        handler.name = method_name;
        handler.return_type = "void";
        handler.params = std::move(listener_params);

        expect(TokenType::LBRACE, "Expected '{' to start listener body");
        while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
        {
            handler.body.push_back(parse_statement());
        }
        expect(TokenType::RBRACE, "Expected '}' after listener body");

        comp.methods.push_back(std::move(handler));
        comp.listen_entries.push_back(std::move(entry));

        if (current().type == TokenType::SEMICOLON)
        {
            advance();
        }
    }

    expect(TokenType::RBRACE, "Expected '}' after listen block");
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

    // Check for collisions with built-in types (only for non-namespaced components)
    // Namespaced components like Supabase::Storage can safely shadow built-in types
    if (module_name.empty() && DefSchema::instance().is_handle(comp.name))
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

                        // Handle array type or map type
                        if (current().type == TokenType::LBRACKET)
                        {
                            advance();
                            param_type += parse_type_bracket_suffix();
                        }

                        // Skip optional parameter name (for documentation purposes)
                        if (current().type == TokenType::IDENTIFIER)
                        {
                            advance();
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

                // Handle array type or map type
                if (current().type == TokenType::LBRACKET)
                {
                    advance();
                    param->type += parse_type_bracket_suffix();
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
            current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER ||
            (current().type == TokenType::DEF && is_mutable))
        {
            auto var_decl = std::make_unique<VarDeclaration>();
            var_decl->is_public = is_public;
            if (current().type == TokenType::DEF)
            {
                // Function-typed variable declaration
                // Supported syntax:
                //   def name(type1, type2) : ret
                advance(); // consume 'def'

                std::string parsed_name;
                std::vector<std::string> callback_params;

                if (is_identifier_token() && peek().type == TokenType::LPAREN)
                {
                    parsed_name = current().value;
                    advance();
                }
                else
                {
                    throw std::runtime_error("Expected function variable name after 'def'");
                }

                expect(TokenType::LPAREN, "Expected '(' in function type declaration");
                while (current().type != TokenType::RPAREN && current().type != TokenType::END_OF_FILE)
                {
                    std::string param_type = current().value;
                    if (current().type == TokenType::INT || current().type == TokenType::STRING ||
                        current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
                        current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER ||
                        current().type == TokenType::VOID)
                    {
                        advance();
                    }
                    else
                    {
                        throw std::runtime_error("Expected parameter type in function type declaration");
                    }

                    // Handle array type or map type
                    if (current().type == TokenType::LBRACKET)
                    {
                        advance();
                        param_type += parse_type_bracket_suffix();
                    }

                    // Skip optional parameter name
                    if (is_identifier_token())
                    {
                        advance();
                    }

                    callback_params.push_back(param_type);

                    if (current().type == TokenType::COMMA)
                    {
                        advance();
                    }
                    else
                    {
                        break;
                    }
                }
                expect(TokenType::RPAREN, "Expected ')' after function type parameters");
                expect(TokenType::COLON, "Expected ':' in function type declaration");

                std::string ret_type = current().value;
                if (is_type_token())
                {
                    advance();
                }
                else
                {
                    throw std::runtime_error("Expected return type in function type declaration");
                }

                std::string params_str;
                for (size_t i = 0; i < callback_params.size(); ++i)
                {
                    if (i > 0)
                        params_str += ", ";
                    params_str += convert_type(callback_params[i]);
                }
                var_decl->type = "webcc::function<" + convert_type(ret_type) + "(" + params_str + ")>";
                var_decl->name = parsed_name;
            }
            else
            {
                var_decl->type = current().value;
                advance();

                // Handle Module::Type syntax for namespaced types
                if (current().type == TokenType::DOUBLE_COLON)
                {
                    advance();
                    var_decl->type += "::" + current().value;
                    expect(TokenType::IDENTIFIER, "Expected type name after '::'");
                }

                // Handle generic type arguments: Result<int>, Pair<A, B>
                if (current().type == TokenType::LT)
                {
                    var_decl->type += "<";
                    advance(); // skip '<'
                    int depth = 1;
                    while (depth > 0 && current().type != TokenType::END_OF_FILE)
                    {
                        if (current().type == TokenType::LT)
                        {
                            var_decl->type += "<";
                            depth++;
                            advance();
                        }
                        else if (current().type == TokenType::GT)
                        {
                            depth--;
                            if (depth > 0) var_decl->type += ">";
                            advance();
                        }
                        else if (current().type == TokenType::COMMA)
                        {
                            var_decl->type += ", ";
                            advance();
                        }
                        else if (current().type == TokenType::LBRACKET)
                        {
                            advance();
                            var_decl->type += parse_type_bracket_suffix();
                        }
                        else if (is_type_token() || current().type == TokenType::IDENTIFIER)
                        {
                            var_decl->type += current().value;
                            advance();
                        }
                        else
                        {
                            throw std::runtime_error("Unexpected token in generic type arguments");
                        }
                    }
                    var_decl->type += ">";
                }

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
                    var_decl->type += parse_type_bracket_suffix();
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

            // Parse generic type parameters: def first<T>(...) : T
            if (current().type == TokenType::LT)
            {
                advance(); // skip '<'
                while (current().type != TokenType::GT && current().type != TokenType::END_OF_FILE)
                {
                    std::string type_param = current().value;
                    expect(TokenType::IDENTIFIER, "Expected type parameter name");
                    
                    // Type parameter names must start with uppercase
                    if (!type_param.empty() && !std::isupper(type_param[0]))
                    {
                        ErrorHandler::compiler_error("Type parameter '" + type_param + "' must start with an uppercase letter", current().line);
                    }
                    
                    func.type_params.push_back(type_param);
                    
                    if (current().type == TokenType::COMMA)
                    {
                        advance();
                    }
                    else
                    {
                        break;
                    }
                }
                expect(TokenType::GT, "Expected '>' after type parameters");
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

                std::string paramType;
                std::string paramName;
                bool parsed_function_param = false;

                if (current().type == TokenType::DEF)
                {
                    // Function-typed method parameter
                    // Supported syntax:
                    //   def handler(type1, type2) : ret
                    parsed_function_param = true;
                    advance(); // consume 'def'

                    std::vector<std::string> callback_params;

                    if (is_identifier_token() && peek().type == TokenType::LPAREN)
                    {
                        paramName = current().value;
                        advance();
                    }
                    else
                    {
                        throw std::runtime_error("Expected function parameter name after 'def'");
                    }

                    expect(TokenType::LPAREN, "Expected '(' in function parameter type");
                    while (current().type != TokenType::RPAREN && current().type != TokenType::END_OF_FILE)
                    {
                        std::string callback_param_type = current().value;
                        if (current().type == TokenType::INT || current().type == TokenType::FLOAT ||
                            current().type == TokenType::FLOAT32 || current().type == TokenType::STRING ||
                            current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER ||
                            current().type == TokenType::VOID)
                        {
                            advance();
                        }
                        else
                        {
                            throw std::runtime_error("Expected parameter type in function parameter");
                        }

                        if (current().type == TokenType::LBRACKET)
                        {
                            advance();
                            callback_param_type += parse_type_bracket_suffix();
                        }

                        // Optional parameter name in callback signature
                        if (is_identifier_token())
                        {
                            advance();
                        }

                        callback_params.push_back(callback_param_type);

                        if (current().type == TokenType::COMMA)
                        {
                            advance();
                        }
                        else
                        {
                            break;
                        }
                    }
                    expect(TokenType::RPAREN, "Expected ')' in function parameter type");
                    expect(TokenType::COLON, "Expected ':' for function parameter return type");

                    std::string retType = current().value;
                    if (is_type_token())
                    {
                        advance();
                    }
                    else
                    {
                        throw std::runtime_error("Expected return type in function parameter");
                    }

                    std::string params_str;
                    for (size_t i = 0; i < callback_params.size(); ++i)
                    {
                        if (i > 0)
                            params_str += ", ";
                        params_str += convert_type(callback_params[i]);
                    }
                    paramType = "webcc::function<" + convert_type(retType) + "(" + params_str + ")>";
                }
                else
                {
                    paramType = current().value;
                    if (current().type == TokenType::INT || current().type == TokenType::FLOAT ||
                        current().type == TokenType::FLOAT32 ||
                        current().type == TokenType::STRING || current().type == TokenType::BOOL ||
                        current().type == TokenType::IDENTIFIER)
                    {
                        advance();
                        // Check for array/map type: Type[] or Type[N] or Type[KeyType]
                        if (current().type == TokenType::LBRACKET)
                        {
                            advance();
                            paramType += parse_type_bracket_suffix();
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

                    paramName = current().value;
                    if (is_identifier_token())
                    {
                        advance();
                    }
                    else
                    {
                        throw std::runtime_error("Expected parameter name at line " + std::to_string(current().line));
                    }

                    func.params.push_back({paramType, paramName, is_mutable_param, is_reference});
                }

                if (parsed_function_param)
                {
                    // Function parameters are not reference parameters in syntax today
                    func.params.push_back({paramType, paramName, is_mutable_param, false});
                }

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

            // Single return type
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
                        // Check for array/map type: Type[] or Type[N] or Type[KeyType]
                        if (current().type == TokenType::LBRACKET)
                        {
                            advance();
                            paramType += parse_type_bracket_suffix();
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
        // Signal declaration
        else if (current().type == TokenType::SIGNAL)
        {
            if (is_mutable)
            {
                throw std::runtime_error("Signal declarations cannot use 'mut' at line " + std::to_string(current().line));
            }
            if (is_shared)
            {
                throw std::runtime_error("Signal declarations cannot use 'shared' at line " + std::to_string(current().line));
            }

            SignalDef signal = parse_signal(is_public);

            bool duplicate = false;
            for (const auto &existing : comp.signals)
            {
                if (existing.name == signal.name)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                throw std::runtime_error("Duplicate signal '" + signal.name + "' in component '" + comp.name + "' at line " + std::to_string(signal.line));
            }

            comp.signals.push_back(std::move(signal));
        }
        // Listen block
        else if (current().type == TokenType::LISTEN)
        {
            parse_listen_block(comp);
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
