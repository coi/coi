#include "parser.h"
#include "defs/def_parser.h"
#include "cli/error.h"
#include <stdexcept>
#include <cctype>

// Parse a prop/attribute value: string, number (with optional unary -), or {expression}
std::unique_ptr<Expression> Parser::parse_prop_or_attr_value()
{
    int err_line = current().line;

    if (current().type == TokenType::STRING_LITERAL)
    {
        auto val = std::make_unique<StringLiteral>(current().value);
        advance();
        return val;
    }

    if (current().type == TokenType::INT_LITERAL)
    {
        auto val = std::make_unique<IntLiteral>(std::stoi(current().value));
        advance();
        return val;
    }

    if (current().type == TokenType::FLOAT_LITERAL)
    {
        auto val = std::make_unique<FloatLiteral>(std::stod(current().value));
        advance();
        return val;
    }

    if (match(TokenType::MINUS))
    {
        if (current().type == TokenType::INT_LITERAL)
        {
            auto val = std::make_unique<IntLiteral>(-std::stoi(current().value));
            advance();
            return val;
        }
        else if (current().type == TokenType::FLOAT_LITERAL)
        {
            auto val = std::make_unique<FloatLiteral>(-std::stod(current().value));
            advance();
            return val;
        }
        ErrorHandler::compiler_error("Expected number after '-' in prop or attribute value", err_line);
    }

    if (match(TokenType::LBRACE))
    {
        auto expr = parse_expression();
        expect(TokenType::RBRACE, "Expected '}'");
        return expr;
    }

    ErrorHandler::compiler_error("Expected prop or attribute value", err_line);
}

std::string Parser::parse_style_block()
{
    expect(TokenType::LBRACE, "Expected '{'");
    std::string css = "";
    int brace_count = 1;

    Token prev = tokens[pos - 1]; // The '{' we just consumed

    while (current().type != TokenType::END_OF_FILE)
    {
        if (current().type == TokenType::RBRACE && brace_count == 1)
        {
            advance(); // Consume closing '}'
            break;
        }

        if (current().type == TokenType::LBRACE)
            brace_count++;
        if (current().type == TokenType::RBRACE)
            brace_count--;

        Token tok = current();

        int prev_len = prev.value.length();
        if (prev.type == TokenType::STRING_LITERAL)
            prev_len += 2;

        if (tok.line > prev.line)
        {
            css += " ";
        }
        else if (tok.column > prev.column + prev_len)
        {
            css += " ";
        }

        if (tok.type == TokenType::STRING_LITERAL)
        {
            css += "\"" + tok.value + "\"";
        }
        else
        {
            css += tok.value;
        }

        prev = tok;
        advance();
    }
    return css;
}

