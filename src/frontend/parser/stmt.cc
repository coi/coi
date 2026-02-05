#include "parser.h"
#include "cli/error.h"
#include <stdexcept>

std::unique_ptr<Statement> Parser::parse_statement()
{
    // Block
    if (current().type == TokenType::LBRACE)
    {
        advance();
        auto block = std::make_unique<BlockStatement>();
        while (current().type != TokenType::RBRACE && current().type != TokenType::END_OF_FILE)
        {
            block->statements.push_back(parse_statement());
        }
        expect(TokenType::RBRACE, "Expected '}'");
        return block;
    }

    // If
    if (current().type == TokenType::IF)
    {
        advance();
        expect(TokenType::LPAREN, "Expected '('");
        auto cond = parse_expression();
        expect(TokenType::RPAREN, "Expected ')'");

        auto ifStmt = std::make_unique<IfStatement>();
        ifStmt->condition = std::move(cond);
        ifStmt->then_branch = parse_statement();

        if (match(TokenType::ELSE))
        {
            ifStmt->else_branch = parse_statement();
        }
        return ifStmt;
    }

    // For (three syntaxes: traditional, range-based, and foreach)
    if (current().type == TokenType::FOR)
    {
        advance();

        // Check for range-based or foreach syntax: for i in start:end { } OR for e in array { }
        if (current().type == TokenType::IDENTIFIER && peek().type == TokenType::IN)
        {
            std::string var_name = current().value;
            advance(); // skip identifier
            advance(); // skip 'in'

            auto first_expr = parse_expression();

            // If we see ':', it's a range: for i in start:end
            if (current().type == TokenType::COLON)
            {
                advance(); // skip ':'
                auto rangeFor = std::make_unique<ForRangeStatement>();
                rangeFor->var_name = var_name;
                rangeFor->start = std::move(first_expr);
                // Disable brace init so Name{ isn't parsed as data literal before the block
                bool old_allow_brace = allow_brace_init;
                allow_brace_init = false;
                rangeFor->end = parse_expression();
                allow_brace_init = old_allow_brace;
                rangeFor->body = parse_statement();
                return rangeFor;
            }

            // Otherwise it's foreach: for e in array
            auto forEach = std::make_unique<ForEachStatement>();
            forEach->var_name = var_name;
            forEach->iterable = std::move(first_expr);
            forEach->body = parse_statement();
            return forEach;
        }

        ErrorHandler::compiler_error("Unexpected token after 'for'. Expected range 'i in start:end' or foreach 'i in array'. C-style for loops are not supported.", -1);
    }

    // Return
    if (current().type == TokenType::RETURN)
    {
        advance();
        auto ret = std::make_unique<ReturnStatement>();
        if (current().type != TokenType::SEMICOLON)
        {
            ret->value = parse_expression();
        }
        expect(TokenType::SEMICOLON, "Expected ';'");
        return ret;
    }

    // Variable declaration
    bool is_mutable = false;
    if (current().type == TokenType::MUT)
    {
        is_mutable = true;
        advance();
    }

    bool is_type = false;
    if (current().type == TokenType::INT || current().type == TokenType::STRING ||
        current().type == TokenType::FLOAT || current().type == TokenType::FLOAT32 ||
        current().type == TokenType::BOOL)
    {
        is_type = true;
    }
    else if (current().type == TokenType::IDENTIFIER)
    {
        // Distinguish between Variable Declaration and other statements starting with Identifier
        // Declaration: Type Name ... | Type[] Name ... | Type[N] Name ... | Type& Name ...
        // Assignment:  Name = ... | Name[index] = ... | Name[index].member = ...
        // Call:        Name(...)

        Token next = peek(1);
        if (next.type == TokenType::IDENTIFIER)
        {
            is_type = true; // "Type Name"
        }
        else if (next.type == TokenType::AMPERSAND)
        {
            is_type = true; // "Type& Name"
        }
        else if (next.type == TokenType::LBRACKET)
        {
            // Check for "Type[] Name" (dynamic) or "Type[N] Name" (fixed-size)
            if (peek(2).type == TokenType::RBRACKET && peek(3).type == TokenType::IDENTIFIER)
            {
                is_type = true; // Type[] Name
            }
            else if (peek(2).type == TokenType::INT_LITERAL && peek(3).type == TokenType::RBRACKET && peek(4).type == TokenType::IDENTIFIER)
            {
                is_type = true; // Type[N] Name
            }
        }
    }

    if (is_type)
    {

        std::string type = current().value;
        advance();

        // Handle reference type
        bool is_reference = false;
        if (current().type == TokenType::AMPERSAND)
        {
            is_reference = true;
            advance();
        }

        // Handle array type
        if (current().type == TokenType::LBRACKET)
        {
            advance();
            if (current().type == TokenType::INT_LITERAL)
            {
                // Fixed-size array: Type[N]
                std::string size = current().value;
                advance();
                expect(TokenType::RBRACKET, "Expected ']'");
                type += "[" + size + "]";
            }
            else
            {
                // Dynamic array: Type[]
                expect(TokenType::RBRACKET, "Expected ']'");
                type += "[]";
            }
        }

        std::string name = current().value;
        if (is_identifier_token())
        {
            advance();
        }
        else
        {
            expect(TokenType::IDENTIFIER, "Expected variable name");
        }

        auto var_decl = std::make_unique<VarDeclaration>();
        var_decl->type = type;
        var_decl->name = name;
        var_decl->is_mutable = is_mutable;
        var_decl->is_reference = is_reference;

        // Check for := (move) or = (copy)
        if (match(TokenType::MOVE_ASSIGN))
        {
            var_decl->is_move = true;
            var_decl->initializer = parse_expression();
        }
        else if (match(TokenType::ASSIGN))
        {
            var_decl->initializer = parse_expression();
        }

        expect(TokenType::SEMICOLON, "Expected ';'");
        return var_decl;
    }
    else if (is_mutable)
    {
        ErrorHandler::compiler_error("Expected type after 'mut'", -1);
    }

    // Assignment to array element: arr[i] = value or arr[i] += value etc.
    // Also handles arr[i].member = value (member assignment on array element)
    if (current().type == TokenType::IDENTIFIER && peek().type == TokenType::LBRACKET)
    {
        // Could be an index assignment or an expression statement with index access
        // Need to look ahead to see if there's an assignment operator after the bracket
        std::string name = current().value;
        size_t saved_pos = pos;
        advance(); // skip identifier
        advance(); // skip '['

        // Skip to find matching ']'
        int bracket_depth = 1;
        while (bracket_depth > 0 && current().type != TokenType::END_OF_FILE)
        {
            if (current().type == TokenType::LBRACKET)
                bracket_depth++;
            else if (current().type == TokenType::RBRACKET)
                bracket_depth--;
            advance();
        }

        // Check if followed by assignment operator (including compound assignments)
        TokenType after_bracket = current().type;
        bool is_index_assign = (after_bracket == TokenType::ASSIGN ||
                                after_bracket == TokenType::PLUS_ASSIGN ||
                                after_bracket == TokenType::MINUS_ASSIGN ||
                                after_bracket == TokenType::STAR_ASSIGN ||
                                after_bracket == TokenType::SLASH_ASSIGN ||
                                after_bracket == TokenType::PERCENT_ASSIGN);

        // Check if followed by .member = value (member assignment on array element)
        bool is_index_member_assign = false;
        if (after_bracket == TokenType::DOT)
        {
            // Skip through member chain to see if there's an assignment at the end
            while (current().type == TokenType::DOT)
            {
                advance(); // skip '.'
                if (current().type != TokenType::IDENTIFIER)
                    break;
                advance(); // skip member name
            }
            TokenType assign_op = current().type;
            is_index_member_assign = (assign_op == TokenType::ASSIGN ||
                                      assign_op == TokenType::PLUS_ASSIGN ||
                                      assign_op == TokenType::MINUS_ASSIGN ||
                                      assign_op == TokenType::STAR_ASSIGN ||
                                      assign_op == TokenType::SLASH_ASSIGN ||
                                      assign_op == TokenType::PERCENT_ASSIGN);
        }

        // Restore position
        pos = saved_pos;

        if (is_index_assign)
        {
            advance(); // skip identifier
            expect(TokenType::LBRACKET, "Expected '['");
            auto index_expr = parse_expression();
            expect(TokenType::RBRACKET, "Expected ']'");

            TokenType opType = current().type;
            advance(); // skip assignment operator

            auto idx_assign = std::make_unique<IndexAssignment>();
            idx_assign->array = std::make_unique<Identifier>(name);
            idx_assign->index = std::move(index_expr);
            idx_assign->value = parse_expression();

            // Set compound operator if not plain assignment
            if (opType == TokenType::PLUS_ASSIGN)
                idx_assign->compound_op = "+";
            else if (opType == TokenType::MINUS_ASSIGN)
                idx_assign->compound_op = "-";
            else if (opType == TokenType::STAR_ASSIGN)
                idx_assign->compound_op = "*";
            else if (opType == TokenType::SLASH_ASSIGN)
                idx_assign->compound_op = "/";
            else if (opType == TokenType::PERCENT_ASSIGN)
                idx_assign->compound_op = "%";

            expect(TokenType::SEMICOLON, "Expected ';'");
            return idx_assign;
        }

        if (is_index_member_assign)
        {
            // Parse arr[i].member = value as MemberAssignment with IndexAccess as object
            advance(); // skip identifier
            expect(TokenType::LBRACKET, "Expected '['");
            auto index_expr = parse_expression();
            expect(TokenType::RBRACKET, "Expected ']'");

            // Build object as IndexAccess
            std::unique_ptr<Expression> obj_expr = std::make_unique<IndexAccess>(
                std::make_unique<Identifier>(name), std::move(index_expr));

            // Now parse the member chain
            expect(TokenType::DOT, "Expected '.'");
            std::string last_member = current().value;
            expect(TokenType::IDENTIFIER, "Expected member name");

            // Handle chained member access (arr[i].a.b = value)
            while (current().type == TokenType::DOT)
            {
                advance(); // skip '.'
                obj_expr = std::make_unique<MemberAccess>(std::move(obj_expr), last_member);
                last_member = current().value;
                expect(TokenType::IDENTIFIER, "Expected member name");
            }

            TokenType opType = current().type;
            advance(); // skip assignment operator

            auto member_assign = std::make_unique<MemberAssignment>();
            member_assign->object = std::move(obj_expr);
            member_assign->member = last_member;
            member_assign->value = parse_expression();

            // Set compound operator if not plain assignment
            if (opType == TokenType::PLUS_ASSIGN)
                member_assign->compound_op = "+";
            else if (opType == TokenType::MINUS_ASSIGN)
                member_assign->compound_op = "-";
            else if (opType == TokenType::STAR_ASSIGN)
                member_assign->compound_op = "*";
            else if (opType == TokenType::SLASH_ASSIGN)
                member_assign->compound_op = "/";
            else if (opType == TokenType::PERCENT_ASSIGN)
                member_assign->compound_op = "%";

            expect(TokenType::SEMICOLON, "Expected ';'");
            return member_assign;
        }
    }

    // Member assignment: obj.member = value or obj.a.b = value
    if (current().type == TokenType::IDENTIFIER && peek().type == TokenType::DOT)
    {
        // Look ahead to find if this is a member assignment
        size_t saved_pos = pos;
        advance(); // skip identifier

        // Track the chain of member accesses
        while (current().type == TokenType::DOT)
        {
            advance(); // skip '.'
            if (current().type != TokenType::IDENTIFIER)
                break;
            advance(); // skip member name
        }

        // Check if followed by assignment operator
        TokenType assign_op = current().type;
        bool is_member_assign = (assign_op == TokenType::ASSIGN ||
                                 assign_op == TokenType::PLUS_ASSIGN ||
                                 assign_op == TokenType::MINUS_ASSIGN ||
                                 assign_op == TokenType::STAR_ASSIGN ||
                                 assign_op == TokenType::SLASH_ASSIGN ||
                                 assign_op == TokenType::PERCENT_ASSIGN);

        // Restore position
        pos = saved_pos;

        if (is_member_assign)
        {
            // Parse the object part (all but the last member)
            std::unique_ptr<Expression> obj_expr = std::make_unique<Identifier>(current().value);
            advance(); // skip first identifier
            advance(); // skip first '.'

            std::string last_member = current().value;
            expect(TokenType::IDENTIFIER, "Expected member name");

            // Handle chained member access (a.b.c = value means object is a.b, member is c)
            while (current().type == TokenType::DOT)
            {
                advance(); // skip '.'
                // Previous member becomes part of the object
                obj_expr = std::make_unique<MemberAccess>(std::move(obj_expr), last_member);
                last_member = current().value;
                expect(TokenType::IDENTIFIER, "Expected member name");
            }

            TokenType opType = current().type;
            advance(); // skip assignment operator

            auto member_assign = std::make_unique<MemberAssignment>();
            member_assign->object = std::move(obj_expr);
            member_assign->member = last_member;
            member_assign->value = parse_expression();

            // Set compound operator if not plain assignment
            if (opType == TokenType::PLUS_ASSIGN)
                member_assign->compound_op = "+";
            else if (opType == TokenType::MINUS_ASSIGN)
                member_assign->compound_op = "-";
            else if (opType == TokenType::STAR_ASSIGN)
                member_assign->compound_op = "*";
            else if (opType == TokenType::SLASH_ASSIGN)
                member_assign->compound_op = "/";
            else if (opType == TokenType::PERCENT_ASSIGN)
                member_assign->compound_op = "%";

            expect(TokenType::SEMICOLON, "Expected ';'");
            return member_assign;
        }
    }

    // Assignment
    if (is_identifier_token() &&
        (peek().type == TokenType::ASSIGN ||
         peek().type == TokenType::MOVE_ASSIGN ||
         peek().type == TokenType::PLUS_ASSIGN ||
         peek().type == TokenType::MINUS_ASSIGN ||
         peek().type == TokenType::STAR_ASSIGN ||
         peek().type == TokenType::SLASH_ASSIGN ||
         peek().type == TokenType::PERCENT_ASSIGN))
    {

        std::string name = current().value;
        advance();

        TokenType opType = current().type;
        advance(); // skip op

        auto assign = std::make_unique<Assignment>();
        assign->name = name;

        // Check for move assignment
        if (opType == TokenType::MOVE_ASSIGN)
        {
            assign->is_move = true;
        }

        auto val = parse_expression();

        if (opType == TokenType::PLUS_ASSIGN)
        {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "+", std::move(val));
            assign->value = std::move(binOp);
        }
        else if (opType == TokenType::MINUS_ASSIGN)
        {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "-", std::move(val));
            assign->value = std::move(binOp);
        }
        else if (opType == TokenType::STAR_ASSIGN)
        {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "*", std::move(val));
            assign->value = std::move(binOp);
        }
        else if (opType == TokenType::SLASH_ASSIGN)
        {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "/", std::move(val));
            assign->value = std::move(binOp);
        }
        else if (opType == TokenType::PERCENT_ASSIGN)
        {
            auto left = std::make_unique<Identifier>(name);
            auto binOp = std::make_unique<BinaryOp>(std::move(left), "%", std::move(val));
            assign->value = std::move(binOp);
        }
        else
        {
            assign->value = std::move(val);
        }

        expect(TokenType::SEMICOLON, "Expected ';'");
        return assign;
    }

    // Expression statement
    // try {
    auto expr = parse_expression();
    expect(TokenType::SEMICOLON, "Expected ';'");
    auto stmt = std::make_unique<ExpressionStatement>();
    stmt->expression = std::move(expr);
    return stmt;
    // } catch (...) {
    // Fall through to error
    // }

    // throw std::runtime_error("Unexpected statement");
}
