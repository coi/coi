// Definition file parser implementation

#include "def_parser.h"
#include "../cli/error.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>

namespace fs = std::filesystem;

// ============================================================
// DefParser - Lexer
// ============================================================

void DefParser::skip_whitespace_and_comments()
{
    while (pos_ < source_.size())
    {
        char c = source_[pos_];
        if (c == ' ' || c == '\t' || c == '\r')
        {
            pos_++;
        }
        else if (c == '\n')
        {
            pos_++;
            line_++;
        }
        else if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/')
        {
            // Line comment
            while (pos_ < source_.size() && source_[pos_] != '\n')
                pos_++;
        }
        else if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '*')
        {
            // Block comment
            pos_ += 2;
            while (pos_ + 1 < source_.size() && !(source_[pos_] == '*' && source_[pos_ + 1] == '/'))
            {
                if (source_[pos_] == '\n')
                    line_++;
                pos_++;
            }
            if (pos_ + 1 < source_.size())
                pos_ += 2;
        }
        else
        {
            break;
        }
    }
}

std::string DefParser::read_string()
{
    pos_++; // skip opening quote
    std::string result;
    while (pos_ < source_.size() && source_[pos_] != '"')
    {
        if (source_[pos_] == '\\' && pos_ + 1 < source_.size())
        {
            pos_++;
            switch (source_[pos_])
            {
            case 'n':
                result += '\n';
                break;
            case 't':
                result += '\t';
                break;
            case '"':
                result += '"';
                break;
            case '\\':
                result += '\\';
                break;
            default:
                result += source_[pos_];
            }
        }
        else
        {
            result += source_[pos_];
        }
        pos_++;
    }
    if (pos_ < source_.size())
        pos_++; // skip closing quote
    return result;
}

std::string DefParser::read_identifier()
{
    std::string result;
    while (pos_ < source_.size() && (std::isalnum(source_[pos_]) || source_[pos_] == '_'))
    {
        result += source_[pos_++];
    }
    return result;
}

DefParser::Token DefParser::next_token()
{
    skip_whitespace_and_comments();

    if (pos_ >= source_.size())
    {
        return {Token::Eof, "", line_};
    }

    char c = source_[pos_];
    int token_line = line_;

    // Single-char tokens
    switch (c)
    {
    case '(':
        pos_++;
        return {Token::LParen, "(", token_line};
    case ')':
        pos_++;
        return {Token::RParen, ")", token_line};
    case '{':
        pos_++;
        return {Token::LBrace, "{", token_line};
    case '}':
        pos_++;
        return {Token::RBrace, "}", token_line};
    case '[':
        pos_++;
        return {Token::LBracket, "[", token_line};
    case ']':
        pos_++;
        return {Token::RBracket, "]", token_line};
    case ':':
        pos_++;
        return {Token::Colon, ":", token_line};
    case ',':
        pos_++;
        return {Token::Comma, ",", token_line};
    case '.':
        pos_++;
        return {Token::Dot, ".", token_line};
    case '@':
        pos_++;
        return {Token::At, "@", token_line};
    case '<':
        pos_++;
        return {Token::Less, "<", token_line};
    case '>':
        pos_++;
        return {Token::Greater, ">", token_line};
    case '=':
        pos_++;
        return {Token::Equals, "=", token_line};
    }

    // String literal
    if (c == '"')
    {
        return {Token::StringLiteral, read_string(), token_line};
    }

    // Identifier or keyword
    if (std::isalpha(c) || c == '_')
    {
        std::string id = read_identifier();
        if (id == "type")
            return {Token::KwType, id, token_line};
        if (id == "def")
            return {Token::KwDef, id, token_line};
        if (id == "shared")
            return {Token::KwShared, id, token_line};
        if (id == "extends")
            return {Token::KwExtends, id, token_line};
        return {Token::Identifier, id, token_line};
    }

    // Skip numbers (not needed in def files, but don't error on them)
    if (std::isdigit(c))
    {
        while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '.'))
        {
            pos_++;
        }
        return next_token(); // Skip and get next real token
    }

    // Unknown - skip
    pos_++;
    return next_token();
}