std::unique_ptr<ASTNode> Parser::parse_html_element()
{
    expect(TokenType::LT, "Expected '<'");
    int start_line = current().line;

    auto parse_component_props = [&](ComponentInstantiation &comp) {
        while (current().type == TokenType::IDENTIFIER || current().type == TokenType::AMPERSAND || current().type == TokenType::COLON)
        {
            bool is_ref_prop = false;
            bool is_move_prop = false;
            if (match(TokenType::AMPERSAND))
            {
                is_ref_prop = true;
            }
            else if (match(TokenType::COLON))
            {
                is_move_prop = true;
            }

            std::string prop_name = current().value;
            expect(TokenType::IDENTIFIER, "Expected prop name");

            std::unique_ptr<Expression> prop_value;
            if (match(TokenType::ASSIGN))
            {
                prop_value = parse_prop_or_attr_value();
            }
            else
            {
                prop_value = std::make_unique<BoolLiteral>(true);
            }

            ComponentProp cprop;
            cprop.name = prop_name;
            cprop.value = std::move(prop_value);
            cprop.is_reference = is_ref_prop;
            cprop.is_move = is_move_prop;
            comp.props.push_back(std::move(cprop));
        }
    };

    // Check for component variable syntax: <{varName} props... />
    // Used to project component variables into the view
    if (current().type == TokenType::LBRACE)
    {
        advance(); // consume '{'

        // Parse the expression (typically just an identifier)
        auto expr = parse_expression();
        expect(TokenType::RBRACE, "Expected '}' after component variable expression");

        // Get the variable name from the expression
        std::string member_name;
        std::string component_type;

        if (auto *ident = dynamic_cast<Identifier *>(expr.get()))
        {
            member_name = ident->name;
            // Look up the component type
            auto it = component_member_types.find(member_name);
            if (it != component_member_types.end())
            {
                component_type = it->second;

                // Error if type is a built-in handle (not a component)
                if (DefSchema::instance().is_handle(component_type))
                {
                    throw std::runtime_error("Variable '" + member_name + "' has type '" + component_type + "' which is a built-in type, not a component. Usage: <{" + member_name + "}/> is only for components at line " + std::to_string(start_line));
                }
            }
            else
            {
                throw std::runtime_error("Variable '" + member_name + "' is not a known component member. Use <{var}/> only for component-typed variables at line " + std::to_string(start_line));
            }
        }
        else
        {
            throw std::runtime_error("Expected identifier in <{...}/> syntax at line " + std::to_string(start_line));
        }

        auto comp = std::make_unique<ComponentInstantiation>();
        comp->line = start_line;
        comp->is_member_reference = true;
        comp->member_name = member_name;
        comp->component_name = component_type;

        // Parse props (same as regular component props): &prop={value} = reference, :prop={value} = move
        parse_component_props(*comp);

        // Must be self-closing: <{var}/>
        expect(TokenType::SLASH, "Expected '/>' - component variable projection must be self-closing: <{" + member_name + "}/>");
        expect(TokenType::GT, "Expected '>'");

        return comp;
    }

    std::string tag = current().value;
    expect(TokenType::IDENTIFIER, "Expected tag name");
    // Special tag: <raw> - raw HTML injection
    if (tag == "raw")
    {
        auto rawEl = std::make_unique<ViewRawElement>();
        rawEl->line = start_line;

        expect(TokenType::GT, "Expected '>' after <raw");

        // Parse children (expressions/text) until </raw>
        while (true)
        {
            if (current().type == TokenType::LT && peek().type == TokenType::SLASH)
            {
                break;
            }
            if (current().type == TokenType::END_OF_FILE)
            {
                throw std::runtime_error("Unexpected end of file, expected </raw> at line " + std::to_string(start_line));
            }
            if (current().type == TokenType::LBRACE)
            {
                advance();
                rawEl->children.push_back(parse_expression());
                expect(TokenType::RBRACE, "Expected '}'");
            }
            else
            {
                // Text content
                std::string text;
                bool first = true;
                Token prev_token = current();
                while (current().type != TokenType::LT && current().type != TokenType::LBRACE &&
                       current().type != TokenType::END_OF_FILE)
                {
                    if (!first)
                    {
                        int prev_len = prev_token.value.length();
                        if (prev_token.type == TokenType::STRING_LITERAL)
                            prev_len += 2;
                        if (prev_token.line != current().line || prev_token.column + prev_len != current().column)
                            text += " ";
                    }
                    text += current().value;
                    prev_token = current();
                    advance();
                    first = false;
                }
                if (!text.empty())
                {
                    rawEl->children.push_back(std::make_unique<TextNode>(text));
                }
            }
        }

        // </raw>
        expect(TokenType::LT, "Expected '<'");
        expect(TokenType::SLASH, "Expected '/'");
        if (current().value != "raw")
        {
            throw std::runtime_error("Mismatched closing tag: expected raw, got " + current().value);
        }
        expect(TokenType::IDENTIFIER, "Expected 'raw'");
        expect(TokenType::GT, "Expected '>'");

        return rawEl;
    }
    // Special tag: <route /> - placeholder for router
    if (tag == "route")
    {
        auto route_placeholder = std::make_unique<RoutePlaceholder>();
        route_placeholder->line = start_line;

        // Must be self-closing
        if (current().type != TokenType::SLASH)
        {
            throw std::runtime_error("<route> must be self-closing: <route /> at line " + std::to_string(start_line));
        }
        expect(TokenType::SLASH, "Expected '/>'");
        expect(TokenType::GT, "Expected '>'");

        return route_placeholder;
    }

    // Check for Module::Component syntax (cross-module access)
    std::string module_prefix;
    if (current().type == TokenType::DOUBLE_COLON)
    {
        // tag is actually the module name
        module_prefix = tag;
        advance(); // consume ::
        if (current().type != TokenType::IDENTIFIER)
        {
            ErrorHandler::compiler_error("Expected component name after '" + module_prefix + "::'", current().line);
        }
        tag = current().value;
        advance();
    }

    // Components must start with uppercase
    // Lowercase tags are always HTML elements
    // Use <{var}/> syntax for component variables
    bool is_component = std::isupper(tag[0]);

    if (is_component)
    {
        // Error if tag is a built-in handle type
        if (DefSchema::instance().is_handle(tag))
        {
            ErrorHandler::compiler_error("Type '" + tag + "' cannot be used as a component tag", start_line);
        }

        auto comp = std::make_unique<ComponentInstantiation>();
        comp->line = start_line;
        comp->component_name = tag;
        comp->module_prefix = module_prefix;  // Store module prefix if specified

        // Props: &prop={value} = reference, :prop={value} = move, prop={value} = copy
        parse_component_props(*comp);

        // Self-closing
        if (match(TokenType::SLASH))
        {
            expect(TokenType::GT, "Expected '>'");
            return comp;
        }

        expect(TokenType::GT, "Expected '>'");
        ErrorHandler::compiler_error("Custom components must be self-closing for now: " + tag, -1);
    }

    auto el = std::make_unique<HTMLElement>();
    el->line = start_line;
    el->tag = tag;

    // Attributes - accept any token as attribute name except those that end the tag
    while (current().type != TokenType::SLASH && current().type != TokenType::GT && current().type != TokenType::END_OF_FILE)
    {
        // Check for element ref binding: &={varName}
        if (match(TokenType::AMPERSAND))
        {
            expect(TokenType::ASSIGN, "Expected '=' after '&' for element binding");
            expect(TokenType::LBRACE, "Expected '{' after '&='");
            if (current().type != TokenType::IDENTIFIER)
            {
                throw std::runtime_error("Expected variable name in element binding &={varName}");
            }
            el->ref_binding = current().value;
            advance();
            expect(TokenType::RBRACE, "Expected '}' after variable name");
            continue;
        }

        std::string attrName = current().value;
        advance();

        // Handle hyphenated attribute names (e.g., fill-opacity, stroke-width, data-id)
        while (current().type == TokenType::MINUS && peek().type == TokenType::IDENTIFIER)
        {
            attrName += "-";
            advance(); // consume '-'
            attrName += current().value;
            advance(); // consume identifier part
        }

        std::unique_ptr<Expression> attrValue;
        if (match(TokenType::ASSIGN))
        {
            attrValue = parse_prop_or_attr_value();
        }
        else
        {
            // Boolean attribute? Treat as "true"
            attrValue = std::make_unique<BoolLiteral>(true);
        }
        el->attributes.push_back({attrName, std::move(attrValue)});
    }

    // Self-closing
    if (match(TokenType::SLASH))
    {
        expect(TokenType::GT, "Expected '>'");
        return el;
    }

    expect(TokenType::GT, "Expected '>'");

    // Children
    // Track the last token position to detect leading whitespace for text nodes
    Token last_non_text_token = tokens[pos - 1];  // The '>' we just consumed
    while (true)
    {
        if (current().type == TokenType::LT)
        {
            if (peek().type == TokenType::SLASH)
            {
                // Closing tag
                break;
            }
            // Check for special tags: <if>, <for>
            if (peek().type == TokenType::IF)
            {
                el->children.push_back(parse_view_if());
            }
            else if (peek().type == TokenType::FOR)
            {
                el->children.push_back(parse_view_for());
            }
            else
            {
                // Regular child element
                el->children.push_back(parse_html_element());
            }
            last_non_text_token = tokens[pos - 1];  // Update after parsing element
        }
        else if (current().type == TokenType::LBRACE)
        {
            // Expression
            advance();
            el->children.push_back(parse_expression());
            expect(TokenType::RBRACE, "Expected '}'");
            last_non_text_token = tokens[pos - 1];  // The '}' we just consumed
        }
        else
        {
            // Text content
            std::string text;
            bool first = true;
            Token prev_token = current();
            
            // Check for leading whitespace (gap between last non-text token and first text token)
            int last_len = last_non_text_token.value.length();
            if (last_non_text_token.type == TokenType::STRING_LITERAL)
                last_len += 2;
            if (last_non_text_token.line != current().line || last_non_text_token.column + last_len != current().column)
            {
                text += " ";
            }
            // Text continues until we hit '<' or '{'
            while (current().type != TokenType::LT && current().type != TokenType::LBRACE &&
                   current().type != TokenType::END_OF_FILE)
            {
                if (!first)
                {
                    int prev_len = prev_token.value.length();
                    if (prev_token.type == TokenType::STRING_LITERAL)
                        prev_len += 2;

                    if (prev_token.line != current().line || prev_token.column + prev_len != current().column)
                    {
                        text += " ";
                    }
                }
                if (current().type == TokenType::STRING_LITERAL)
                    text += current().value;
                else
                    text += current().value;

                prev_token = current();
                advance();
                first = false;
            }
            if (!text.empty())
            {
                // Check if there was whitespace between the last text token and the next token (<, {, or EOF)
                // If so, preserve the trailing space
                if (current().type != TokenType::END_OF_FILE)
                {
                    int prev_len = prev_token.value.length();
                    if (prev_token.type == TokenType::STRING_LITERAL)
                        prev_len += 2;

                    if (prev_token.line != current().line || prev_token.column + prev_len != current().column)
                    {
                        text += " ";
                    }
                }
                el->children.push_back(std::make_unique<TextNode>(text));
            }

            if (current().type == TokenType::END_OF_FILE)
                break;
        }
    }

    expect(TokenType::LT, "Expected '<'");
    expect(TokenType::SLASH, "Expected '/'");
    if (current().value != tag)
    {
        throw std::runtime_error("Mismatched closing tag: expected " + tag + ", got " + current().value);
    }
    expect(TokenType::IDENTIFIER, "Expected tag name");
    expect(TokenType::GT, "Expected '>'");

    return el;
}

