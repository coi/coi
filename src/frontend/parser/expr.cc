#include "parser.h"
#include "cli/error.h"
#include <limits>
#include <stdexcept>
#include <cctype>

std::unique_ptr<Expression> Parser::parse_expression()
{
    return parse_ternary();
}

std::unique_ptr<Expression> Parser::parse_ternary()
{
    auto expr = parse_or();

    if (current().type == TokenType::QUESTION)
    {
        advance();                           // skip '?'
        auto true_expr = parse_expression(); // Allow nested ternary
        expect(TokenType::COLON, "Expected ':' in ternary expression");
        auto false_expr = parse_ternary(); // Right-associative
        expr = std::make_unique<TernaryOp>(std::move(expr), std::move(true_expr), std::move(false_expr));
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parse_expression_no_gt()
{
    // Parse expression but don't treat > as comparison operator
    // Used for expressions inside view tags like <if condition>
    bool old_allow_gt = allow_gt_comparison;
    allow_gt_comparison = false;
    auto expr = parse_or();
    allow_gt_comparison = old_allow_gt;
    return expr;
}

std::unique_ptr<Expression> Parser::parse_or()
{
    auto left = parse_and();

    while (current().type == TokenType::OR)
    {
        std::string op = current().value;
        advance();
        auto right = parse_and();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_and()
{
    auto left = parse_bitwise_or();

    while (current().type == TokenType::AND)
    {
        std::string op = current().value;
        advance();
        auto right = parse_bitwise_or();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_bitwise_or()
{
    auto left = parse_bitwise_xor();

    while (current().type == TokenType::PIPE)
    {
        std::string op = current().value;
        advance();
        auto right = parse_bitwise_xor();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_bitwise_xor()
{
    auto left = parse_bitwise_and();

    while (current().type == TokenType::CARET)
    {
        std::string op = current().value;
        advance();
        auto right = parse_bitwise_and();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_bitwise_and()
{
    auto left = parse_equality();

    while (current().type == TokenType::AMPERSAND)
    {
        std::string op = current().value;
        advance();
        auto right = parse_equality();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_equality()
{
    auto left = parse_comparison();

    while (current().type == TokenType::EQ || current().type == TokenType::NEQ)
    {
        std::string op = current().value;
        advance();
        auto right = parse_comparison();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_comparison()
{
    auto left = parse_shift();

    while (current().type == TokenType::LT ||
           (current().type == TokenType::GT && allow_gt_comparison) ||
           current().type == TokenType::LTE || current().type == TokenType::GTE)
    {
        std::string op = current().value;
        advance();
        auto right = parse_shift();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_shift()
{
    auto left = parse_additive();

    while (current().type == TokenType::LSHIFT || current().type == TokenType::RSHIFT)
    {
        std::string op = current().value;
        advance();
        auto right = parse_additive();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_additive()
{
    auto left = parse_multiplicative();

    while (current().type == TokenType::PLUS || current().type == TokenType::MINUS)
    {
        std::string op = current().value;
        advance();
        auto right = parse_multiplicative();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_postfix()
{
    auto expr = parse_primary();

    while (true)
    {
        if (current().type == TokenType::PLUS_PLUS)
        {
            advance();
            expr = std::make_unique<PostfixOp>(std::move(expr), "++");
        }
        else if (current().type == TokenType::MINUS_MINUS)
        {
            advance();
            expr = std::make_unique<PostfixOp>(std::move(expr), "--");
        }
        else
        {
            break;
        }
    }
    return expr;
}

std::unique_ptr<Expression> Parser::parse_unary()
{
    // Unary operators: -, +, !, ~
    if (current().type == TokenType::MINUS || current().type == TokenType::PLUS || 
        current().type == TokenType::NOT || current().type == TokenType::TILDE)
    {
        std::string op = current().value;
        advance();
        auto operand = parse_unary();
        return std::make_unique<UnaryOp>(op, std::move(operand));
    }
    // Reference expression: &expr (borrow, pass by reference)
    if (current().type == TokenType::AMPERSAND)
    {
        advance();
        auto operand = parse_unary();
        return std::make_unique<ReferenceExpression>(std::move(operand));
    }
    // Move expression: :expr (transfer ownership)
    if (current().type == TokenType::COLON)
    {
        advance();
        auto operand = parse_unary();
        return std::make_unique<MoveExpression>(std::move(operand));
    }
    return parse_postfix();
}

std::unique_ptr<Expression> Parser::parse_multiplicative()
{
    auto left = parse_unary();

    while (current().type == TokenType::STAR || current().type == TokenType::SLASH || current().type == TokenType::PERCENT)
    {
        std::string op = current().value;
        advance();
        auto right = parse_unary();
        left = std::make_unique<BinaryOp>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_primary()
{
    // Match expression: match (subject) { pattern => result, ... }
    if (current().type == TokenType::MATCH)
    {
        return parse_match();
    }

    // Integer literal
    if (current().type == TokenType::INT_LITERAL)
    {
        int value;
        try
        {
            // Use base 0 to auto-detect decimal (10) or hexadecimal (0x)
            long long ll_value = std::stoll(current().value, nullptr, 0);
            if (ll_value > std::numeric_limits<int>::max() || ll_value < std::numeric_limits<int>::min())
            {
                throw std::out_of_range("overflow");
            }
            value = static_cast<int>(ll_value);
        }
        catch (const std::out_of_range &)
        {
            ErrorHandler::compiler_error("Integer literal '" + current().value + "' is too large", current().line);
        }
        catch (const std::invalid_argument &)
        {
            ErrorHandler::compiler_error("Invalid integer literal '" + current().value + "'", current().line);
        }
        advance();
        return std::make_unique<IntLiteral>(value);
    }

    // Float literal
    if (current().type == TokenType::FLOAT_LITERAL)
    {
        double value;
        try
        {
            value = std::stod(current().value);
        }
        catch (const std::out_of_range &)
        {
            ErrorHandler::compiler_error("Float literal '" + current().value + "' is too large", current().line);
        }
        catch (const std::invalid_argument &)
        {
            ErrorHandler::compiler_error("Invalid float literal '" + current().value + "'", current().line);
        }
        advance();
        return std::make_unique<FloatLiteral>(value);
    }

    // String literal
    if (current().type == TokenType::STRING_LITERAL)
    {
        std::string value = current().value;
        advance();
        return std::make_unique<StringLiteral>(value, false);
    }

    // Template string (backticks)
    if (current().type == TokenType::TEMPLATE_STRING)
    {
        std::string value = current().value;
        advance();
        return std::make_unique<StringLiteral>(value, true);
    }

    // Boolean literal
    if (current().type == TokenType::TRUE)
    {
        advance();
        return std::make_unique<BoolLiteral>(true);
    }
    if (current().type == TokenType::FALSE)
    {
        advance();
        return std::make_unique<BoolLiteral>(false);
    }

    // Anonymous struct literal: { field = value, ... }
    // Type is inferred from context (e.g., array element type)
    if (current().type == TokenType::LBRACE && allow_brace_init)
    {
        advance();
        auto parsed_args = parse_call_args(TokenType::RBRACE);
        expect(TokenType::RBRACE, "Expected '}'");

        // Create ComponentConstruction with empty name - type will be inferred
        auto data_expr = std::make_unique<ComponentConstruction>("");
        data_expr->args = std::move(parsed_args);
        return data_expr;
    }

    // Identifer or function call (also allow 'key' and 'data' keywords as identifier)
    if (is_identifier_token())
    {
        std::string name = current().value;
        int identifier_line = current().line;
        advance();

        // Check for namespaced access: Module::Value or Module::Type(...)
        if (current().type == TokenType::DOUBLE_COLON)
        {
            advance();
            std::string value_name = current().value;
            expect(TokenType::IDENTIFIER, "Expected name after '::'");
            
            // Check if this is a namespaced constructor call: Module::Type(...)
            if (current().type == TokenType::LPAREN && std::isupper(value_name[0]))
            {
                advance(); // consume '('
                auto parsed_args = parse_call_args(TokenType::RPAREN);
                expect(TokenType::RPAREN, "Expected ')'");
                
                // Create a component construction with qualified name
                auto comp_expr = std::make_unique<ComponentConstruction>(name + "::" + value_name);
                comp_expr->args = std::move(parsed_args);
                return comp_expr;
            }
            
            // Otherwise treat as enum access
            return std::make_unique<EnumAccess>(name, value_name);
        }

        std::unique_ptr<Expression> expr = std::make_unique<Identifier>(name);

        while (true)
        {
            // Data literal initialization: TypeName{val1, val2, ...} or TypeName{name = val, ...}
            if (current().type == TokenType::LBRACE && std::isupper(name[0]) && allow_brace_init)
            {
                advance();
                auto parsed_args = parse_call_args(TokenType::RBRACE);
                expect(TokenType::RBRACE, "Expected '}'");

                // Use ComponentConstruction for data types too (same aggregate init semantics)
                auto data_expr = std::make_unique<ComponentConstruction>(name);
                data_expr->args = std::move(parsed_args);
                return data_expr;
            }
            if (current().type == TokenType::LPAREN)
            {
                advance();

                // Check if this is a component construction (uppercase) vs function call
                bool is_component = false;
                if (dynamic_cast<Identifier *>(expr.get()))
                {
                    // Simple identifier - component if uppercase
                    is_component = std::isupper(name[0]);
                }

                auto parsed_args = parse_call_args(TokenType::RPAREN);
                expect(TokenType::RPAREN, "Expected ')'");

                if (is_component)
                {
                    // Component construction
                    auto comp_expr = std::make_unique<ComponentConstruction>(expr->to_webcc());
                    comp_expr->args = std::move(parsed_args);
                    expr = std::move(comp_expr);
                }
                else
                {
                    // Function call
                    auto call = std::make_unique<FunctionCall>(expr->to_webcc());
                    call->line = identifier_line;
                    call->args = std::move(parsed_args);
                    expr = std::move(call);
                }
            }
            else if (current().type == TokenType::DOT)
            {
                advance();
                std::string member = current().value;
                expect(TokenType::IDENTIFIER, "Expected member name");

                // Check for Component.EnumName::Value syntax for shared enums
                if (current().type == TokenType::DOUBLE_COLON)
                {
                    advance();
                    std::string value_name = current().value;
                    expect(TokenType::IDENTIFIER, "Expected enum value name after '::'");
                    // name is the component name, member is the enum name
                    return std::make_unique<EnumAccess>(member, value_name, name);
                }

                expr = std::make_unique<MemberAccess>(std::move(expr), member);
            }
            else if (current().type == TokenType::LBRACKET)
            {
                // Check for type literal syntax: TypeName[] (empty brackets = array type)
                if (peek().type == TokenType::RBRACKET)
                {
                    // This is a type literal like "User[]" - used in Json.parse(User[], ...)
                    advance(); // consume [
                    advance(); // consume ]
                    return std::make_unique<TypeLiteral>(name + "[]");
                }
                // Array index access
                advance();
                auto index = parse_expression();
                expect(TokenType::RBRACKET, "Expected ']'");
                expr = std::make_unique<IndexAccess>(std::move(expr), std::move(index));
            }
            else
            {
                break;
            }
        }
        return expr;
    }

    // Array literal: [expr, expr, ...] or repeat initializer: [value; count]
    if (current().type == TokenType::LBRACKET)
    {
        advance();

        // Empty array
        if (current().type == TokenType::RBRACKET)
        {
            advance();
            return std::make_unique<ArrayLiteral>();
        }

        // Allow { field = value } syntax for anonymous struct literals in arrays
        allow_brace_init = true;

        // Parse first expression
        auto first_expr = parse_expression();

        // Check for repeat initializer syntax: [value; count]
        if (current().type == TokenType::SEMICOLON)
        {
            advance();
            auto repeat = std::make_unique<ArrayRepeatLiteral>();
            repeat->value = std::move(first_expr);

            // Parse count expression (type checker validates it's a compile-time constant integer)
            repeat->count = parse_expression();

            expect(TokenType::RBRACKET, "Expected ']'");
            return repeat;
        }

        // Regular array literal
        auto arr = std::make_unique<ArrayLiteral>();
        arr->elements.push_back(std::move(first_expr));

        while (current().type == TokenType::COMMA)
        {
            advance();
            if (current().type == TokenType::RBRACKET)
                break; // Allow trailing comma
            arr->elements.push_back(parse_expression());
        }
        expect(TokenType::RBRACKET, "Expected ']'");
        return arr;
    }

    // Parenthesized expression
    if (match(TokenType::LPAREN))
    {
        // Re-enable > comparison inside parentheses since it's unambiguous
        bool old_allow_gt = allow_gt_comparison;
        allow_gt_comparison = true;
        auto expr = parse_expression();
        allow_gt_comparison = old_allow_gt;
        expect(TokenType::RPAREN, "Expected ')'");
        return expr;
    }

    ErrorHandler::compiler_error("Unexpected token in expression: " + current().value + " (Type: " + std::to_string((int)current().type) + ")", current().line);
}

// Parse match expression: match (subject) { pattern => expr, pattern => { ... yield expr; ... }, ... }
// Patterns:
//   - Enum: EnumType::Value
//   - Pod with value: PodType{field = value, ...}
//   - Pod with binding: PodType{field, ...} (captures field into local)
//   - Variant: Success(Type value, Meta meta) / Error(string message)
//   - else: default case
std::unique_ptr<Expression> Parser::parse_match()
{
    int match_line = current().line;
    advance(); // skip 'match'
    
    expect(TokenType::LPAREN, "Expected '(' after 'match'");
    auto subject = parse_expression();
    expect(TokenType::RPAREN, "Expected ')' after match subject");
    
    expect(TokenType::LBRACE, "Expected '{' to start match body");
    
    auto match_expr = std::make_unique<MatchExpr>();
    match_expr->subject = std::move(subject);
    match_expr->line = match_line;
    
    while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
    {
        MatchArm arm;
        arm.line = current().line;
        
        // Check for 'else' pattern
        if (current().type == TokenType::ELSE)
        {
            advance(); // skip 'else'
            arm.pattern.kind = MatchPattern::Kind::Else;
        }
        // Check for literal patterns: numbers, strings, booleans
        else if (current().type == TokenType::INT_LITERAL ||
                 current().type == TokenType::FLOAT_LITERAL ||
                 current().type == TokenType::STRING_LITERAL ||
                 current().type == TokenType::TEMPLATE_STRING ||
                 current().type == TokenType::TRUE ||
                 current().type == TokenType::FALSE)
        {
            arm.pattern.kind = MatchPattern::Kind::Literal;
            // Parse the literal as an expression
            arm.pattern.literal_value = parse_primary();
        }
        // Check for enum pattern: TypeName::Value
        else if (current().type == TokenType::IDENTIFIER && peek().type == TokenType::DOUBLE_COLON)
        {
            arm.pattern.kind = MatchPattern::Kind::Enum;
            arm.pattern.type_name = current().value;
            advance(); // skip enum name
            advance(); // skip '::'
            arm.pattern.enum_value = current().value;
            expect(TokenType::IDENTIFIER, "Expected enum value after '::'");
        }
        // Check for variant pattern: Variant(Type name, ...)
        else if (current().type == TokenType::IDENTIFIER && peek().type == TokenType::LPAREN)
        {
            arm.pattern.kind = MatchPattern::Kind::Variant;
            arm.pattern.type_name = current().value;  // constructor/variant name (e.g., Success/Error)
            advance(); // skip variant name
            advance(); // skip '('

            while (current().type != TokenType::RPAREN && current().type != TokenType::END_OF_FILE)
            {
                MatchPattern::VariantBinding binding;
                binding.type = current().value;

                if (current().type == TokenType::INT || current().type == TokenType::STRING ||
                    current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
                    current().type == TokenType::BOOL || current().type == TokenType::IDENTIFIER)
                {
                    advance();
                }
                else
                {
                    ErrorHandler::compiler_error("Expected type in variant pattern", current().line);
                }

                if (current().type == TokenType::DOUBLE_COLON)
                {
                    advance();
                    binding.type += "::" + current().value;
                    expect(TokenType::IDENTIFIER, "Expected type name after '::' in variant pattern");
                }

                if (current().type == TokenType::LBRACKET)
                {
                    advance();
                    expect(TokenType::RBRACKET, "Expected ']' for array type in variant pattern");
                    binding.type += "[]";
                }

                binding.name = current().value;
                expect(TokenType::IDENTIFIER, "Expected variable name in variant pattern");

                arm.pattern.variant_bindings.push_back(std::move(binding));

                if (current().type == TokenType::COMMA)
                {
                    advance();
                    if (current().type == TokenType::RPAREN)
                        break;
                }
                else
                {
                    break;
                }
            }

            expect(TokenType::RPAREN, "Expected ')' after variant pattern");
        }
        // Check for pod pattern: TypeName{field = value, ...} or TypeName{field, ...}
        else if (current().type == TokenType::IDENTIFIER && peek().type == TokenType::LBRACE)
        {
            arm.pattern.kind = MatchPattern::Kind::Pod;
            arm.pattern.type_name = current().value;
            advance(); // skip type name
            advance(); // skip '{'
            
            while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
            {
                MatchPattern::FieldPattern field;
                field.name = current().value;
                expect(TokenType::IDENTIFIER, "Expected field name in pod pattern");
                
                // Check if this is a value match (field = value) or just a binding (field)
                if (current().type == TokenType::ASSIGN)
                {
                    advance(); // skip '='
                    field.value = parse_expression();
                }
                // Otherwise it's a binding pattern - field.value remains nullptr
                
                arm.pattern.fields.push_back(std::move(field));
                
                if (current().type == TokenType::COMMA)
                {
                    advance();
                    if (current().type == TokenType::RBRACE)
                        break; // trailing comma
                }
                else
                {
                    break;
                }
            }
            expect(TokenType::RBRACE, "Expected '}' after pod pattern fields");
        }
        else
        {
            ErrorHandler::compiler_error("Expected pattern (literal, EnumType::Value, Variant(...), PodType{...}, or else) in match arm", current().line);
        }
        
        // Expect '=>'
        expect(TokenType::ARROW, "Expected '=>' after match pattern");
        
        // Parse the arm body.
        // - Braced block requires explicit `yield` statements.
        // - Non-braced form is shorthand expression arm.
        if (current().type == TokenType::LBRACE)
        {
            advance(); // skip '{'
            auto block = std::make_unique<BlockExpr>();
            bool has_yield = false;

            while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
            {
                // Match-arm explicit yield: yield <expr>;
                // Parsed locally so we don't introduce a global keyword change.
                if (current().type == TokenType::IDENTIFIER && current().value == "yield")
                {
                    int yield_line = current().line;
                    advance(); // skip 'yield'

                    auto ret = std::make_unique<ReturnStatement>();
                    if (current().type != TokenType::SEMICOLON)
                    {
                        ret->value = parse_expression();
                    }
                    expect(TokenType::SEMICOLON, "Expected ';' after yield expression");
                    ret->line = yield_line;
                    block->statements.push_back(std::move(ret));
                    has_yield = true;
                    continue;
                }

                block->statements.push_back(parse_statement());
            }

            expect(TokenType::RBRACE, "Expected '}' after match arm body");

            if (!has_yield)
            {
                ErrorHandler::compiler_error(
                    "Match arm block requires explicit 'yield <expr>;' (use non-braced shorthand for simple expression arms)",
                    arm.line);
            }

            arm.body = std::move(block);
        }
        else
        {
            // Allow simple expression without braces
            arm.body = parse_expression();
        }
        
        match_expr->arms.push_back(std::move(arm));
        
        // Optional semicolon or comma between arms
        if (current().type == TokenType::SEMICOLON || current().type == TokenType::COMMA)
        {
            advance();
        }
    }
    
    expect(TokenType::RBRACE, "Expected '}' to close match expression");
    
    return match_expr;
}

