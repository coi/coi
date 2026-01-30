#include "lexer.h"
#include <cctype>
#include <unordered_map>

Lexer::Lexer(const std::string& src) : source(src){}

char Lexer::current(){
    return pos < source.size() ? source[pos] : '\0';
}

char Lexer::peek(int offset){
    return (pos + offset) < source.size() ? source[pos + offset] : '\0';
}

void Lexer::advance(){
    if(current() == '\n'){
        line++;
        column = 1;
    }else{
        column++;
    }
    pos++;
}

void Lexer::skip_whitespace(){
    while(std::isspace(current())) advance();
}

void Lexer::skip_comment(){
    if(current() == '/' && peek() == '/'){
        while(current() != '\n' && current() != '\0') advance();
    }
}

Token Lexer::make_token(TokenType type, const std::string& value){
    return Token{type, value, line, column};
}

Token Lexer::read_number(){
    int start_line = line;
    int start_column = column;
    std::string num;
    bool is_float = false;

    // Check for hexadecimal (0x prefix)
    if(current() == '0' && (peek() == 'x' || peek() == 'X')){
        num += current(); advance(); // '0'
        num += current(); advance(); // 'x' or 'X'
        
        // Read hexadecimal digits
        while(std::isxdigit(current())){
            num += current();
            advance();
        }
        
        return Token{TokenType::INT_LITERAL, num, start_line, start_column};
    }

    // Regular decimal number
    while(std::isdigit(current()) || current() == '.'){
        if(current() == '.'){
            if(is_float) break;
            is_float = true;
        }
        num += current();
        advance();
    }

    return Token{is_float ? TokenType::FLOAT_LITERAL : TokenType::INT_LITERAL, num, start_line, start_column};
}

Token Lexer::read_string(){
    int start_line = line;
    int start_column = column;
    std::string str;
    advance(); // skip opening quote

    while(current() != '"' && current() != '\0'){
        if(current() == '\\'){
            advance();
            switch (current()) {
                case 'n' : str += '\n'; break;
                case 't' : str += '\t'; break;
                case '\\' : str += '\\'; break;
                case '"' : str += '"'; break;
                case '{' : str += "\\{"; break;
                case '}' : str += "\\}"; break;
                default: str += current();
            }
        }else{
            str += current();
        }
        advance();
    }

    advance(); // skip closing quote
    return Token{TokenType::STRING_LITERAL, str, start_line, start_column};
}

Token Lexer::read_template_string(){
    int start_line = line;
    int start_column = column;
    std::string str;
    advance(); // skip opening backtick

    while(current() != '`' && current() != '\0'){
        // Template strings support raw content - no escape sequences except for backtick
        if(current() == '\\' && peek() == '`'){
            advance(); // skip backslash
            str += '`'; // add literal backtick
            advance();
        }else{
            str += current();
            advance();
        }
    }

    advance(); // skip closing backtick
    return Token{TokenType::TEMPLATE_STRING, str, start_line, start_column};
}

Token Lexer::read_identifier(){
    int start_line = line;
    int start_column = column;
    std::string id;
    id.reserve(32);  // Pre-allocate for typical identifier lengths

    while(std::isalnum(current()) || current() == '_'){
        id += current();
        advance();
    }

    // Check for keywords
    static const std::unordered_map<std::string, TokenType> keywords = {
        {"component", TokenType::COMPONENT},
        {"def", TokenType::DEF},
        {"return", TokenType::RETURN},
        {"data", TokenType::DATA},
        {"view", TokenType::VIEW},
        {"tick", TokenType::TICK},
        {"init", TokenType::INIT},
        {"mount", TokenType::MOUNT},
        {"pub", TokenType::PUB},
        {"key", TokenType::KEY},
        {"style", TokenType::STYLE},
        {"mut", TokenType::MUT},
        {"import", TokenType::IMPORT},
        {"shared", TokenType::SHARED},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"for", TokenType::FOR},
        {"in", TokenType::IN},
        {"int", TokenType::INT},
        {"float", TokenType::FLOAT},
        {"float32", TokenType::FLOAT32},
        {"string", TokenType::STRING},
        {"bool", TokenType::BOOL},
        {"void", TokenType::VOID},
        {"true", TokenType::TRUE},
        {"false", TokenType::FALSE},
        {"enum", TokenType::ENUM},
        {"router", TokenType::ROUTER},
    };

    auto it = keywords.find(id);
    if(it != keywords.end()){
        return Token{it->second, id, start_line, start_column};
    }

    return Token{TokenType::IDENTIFIER, id, start_line, start_column};
}