std::unique_ptr<ASTNode> Parser::parse_view_node()
{
    // Must start with '<'
    if (current().type != TokenType::LT)
    {
        throw std::runtime_error("Expected '<' at line " + std::to_string(current().line));
    }

    // Check for special tags
    if (peek().type == TokenType::IF)
    {
        return parse_view_if();
    }
    if (peek().type == TokenType::FOR)
    {
        return parse_view_for();
    }
    // Regular HTML element
    return parse_html_element();
}

std::unique_ptr<ViewIfStatement> Parser::parse_view_if()
{
    // Syntax: <if condition> ... <else> ... </else> </if>
    //     or: <if condition> ... </if>
    auto viewIf = std::make_unique<ViewIfStatement>();
    viewIf->line = current().line;

    expect(TokenType::LT, "Expected '<'");
    expect(TokenType::IF, "Expected 'if'");

    // Parse condition (everything until '>')
    // Use parse_expression_no_gt so > is not treated as comparison
    viewIf->condition = parse_expression_no_gt();
    expect(TokenType::GT, "Expected '>'");

    // Parse then children until we hit </if> or <else>
    while (current().type != TokenType::END_OF_FILE)
    {
        if (current().type == TokenType::LT)
        {
            if (peek().type == TokenType::SLASH && peek(2).type == TokenType::IF)
            {
                // </if> - end of if block
                break;
            }
            if (peek().type == TokenType::ELSE)
            {
                // <else> block
                break;
            }
        }
        viewIf->then_children.push_back(parse_view_node());
    }

    // Check for <else>
    if (current().type == TokenType::LT && peek().type == TokenType::ELSE)
    {
        advance(); // <
        advance(); // else
        expect(TokenType::GT, "Expected '>'");

        // Parse else children until </else>
        while (current().type != TokenType::END_OF_FILE)
        {
            if (current().type == TokenType::LT && peek().type == TokenType::SLASH && peek(2).type == TokenType::ELSE)
            {
                break;
            }
            viewIf->else_children.push_back(parse_view_node());
        }

        // </else>
        expect(TokenType::LT, "Expected '<'");
        expect(TokenType::SLASH, "Expected '/'");
        expect(TokenType::ELSE, "Expected 'else'");
        expect(TokenType::GT, "Expected '>'");
    }

    // </if>
    expect(TokenType::LT, "Expected '<'");
    expect(TokenType::SLASH, "Expected '/'");
    expect(TokenType::IF, "Expected 'if'");
    expect(TokenType::GT, "Expected '>'");

    return viewIf;
}

