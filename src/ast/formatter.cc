#include "formatter.h"
#include "expressions.h"

bool is_string_expr(Expression* expr) {
    if (dynamic_cast<StringLiteral*>(expr)) return true;
    if (auto* bin = dynamic_cast<BinaryOp*>(expr)) {
        if (bin->op == "+" && is_string_expr(bin->left.get())) return true;
    }
    return false;
}

void flatten_string_concat(Expression* expr, std::vector<Expression*>& parts) {
    if (auto* bin = dynamic_cast<BinaryOp*>(expr)) {
        if (bin->op == "+" && is_string_expr(bin->left.get())) {
            flatten_string_concat(bin->left.get(), parts);
            flatten_string_concat(bin->right.get(), parts);
            return;
        }
    }
    parts.push_back(expr);
}

std::string generate_formatter_appends(const std::vector<std::string>& parts) {
    std::string code;
    for (const auto& part : parts) {
        code += "_fmt << (" + part + "); ";
    }
    return code;
}

std::string generate_formatter_appends(const std::vector<Expression*>& parts) {
    std::vector<std::string> str_parts;
    for (auto* part : parts) {
        str_parts.push_back(part->to_webcc());
    }
    return generate_formatter_appends(str_parts);
}

std::string generate_formatter_expr(const std::vector<Expression*>& parts) {
    std::string code = "([&]() { webcc::hybrid_formatter<" + std::to_string(FORMATTER_BUFFER_SIZE) + "> _fmt; ";
    code += generate_formatter_appends(parts);
    code += "return coi::string(_fmt.c_str()); }())";
    return code;
}

std::string generate_formatter_block(const std::vector<std::string>& parts,
                                     const std::string& callback_prefix,
                                     const std::string& callback_suffix) {
    std::string code = "{ webcc::hybrid_formatter<" + std::to_string(FORMATTER_BUFFER_SIZE) + "> _fmt; ";
    code += generate_formatter_appends(parts);
    code += callback_prefix + "_fmt.c_str()" + callback_suffix + "; }";
    return code;
}

std::string generate_formatter_block(const std::vector<Expression*>& parts,
                                     const std::string& callback_prefix,
                                     const std::string& callback_suffix) {
    std::vector<std::string> str_parts;
    for (auto* part : parts) {
        str_parts.push_back(part->to_webcc());
    }
    return generate_formatter_block(str_parts, callback_prefix, callback_suffix);
}

std::string generate_formatter_block_from_string_literal(StringLiteral* strLit,
                                                         const std::string& callback_prefix,
                                                         const std::string& callback_suffix) {
    std::string code = "{ webcc::hybrid_formatter<" + std::to_string(FORMATTER_BUFFER_SIZE) + "> _fmt; ";
    auto parts = strLit->parse();
    for (auto& p : parts) {
        if (p.is_expr) {
            code += "_fmt << (" + p.content + "); ";
        } else {
            code += "_fmt << \"" + p.content + "\"; ";
        }
    }
    code += callback_prefix + "_fmt.c_str()" + callback_suffix + "; }";
    return code;
}
