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
    // Parse module declaration (must be first non-whitespace statement if present)
    if (current().type == TokenType::MODULE)
    {
        advance();
        if (current().type != TokenType::IDENTIFIER)
        {
            ErrorHandler::compiler_error("Expected module name after 'module'", current().line);
        }
        module_name = current().value;
        advance();
        expect(TokenType::SEMICOLON, "Expected ';' after module declaration");
    }

    while (current().type != TokenType::END_OF_FILE)
    {
        // Check for pub keyword before component/enum/pod
        bool is_public = false;
        if (current().type == TokenType::PUB)
        {
            is_public = true;
            advance();
            
            // pub must be followed by component, enum, or pod
            if (current().type != TokenType::COMPONENT && 
                current().type != TokenType::ENUM && 
                current().type != TokenType::POD)
            {
                ErrorHandler::compiler_error("'pub' can only be used with component, enum, or pod declarations", current().line);
            }
        }

        if (current().type == TokenType::IMPORT)
        {
            advance();
            expect(TokenType::STRING_LITERAL, "Expected import path");
            imports.push_back(tokens[pos - 1].value);
            expect(TokenType::SEMICOLON, "Expected ';'");
        }
        else if (current().type == TokenType::COMPONENT)
        {
            Component comp = parse_component();
            comp.is_public = is_public;
            comp.module_name = module_name;
            components.push_back(std::move(comp));
        }
        else if (current().type == TokenType::ENUM)
        {
            // Global enum (outside any component)
            auto enum_def = parse_enum();
            enum_def->is_public = is_public;
            enum_def->module_name = module_name;
            global_enums.push_back(std::move(enum_def));
        }
        else if (current().type == TokenType::POD)
        {
            // Global pod type (outside any component)
            auto data_def = parse_data();
            data_def->is_public = is_public;
            data_def->module_name = module_name;
            global_data.push_back(std::move(data_def));
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
