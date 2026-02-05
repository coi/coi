#include "parser.h"
#include "defs/def_parser.h"
#include "cli/error.h"
#include <stdexcept>
#include <limits>
#include <cctype>

Parser::Parser(const std::vector<Token> &toks) : tokens(toks) {}

Token Parser::current()
{
    return pos < tokens.size() ? tokens[pos] : tokens.back();
}

Token Parser::peek(int offset)
{
    return (pos + offset) < tokens.size() ? tokens[pos + offset] : tokens.back();
}

void Parser::advance() { pos++; }

bool Parser::match(TokenType type)
{
    if (current().type == type)
    {
        advance();
        return true;
    }
    return false;
}

void Parser::expect(TokenType type, const std::string &msg)
{
    if (!match(type))
    {
        ErrorHandler::compiler_error(msg, current().line);
    }
}

// Check if current token is a type keyword (INT, STRING, FLOAT, etc.) or identifier
bool Parser::is_type_token()
{
    TokenType t = current().type;
    return t == TokenType::INT || t == TokenType::STRING ||
           t == TokenType::FLOAT || t == TokenType::FLOAT32 ||
           t == TokenType::BOOL || t == TokenType::IDENTIFIER ||
           t == TokenType::VOID;
}

// Check if current token can be used as an identifier (including keywords that are allowed as names)
bool Parser::is_identifier_token()
{
    TokenType t = current().type;

    // 1. Standard identifiers are always okay.
    if (t == TokenType::IDENTIFIER)
        return true;

    // 2. Soft Keywords: Common words that are keywords in some places
    // but useful as variable names in others.
    if (t == TokenType::KEY)
        return true;

    // 3. Hard Keywords: Modifiers and Structure.
    // We do NOT return true for PUB, MUT, COMPONENT, POD, or SHARED.
    // This prevents: int pub = 5; or int mut = 10;
    return false;
}

// Parse comma-separated arguments until end_token (RPAREN or RBRACE)
// Supports: positional, named (name = val), reference (&val or &name = val), move (:val or name := val)
std::vector<CallArg> Parser::parse_call_args(TokenType end_token)
{
    std::vector<CallArg> args;

    while (current().type != end_token)
    {
        CallArg arg;

        // Check for reference prefix &
        if (current().type == TokenType::AMPERSAND)
        {
            arg.is_reference = true;
            advance();
        }
        // Check for move prefix :
        else if (current().type == TokenType::COLON)
        {
            arg.is_move = true;
            advance();
        }

        // Check if this is a named argument: name = value or name := value
        bool is_named = false;
        if (is_identifier_token())
        {
            if (peek().type == TokenType::ASSIGN || peek().type == TokenType::MOVE_ASSIGN)
            {
                is_named = true;
            }
        }

        if (is_named)
        {
            arg.name = current().value;
            advance();

            // Check for := (move) or = (copy/reference)
            if (match(TokenType::MOVE_ASSIGN))
            {
                arg.is_move = true;
            }
            else
            {
                expect(TokenType::ASSIGN, "Expected '=' or ':=' after argument name");
            }

            arg.value = parse_expression();
        }
        else
        {
            arg.value = parse_expression();
        }

        args.push_back(std::move(arg));

        if (current().type == TokenType::COMMA)
        {
            advance();
            if (current().type == end_token)
                break; // Allow trailing comma
        }
    }

    return args;
}

void Parser::parse_file()
{
    while (current().type != TokenType::END_OF_FILE)
    {
        if (current().type == TokenType::IMPORT)
        {
            advance();
            expect(TokenType::STRING_LITERAL, "Expected import path");
            imports.push_back(tokens[pos - 1].value);
            expect(TokenType::SEMICOLON, "Expected ';'");
        }
        else if (current().type == TokenType::COMPONENT)
        {
            components.push_back(parse_component());
        }
        else if (current().type == TokenType::ENUM)
        {
            // Global enum (outside any component)
            global_enums.push_back(parse_enum());
        }
        else if (current().type == TokenType::POD)
        {
            // Global pod type (outside any component)
            global_data.push_back(parse_data());
        }
        else if (current().type == TokenType::IDENTIFIER && current().value == "app")
        {
            advance();
            parse_app();
        }
        else
        {
            advance();
        }
    }

    // Default to Main if no app config
    if (app_config.root_component.empty())
    {
        for (const auto &comp : components)
        {
            if (comp.name == "Main")
            {
                app_config.root_component = "Main";
                break;
            }
        }
    }
}