std::unique_ptr<ASTNode> Parser::parse_view_for()
{
    // Syntax: <for var in start:end> ... </for>
    //     or: <for var in iterable> ... </for>
    int start_line = current().line;

    expect(TokenType::LT, "Expected '<'");
    expect(TokenType::FOR, "Expected 'for'");

    std::string var_name = current().value;
    expect(TokenType::IDENTIFIER, "Expected loop variable name");
    expect(TokenType::IN, "Expected 'in'");

    // Use parse_expression_no_gt so > is not treated as comparison
    auto first_expr = parse_expression_no_gt();

    // Check if this is a range (has colon) or foreach
    if (current().type == TokenType::COLON)
    {
        // Range: <for i in 0:10>
        advance();
        auto end_expr = parse_expression_no_gt();
        expect(TokenType::GT, "Expected '>'");

        auto viewFor = std::make_unique<ViewForRangeStatement>();
        viewFor->line = start_line;
        viewFor->var_name = var_name;
        viewFor->start = std::move(first_expr);
        viewFor->end = std::move(end_expr);

        // Parse children until </for>
        while (current().type != TokenType::END_OF_FILE)
        {
            if (current().type == TokenType::LT && peek().type == TokenType::SLASH && peek(2).type == TokenType::FOR)
            {
                break;
            }
            viewFor->children.push_back(parse_view_node());
        }

        // </for>
        expect(TokenType::LT, "Expected '<'");
        expect(TokenType::SLASH, "Expected '/'");
        expect(TokenType::FOR, "Expected 'for'");
        expect(TokenType::GT, "Expected '>'");

        return viewFor;
    }
    else
    {
        // ForEach: <for item in items key={item.id}>
        auto viewForEach = std::make_unique<ViewForEachStatement>();
        viewForEach->line = start_line;
        viewForEach->var_name = var_name;
        viewForEach->iterable = std::move(first_expr);

        // Require key attribute for foreach loops
        if (current().type != TokenType::KEY)
        {
            throw std::runtime_error("Expected 'key' for foreach loop at line " + std::to_string(start_line) + ". Use: <for " + var_name + " in array key={" + var_name + ".id}>");
        }
        advance(); // consume 'key'
        expect(TokenType::ASSIGN, "Expected '=' after 'key'");
        expect(TokenType::LBRACE, "Expected '{' for key expression");
        viewForEach->key_expr = parse_expression();
        expect(TokenType::RBRACE, "Expected '}' after key expression");

        expect(TokenType::GT, "Expected '>'");

        // If iterating over a component array, temporarily add loop var to component_member_types
        // so that <var_name/> syntax works inside the loop
        std::string loop_var_comp_type;
        if (auto *ident = dynamic_cast<Identifier *>(viewForEach->iterable.get()))
        {
            auto it = component_array_types.find(ident->name);
            if (it != component_array_types.end())
            {
                loop_var_comp_type = it->second;
                component_member_types[var_name] = loop_var_comp_type;
            }
        }

        // Parse children until </for>
        while (current().type != TokenType::END_OF_FILE)
        {
            if (current().type == TokenType::LT && peek().type == TokenType::SLASH && peek(2).type == TokenType::FOR)
            {
                break;
            }
            viewForEach->children.push_back(parse_view_node());
        }

        // Remove the temporary loop variable from component_member_types
        if (!loop_var_comp_type.empty())
        {
            component_member_types.erase(var_name);
        }

        // </for>
        expect(TokenType::LT, "Expected '<'");
        expect(TokenType::SLASH, "Expected '/'");
        expect(TokenType::FOR, "Expected 'for'");
        expect(TokenType::GT, "Expected '>'");

        return viewForEach;
    }
}
