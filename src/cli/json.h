#pragma once

// JSON as the compiler itself reads and writes it: coi mcp's JSON-RPC, and a project's lang/*.json.
// Objects keep their keys in order.

#include <string>
#include <utility>
#include <vector>

struct Json
{
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;   // in order, for readable output

    Json() {}
    Json(bool v) : kind(Bool), b(v) {}
    Json(int v) : kind(Num), n(v) {}
    Json(double v) : kind(Num), n(v) {}
    Json(const char *v) : kind(Str), s(v) {}
    Json(std::string v) : kind(Str), s(std::move(v)) {}

    static Json array(std::vector<Json> v = {})
    {
        Json j;
        j.kind = Arr;
        j.arr = std::move(v);
        return j;
    }
    static Json object(std::vector<std::pair<std::string, Json>> v = {})
    {
        Json j;
        j.kind = Obj;
        j.obj = std::move(v);
        return j;
    }

    const Json *get(const std::string &key) const
    {
        if (kind != Obj) return nullptr;
        for (const auto &[k, v] : obj)
            if (k == key) return &v;
        return nullptr;
    }
    std::string str(const std::string &key) const
    {
        const Json *v = get(key);
        return v && v->kind == Str ? v->s : "";
    }
};

// throws std::runtime_error on anything that isn't one JSON value
Json parse_json(const std::string &text);
// JSON values one after another, up to the first that isn't one
std::vector<Json> parse_json_all(const std::string &text);
std::string dump(const Json &j);
void dump(const Json &j, std::string &out);