DefParser::Token DefParser::peek_token()
{
    size_t saved_pos = pos_;
    int saved_line = line_;
    Token t = next_token();
    pos_ = saved_pos;
    line_ = saved_line;
    return t;
}

void DefParser::advance()
{
    current_ = next_token();
}

bool DefParser::match(Token::Type type)
{
    if (current_.type == type)
    {
        advance();
        return true;
    }
    return false;
}

bool DefParser::expect(Token::Type type, const std::string &msg)
{
    if (!match(type))
    {
        std::cerr << "[def_parser] Error at line " << current_.line << ": " << msg
                  << " (got '" << current_.value << "')" << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// DefParser - Parser
// ============================================================

std::pair<std::string, std::string> DefParser::parse_annotation()
{
    // Current token is '@'
    advance(); // consume '@'

    std::string name = current_.value;
    advance(); // consume annotation name

    std::string value;
    if (current_.type == Token::LParen)
    {
        advance(); // consume '('
        if (current_.type == Token::StringLiteral)
        {
            value = current_.value;
            advance();
        }
        expect(Token::RParen, "expected ')' after annotation value");
    }

    return {name, value};
}

std::vector<MethodParam> DefParser::parse_params()
{
    std::vector<MethodParam> params;

    if (!expect(Token::LParen, "expected '(' for parameter list"))
        return params;

    while (current_.type != Token::RParen && current_.type != Token::Eof)
    {
        MethodParam param;

        // Check for function type: def name(args) : ret or def name : ret
        if (current_.type == Token::KwDef)
        {
            advance();
            
            // Function parameter name
            param.name = current_.value;
            advance();
            
            // Build the function type signature
            std::string func_type = "def(";
            
            // Check for parameter list
            if (current_.type == Token::LParen)
            {
                advance();
                bool first_param = true;
                while (current_.type != Token::RParen && current_.type != Token::Eof)
                {
                    if (!first_param) func_type += ",";
                    first_param = false;
                    func_type += current_.value;
                    advance();
                    if (current_.type == Token::Comma) advance();
                }
                if (current_.type == Token::RParen) advance();
            }
            func_type += ")";
            
            // Return type after ':'
            if (current_.type == Token::Colon)
            {
                advance();
                func_type += ":" + current_.value;
                advance();
            }
            
            param.type = func_type;
            
            // Skip optional default value (= void or = something)
            if (current_.type == Token::Equals)
            {
                advance(); // skip '='
                advance(); // skip the default value
            }
        }
        else
        {
            // Regular type
            param.type = current_.value;
            advance();

            // Handle generic types like array<T>
            if (current_.type == Token::Less)
            {
                param.type += "<";
                advance();
                param.type += current_.value;
                advance();
                if (current_.type == Token::Greater)
                {
                    param.type += ">";
                    advance();
                }
            }

            // Name
            param.name = current_.value;
            advance();
        }

        params.push_back(param);

        if (current_.type == Token::Comma)
        {
            advance();
        }
    }

    expect(Token::RParen, "expected ')' after parameters");
    return params;
}

std::optional<MethodDef> DefParser::parse_method(const std::vector<std::pair<std::string, std::string>> &annotations)
{
    MethodDef method;

    // Check for 'shared'
    if (current_.type == Token::KwShared)
    {
        method.is_shared = true;
        advance();
    }

    if (!expect(Token::KwDef, "expected 'def'"))
        return std::nullopt;

    // Method name
    method.name = current_.value;
    advance();

    // Parameters
    method.params = parse_params();

    // Return type
    if (current_.type == Token::Colon)
    {
        advance();
        method.return_type = current_.value;
        advance();

        // Handle generic return types
        if (current_.type == Token::Less)
        {
            method.return_type += "<";
            advance();
            method.return_type += current_.value;
            advance();
            if (current_.type == Token::Greater)
            {
                method.return_type += ">";
                advance();
            }
        }
    }

    // Process annotations
    for (const auto &[name, value] : annotations)
    {
        if (name == "map")
        {
            method.mapping_type = MappingType::Map;
            method.mapping_value = value;
        }
        else if (name == "inline")
        {
            method.mapping_type = MappingType::Inline;
            method.mapping_value = value;
        }
        else if (name == "intrinsic")
        {
            method.mapping_type = MappingType::Intrinsic;
            method.mapping_value = value;
        }
    }

    // Skip method body if present
    if (current_.type == Token::LBrace)
    {
        int depth = 1;
        advance();
        while (depth > 0 && current_.type != Token::Eof)
        {
            if (current_.type == Token::LBrace)
                depth++;
            else if (current_.type == Token::RBrace)
                depth--;
            advance();
        }
    }

    return method;
}

std::optional<TypeDef> DefParser::parse_type()
{
    TypeDef type_def;

    // Collect annotations before 'type'
    std::vector<std::pair<std::string, std::string>> type_annotations;
    while (current_.type == Token::At)
    {
        type_annotations.push_back(parse_annotation());
    }

    if (!expect(Token::KwType, "expected 'type'"))
        return std::nullopt;

    // Type name
    type_def.name = current_.value;
    advance();

    // Handle generic types (e.g., array<T> or array<T, N>)
    if (current_.type == Token::Less)
    {
        advance(); // skip '<'
        advance(); // skip first type parameter
        // Handle multiple type parameters separated by commas
        while (current_.type == Token::Comma)
        {
            advance(); // skip ','
            advance(); // skip next type parameter
        }
        if (current_.type == Token::Greater)
            advance();
    }

    // Check for 'extends'
    if (current_.type == Token::KwExtends)
    {
        advance();
        type_def.extends = current_.value;
        advance();
    }

    // Process type annotations
    for (const auto &[name, value] : type_annotations)
    {
        if (name == "builtin")
        {
            type_def.is_builtin = true;
        }
        else if (name == "nocopy")
        {
            type_def.is_nocopy = true;
        }
        else if (name == "alias")
        {
            type_def.alias_of = value;
        }
    }

    // Parse body
    if (!expect(Token::LBrace, "expected '{'"))
        return std::nullopt;

    while (current_.type != Token::RBrace && current_.type != Token::Eof)
    {
        // Collect method annotations
        std::vector<std::pair<std::string, std::string>> method_annotations;
        while (current_.type == Token::At)
        {
            method_annotations.push_back(parse_annotation());
        }

        // Parse method
        if (current_.type == Token::KwDef || current_.type == Token::KwShared)
        {
            if (auto method = parse_method(method_annotations))
            {
                type_def.methods.push_back(*method);
            }
        }
        else
        {
            // Skip unknown tokens
            advance();
        }
    }

    expect(Token::RBrace, "expected '}'");
    return type_def;
}

std::optional<DefFile> DefParser::parse_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
    {
        std::cerr << "[def_parser] Could not open: " << path << std::endl;
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    source_ = buffer.str();
    pos_ = 0;
    line_ = 1;

    DefFile def_file;
    def_file.path = path;

    advance(); // Get first token

    while (current_.type != Token::Eof)
    {
        // Skip annotations at file level that aren't followed by 'type'
        while (current_.type == Token::At)
        {
            auto peek = peek_token();
            // If next meaningful token after annotation is 'type', break to parse it
            size_t saved_pos = pos_;
            int saved_line = line_;
            parse_annotation();
            if (current_.type == Token::KwType || current_.type == Token::At)
            {
                pos_ = saved_pos;
                line_ = saved_line;
                current_ = {Token::At, "@", saved_line};
                break;
            }
        }

        if (current_.type == Token::KwType || current_.type == Token::At)
        {
            if (auto type_def = parse_type())
            {
                def_file.types.push_back(*type_def);
            }
        }
        else if (current_.type != Token::Eof)
        {
            advance(); // Skip unknown
        }
    }

    return def_file;
}

std::vector<DefFile> DefParser::parse_directory(const std::string &dir_path)
{
    std::vector<DefFile> files;

    if (!fs::exists(dir_path))
    {
        std::cerr << "[def_parser] Directory not found: " << dir_path << std::endl;
        return files;
    }

    for (const auto &entry : fs::recursive_directory_iterator(dir_path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".coi")
        {
            if (auto def_file = parse_file(entry.path().string()))
            {
                files.push_back(*def_file);
            }
        }
    }

    return files;
}

// ============================================================
// DefSchema - Singleton
// ============================================================

DefSchema &DefSchema::instance()
{
    static DefSchema instance;
    return instance;
}

bool DefSchema::is_cache_valid(const std::string &cache_path, const std::string &def_dir)
{
    if (!fs::exists(cache_path))
        return false;

    auto cache_time = fs::last_write_time(cache_path);

    for (const auto &entry : fs::recursive_directory_iterator(def_dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".coi")
        {
            if (fs::last_write_time(entry.path()) > cache_time)
            {
                return false; // A def file is newer than cache
            }
        }
    }

    return true;
}

bool DefSchema::load(const std::string &def_dir)
{
    if (loaded_)
        return true;

    DefParser parser;
    auto files = parser.parse_directory(def_dir);

    for (const auto &file : files)
    {
        for (const auto &type_def : file.types)
        {
            // Merge types with the same name (e.g., System from system.d.coi + intrinsics.d.coi)
            auto it = types_.find(type_def.name);
            if (it != types_.end())
            {
                // Merge methods from the new type into the existing one
                for (const auto &method : type_def.methods)
                {
                    // Check for duplicate method (same name and param count)
                    bool exists = false;
                    for (const auto &existing : it->second.methods)
                    {
                        if (existing.name == method.name &&
                            existing.params.size() == method.params.size())
                        {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists)
                    {
                        it->second.methods.push_back(method);
                    }
                }
                // Merge other properties
                if (type_def.is_builtin)
                    it->second.is_builtin = true;
                if (!type_def.extends.empty() && it->second.extends.empty())
                {
                    it->second.extends = type_def.extends;
                }
            }
            else
            {
                types_[type_def.name] = type_def;
            }
        }
    }

    loaded_ = true;
    std::cout << "[DefSchema] Loaded " << types_.size() << " types from def files" << std::endl;
    return true;
}

bool DefSchema::load_cache(const std::string &cache_path)
{
    std::ifstream file(cache_path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    // Read number of types
    uint32_t type_count;
    file.read(reinterpret_cast<char *>(&type_count), sizeof(type_count));

    auto read_string = [&file]() -> std::string
    {
        uint32_t len;
        file.read(reinterpret_cast<char *>(&len), sizeof(len));
        std::string s(len, '\0');
        file.read(s.data(), len);
        return s;
    };

    for (uint32_t i = 0; i < type_count; ++i)
    {
        TypeDef type_def;
        type_def.name = read_string();
        type_def.is_builtin = file.get() != 0;
        type_def.is_nocopy = file.get() != 0;
        type_def.extends = read_string();
        type_def.alias_of = read_string();

        uint32_t method_count;
        file.read(reinterpret_cast<char *>(&method_count), sizeof(method_count));

        for (uint32_t j = 0; j < method_count; ++j)
        {
            MethodDef method;
            method.name = read_string();
            method.return_type = read_string();
            method.is_shared = file.get() != 0;
            method.mapping_type = static_cast<MappingType>(file.get());
            method.mapping_value = read_string();

            uint32_t param_count;
            file.read(reinterpret_cast<char *>(&param_count), sizeof(param_count));

            for (uint32_t k = 0; k < param_count; ++k)
            {
                MethodParam param;
                param.type = read_string();
                param.name = read_string();
                method.params.push_back(param);
            }

            type_def.methods.push_back(method);
        }

        types_[type_def.name] = type_def;
    }

    loaded_ = true;
    std::cout << "[DefSchema] Loaded " << types_.size() << " types from cache" << std::endl;
    return true;
}

bool DefSchema::save_cache(const std::string &cache_path)
{
    std::ofstream file(cache_path, std::ios::binary);
    if (!file)
        return false;

    auto write_string = [&file](const std::string &s)
    {
        uint32_t len = s.size();
        file.write(reinterpret_cast<const char *>(&len), sizeof(len));
        file.write(s.data(), len);
    };

    uint32_t type_count = types_.size();
    file.write(reinterpret_cast<const char *>(&type_count), sizeof(type_count));

    for (const auto &[name, type_def] : types_)
    {
        write_string(type_def.name);
        file.put(type_def.is_builtin ? 1 : 0);
        file.put(type_def.is_nocopy ? 1 : 0);
        write_string(type_def.extends);
        write_string(type_def.alias_of);

        uint32_t method_count = type_def.methods.size();
        file.write(reinterpret_cast<const char *>(&method_count), sizeof(method_count));

        for (const auto &method : type_def.methods)
        {
            write_string(method.name);
            write_string(method.return_type);
            file.put(method.is_shared ? 1 : 0);
            file.put(static_cast<uint8_t>(method.mapping_type));
            write_string(method.mapping_value);

            uint32_t param_count = method.params.size();
            file.write(reinterpret_cast<const char *>(&param_count), sizeof(param_count));

            for (const auto &param : method.params)
            {
                write_string(param.type);
                write_string(param.name);
            }
        }
    }

    std::cout << "[DefSchema] Saved cache with " << types_.size() << " types" << std::endl;
    return true;
}

const MethodDef *DefSchema::lookup_method(const std::string &type_name, const std::string &method_name) const
{
    auto type_it = types_.find(type_name);
    if (type_it == types_.end())
        return nullptr;

    for (const auto &method : type_it->second.methods)
    {
        if (method.name == method_name)
        {
            return &method;
        }
    }

    // Check parent type
    if (!type_it->second.extends.empty())
    {
        return lookup_method(type_it->second.extends, method_name);
    }

    return nullptr;
}

const MethodDef *DefSchema::lookup_method(const std::string &type_name, const std::string &method_name, size_t arg_count) const
{
    auto type_it = types_.find(type_name);
    if (type_it == types_.end())
        return nullptr;

    for (const auto &method : type_it->second.methods)
    {
        if (method.name == method_name && method.params.size() == arg_count)
        {
            return &method;
        }
    }

    // Check parent type
    if (!type_it->second.extends.empty())
    {
        return lookup_method(type_it->second.extends, method_name, arg_count);
    }

    return nullptr;
}

const TypeDef *DefSchema::lookup_type(const std::string &type_name) const
{
    auto it = types_.find(type_name);
    return it != types_.end() ? &it->second : nullptr;
}

bool DefSchema::inherits_from(const std::string &derived, const std::string &base) const
{
    if (derived == base)
        return true;

    auto type_it = types_.find(derived);
    if (type_it == types_.end())
        return false;

    if (type_it->second.extends.empty())
        return false;
    if (type_it->second.extends == base)
        return true;

    return inherits_from(type_it->second.extends, base);
}

bool DefSchema::is_handle(const std::string &type_name) const
{
    // A handle is a non-builtin type that has methods with @map or @intrinsic annotations
    auto type_it = types_.find(type_name);
    if (type_it == types_.end())
        return false;
    if (type_it->second.is_builtin)
        return false;

    // Check if it has any @map or @intrinsic methods (webcc handle or intrinsic type)
    for (const auto &method : type_it->second.methods)
    {
        if (method.mapping_type == MappingType::Map || 
            method.mapping_type == MappingType::Intrinsic)
        {
            return true;
        }
    }

    // Check parent type
    if (!type_it->second.extends.empty())
    {
        return is_handle(type_it->second.extends);
    }

    return false;
}

bool DefSchema::is_nocopy(const std::string &type_name) const
{
    // Handle array types - check the element type
    std::string base_type = type_name;
    if (type_name.ends_with("[]"))
    {
        base_type = type_name.substr(0, type_name.length() - 2);
    }
    else
    {
        // Check for fixed-size array T[N]
        size_t bracket_pos = type_name.rfind('[');
        if (bracket_pos != std::string::npos && type_name.back() == ']')
        {
            base_type = type_name.substr(0, bracket_pos);
        }
    }

    auto type_it = types_.find(base_type);
    if (type_it == types_.end())
        return false;

    // Check if this type has @nocopy
    if (type_it->second.is_nocopy)
        return true;

    // Check parent type (inheritance)
    if (!type_it->second.extends.empty())
    {
        return is_nocopy(type_it->second.extends);
    }

    return false;
}

std::string DefSchema::resolve_alias(const std::string &type_name) const
{
    auto type_it = types_.find(type_name);
    if (type_it == types_.end())
        return type_name;
    
    if (!type_it->second.alias_of.empty())
    {
        // Recursively resolve in case of alias chains
        return resolve_alias(type_it->second.alias_of);
    }
    
    return type_name;
}

std::string DefSchema::get_namespace_for_type(const std::string &type_name) const
{
    auto type_it = types_.find(type_name);
    if (type_it == types_.end())
        return "";

    // Extract namespace from first @map, @intrinsic, or @inline annotation
    for (const auto &method : type_it->second.methods)
    {
        if (method.mapping_value.empty())
            continue;

        size_t sep = std::string::npos;
        if (method.mapping_type == MappingType::Map)
            sep = method.mapping_value.find("::"); // "ns::func_name"
        else if (method.mapping_type == MappingType::Intrinsic)
            sep = method.mapping_value.find('_');  // "ns_func"
        else if (method.mapping_type == MappingType::Inline)
        {
            // For @inline("webcc::func(${0})"), extract namespace before ::
            sep = method.mapping_value.find("::");
        }

        if (sep != std::string::npos)
            return method.mapping_value.substr(0, sep);
    }

    // Check parent
    if (!type_it->second.extends.empty())
    {
        return get_namespace_for_type(type_it->second.extends);
    }

    return "";
}

void DefSchema::build_map_index() const
{
    if (map_index_built_)
        return;

    for (const auto &[type_name, type_def] : types_)
    {
        for (const auto &method : type_def.methods)
        {
            if (method.mapping_type == MappingType::Map && !method.mapping_value.empty())
            {
                map_index_[method.mapping_value] = {type_name, &method};
            }
        }
    }
    map_index_built_ = true;
}

const MethodDef *DefSchema::lookup_by_map(const std::string &ns, const std::string &func_name) const
{
    build_map_index();

    std::string key = ns + "::" + func_name;
    auto it = map_index_.find(key);
    if (it != map_index_.end())
    {
        return it->second.second;
    }
    return nullptr;
}

std::string DefSchema::to_snake_case(const std::string &camel)
{
    std::string result;
    for (size_t i = 0; i < camel.length(); ++i)
    {
        char c = camel[i];
        if (std::isupper(c))
        {
            if (i > 0)
                result += '_';
            result += std::tolower(c);
        }
        else if (std::isdigit(c))
        {
            // Add underscore before digit if previous char was a letter
            if (i > 0 && std::isalpha(camel[i - 1]))
                result += '_';
            result += c;
        }
        else
        {
            result += c;
        }
    }
    return result;
}

void DefSchema::build_func_index() const
{
    if (func_index_built_)
        return;

    for (const auto &[type_name, type_def] : types_)
    {
        for (const auto &method : type_def.methods)
        {
            if (method.mapping_type == MappingType::Map && !method.mapping_value.empty())
            {
                // mapping_value is "ns::func_name"
                size_t sep = method.mapping_value.find("::");
                if (sep != std::string::npos)
                {
                    std::string ns = method.mapping_value.substr(0, sep);
                    std::string func_name = method.mapping_value.substr(sep + 2);

                    // Index by snake_case func_name
                    FuncLookupResult result;
                    result.ns = ns;
                    result.type_name = type_name;
                    result.method = &method;
                    func_index_[func_name] = result;
                }
            }
        }
    }
    func_index_built_ = true;
}

const DefSchema::FuncLookupResult *DefSchema::lookup_func(const std::string &snake_func_name) const
{
    build_func_index();

    auto it = func_index_.find(snake_func_name);
    if (it != func_index_.end())
    {
        return &it->second;
    }
    return nullptr;
}
