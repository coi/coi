#include "json.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace
{

struct JsonParser
{
    const std::string &t;
    size_t i = 0;

    void ws()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++;
    }
    bool at_end()
    {
        ws();
        return i >= t.size();
    }
    void expect(char c)
    {
        ws();
        if (i >= t.size() || t[i] != c) throw std::runtime_error(std::string("expected '") + c + "'");
        i++;
    }
    static void utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    unsigned hex4()
    {
        if (i + 4 > t.size()) throw std::runtime_error("bad \\u escape");
        unsigned v = (unsigned)std::stoul(t.substr(i, 4), nullptr, 16);
        i += 4;
        return v;
    }
    std::string string()
    {
        expect('"');
        std::string out;
        while (i < t.size())
        {
            char c = t[i++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (i >= t.size()) break;
            char e = t[i++];
            switch (e)
            {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
            {
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u')
                {
                    i += 2;
                    unsigned lo = hex4();
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: out += e;   // \" \\ \/
            }
        }
        throw std::runtime_error("unterminated string");
    }
    Json value()
    {
        ws();
        if (i >= t.size()) throw std::runtime_error("unexpected end");
        char c = t[i];
        if (c == '{')
        {
            i++;
            Json o = Json::object();
            ws();
            if (i < t.size() && t[i] == '}') { i++; return o; }
            while (true)
            {
                std::string k = string();
                expect(':');
                o.obj.emplace_back(k, value());
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                expect('}');
                return o;
            }
        }
        if (c == '[')
        {
            i++;
            Json a = Json::array();
            ws();
            if (i < t.size() && t[i] == ']') { i++; return a; }
            while (true)
            {
                a.arr.push_back(value());
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                expect(']');
                return a;
            }
        }
        if (c == '"') return Json(string());
        if (t.compare(i, 4, "true") == 0) { i += 4; return Json(true); }
        if (t.compare(i, 5, "false") == 0) { i += 5; return Json(false); }
        if (t.compare(i, 4, "null") == 0) { i += 4; return Json(); }
        const char *start = t.c_str() + i;
        char *end = nullptr;
        double d = std::strtod(start, &end);
        if (end == start) throw std::runtime_error("unexpected character");
        i += end - start;
        return Json(d);
    }
};

} // namespace

void dump(const Json &j, std::string &out)
{
    switch (j.kind)
    {
    case Json::Null: out += "null"; break;
    case Json::Bool: out += j.b ? "true" : "false"; break;
    case Json::Num:
    {
        char buf[32];
        if (std::floor(j.n) == j.n && std::fabs(j.n) < 1e15) std::snprintf(buf, sizeof(buf), "%lld", (long long)j.n);
        else std::snprintf(buf, sizeof(buf), "%.17g", j.n);
        out += buf;
        break;
    }
    case Json::Str:
        out += '"';
        for (unsigned char c : j.s)
        {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else out += (char)c;
        }
        out += '"';
        break;
    case Json::Arr:
        out += '[';
        for (size_t k = 0; k < j.arr.size(); k++)
        {
            if (k) out += ',';
            dump(j.arr[k], out);
        }
        out += ']';
        break;
    case Json::Obj:
        out += '{';
        for (size_t k = 0; k < j.obj.size(); k++)
        {
            if (k) out += ',';
            dump(Json(j.obj[k].first), out);
            out += ':';
            dump(j.obj[k].second, out);
        }
        out += '}';
        break;
    }
}

std::string dump(const Json &j)
{
    std::string out;
    dump(j, out);
    return out;
}

Json parse_json(const std::string &text)
{
    JsonParser p{text};
    Json v = p.value();
    if (!p.at_end()) throw std::runtime_error("trailing characters");
    return v;
}

std::vector<Json> parse_json_all(const std::string &text)
{
    std::vector<Json> out;
    JsonParser p{text};
    while (!p.at_end())
    {
        try { out.push_back(p.value()); }
        catch (...) { break; }
    }
    return out;
}
