#pragma once

#include "token.h"
#include <string>
#include <vector>

class Lexer {
    private:
        std::string source;
        size_t pos = 0;
        int line = 1;
        int column = 1;

        char current();
        char peek(int offset = 1);
        void advance();
        void skip_whitespace();
        void skip_comment();
        Token make_token(TokenType type, const std::string& value = "");
        Token read_number();
        Token read_string();
        Token read_template_string();
        Token read_identifier();
    public:
        Lexer(const std::string& src);
        std::vector<Token> tokenize();
};
