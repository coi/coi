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
    auto left = parse_equality();

    while (current().type == TokenType::AND)
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
    auto left = parse_additive();

    while (current().type == TokenType::LT ||
           (current().type == TokenType::GT && allow_gt_comparison) ||
           current().type == TokenType::LTE || current().type == TokenType::GTE)
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
    // Unary operators: -, +, !
    if (current().type == TokenType::MINUS || current().type == TokenType::PLUS || current().type == TokenType::NOT)
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

    // Identifer or function call (also allow 'key' and 'data' keywords as identifier)
    if (is_identifier_token())
    {
        std::string name = current().value;
        int identifier_line = current().line;
        advance();

        // Check for enum access: EnumName::Value
        if (current().type == TokenType::DOUBLE_COLON)
        {
            advance();
            std::string value_name = current().value;
            expect(TokenType::IDENTIFIER, "Expected enum value name after '::'");
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