std::vector<Token> Lexer::tokenize(){
    std::vector<Token> tokens;
    // Pre-allocate based on source size estimate (roughly 1 token per 5 chars)
    tokens.reserve(source.size() / 5);

    while(current() != '\0'){
        // Skip whitespace and comments
        while(std::isspace(current()) || (current() == '/' && peek() == '/')){
            if(std::isspace(current())){
                skip_whitespace();
            } else {
                skip_comment();
            }
        }

        if(current() == '\0') break;

        // Numbers
        if(std::isdigit(current())){
            tokens.push_back(read_number());
            continue;
        }

        // Strings
        if(current() == '"'){
            tokens.push_back(read_string());
            continue;
        }

        // Template strings (backticks)
        if(current() == '`'){
            tokens.push_back(read_template_string());
            continue;
        }

        // Identifiers and keywords
        if(std::isalpha(current()) || current() == '_'){
            tokens.push_back(read_identifier());
            continue;
        }

        // Two-character operators
        if(current() == '=' && peek() == '='){
            tokens.push_back(make_token(TokenType::EQ, "=="));
            advance(); advance();
            continue;
        }
        if (current() == '!' && peek() == '=') {
            tokens.push_back(make_token(TokenType::NEQ, "!="));
            advance(); advance();
            continue;
        }
        if (current() == '<' && peek() == '=') {
            tokens.push_back(make_token(TokenType::LTE, "<="));
            advance(); advance();
            continue;
        }
        if (current() == '>' && peek() == '=') {
            tokens.push_back(make_token(TokenType::GTE, ">="));
            advance(); advance();
            continue;
        }
        if (current() == '+' && peek() == '=') {
            tokens.push_back(make_token(TokenType::PLUS_ASSIGN, "+="));
            advance(); advance();
            continue;
        }
        if (current() == '-' && peek() == '=') {
            tokens.push_back(make_token(TokenType::MINUS_ASSIGN, "-="));
            advance(); advance();
            continue;
        }
        if (current() == '+' && peek() == '+') {
            tokens.push_back(make_token(TokenType::PLUS_PLUS, "++"));
            advance(); advance();
            continue;
        }
        if (current() == '-' && peek() == '-') {
            tokens.push_back(make_token(TokenType::MINUS_MINUS, "--"));
            advance(); advance();
            continue;
        }
        if (current() == '*' && peek() == '=') {
            tokens.push_back(make_token(TokenType::STAR_ASSIGN, "*="));
            advance(); advance();
            continue;
        }
        if (current() == '/' && peek() == '=') {
            tokens.push_back(make_token(TokenType::SLASH_ASSIGN, "/="));
            advance(); advance();
            continue;
        }
        if (current() == '%' && peek() == '=') {
            tokens.push_back(make_token(TokenType::PERCENT_ASSIGN, "%="));
            advance(); advance();
            continue;
        }
        if (current() == '&' && peek() == '&') {
            tokens.push_back(make_token(TokenType::AND, "&&"));
            advance(); advance();
            continue;
        }
        if (current() == '|' && peek() == '|') {
            tokens.push_back(make_token(TokenType::OR, "||"));
            advance(); advance();
            continue;
        }
        if (current() == '=' && peek() == '>') {
            tokens.push_back(make_token(TokenType::ARROW, "=>"));
            advance(); advance();
            continue;
        }
        if (current() == ':' && peek() == ':') {
            tokens.push_back(make_token(TokenType::DOUBLE_COLON, "::"));
            advance(); advance();
            continue;
        }
        if (current() == ':' && peek() == '=') {
            tokens.push_back(make_token(TokenType::MOVE_ASSIGN, ":="));
            advance(); advance();
            continue;
        }

        // Single-character tokens
        switch (current()) {
            case '+': tokens.push_back(make_token(TokenType::PLUS, "+")); break;
            case '-': tokens.push_back(make_token(TokenType::MINUS, "-")); break;
            case '*': tokens.push_back(make_token(TokenType::STAR, "*")); break;
            case '/': tokens.push_back(make_token(TokenType::SLASH, "/")); break;
            case '%': tokens.push_back(make_token(TokenType::PERCENT, "%")); break;
            case '=': tokens.push_back(make_token(TokenType::ASSIGN, "=")); break;
            case '<': tokens.push_back(make_token(TokenType::LT, "<")); break;
            case '>': tokens.push_back(make_token(TokenType::GT, ">")); break;
            case '!': tokens.push_back(make_token(TokenType::NOT, "!")); break;
            case '?': tokens.push_back(make_token(TokenType::QUESTION, "?")); break;
            case '(': tokens.push_back(make_token(TokenType::LPAREN, "(")); break;
            case ')': tokens.push_back(make_token(TokenType::RPAREN, ")")); break;
            case '{': tokens.push_back(make_token(TokenType::LBRACE, "{")); break;
            case '}': tokens.push_back(make_token(TokenType::RBRACE, "}")); break;
            case '[': tokens.push_back(make_token(TokenType::LBRACKET, "[")); break;
            case ']': tokens.push_back(make_token(TokenType::RBRACKET, "]")); break;
            case ';': tokens.push_back(make_token(TokenType::SEMICOLON, ";")); break;
            case ',': tokens.push_back(make_token(TokenType::COMMA, ",")); break;
            case '.': tokens.push_back(make_token(TokenType::DOT, ".")); break;
            case ':': tokens.push_back(make_token(TokenType::COLON, ":")); break;
            case '&': tokens.push_back(make_token(TokenType::AMPERSAND, "&")); break;
            case '|': tokens.push_back(make_token(TokenType::UNKNOWN, "|")); break;  // Single | not supported
            default:
                tokens.push_back(make_token(TokenType::UNKNOWN, std::string(1, current())));
        }
        advance();
    }

    tokens.push_back(make_token(TokenType::END_OF_FILE, ""));
    return tokens;
}
