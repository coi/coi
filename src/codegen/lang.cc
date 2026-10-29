#include "lang.h"
#include "../cli/error.h"
#include "../cli/json.h"
#include "codegen_utils.h"
#include "json_codegen.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

// CLDR's plural categories: every language sorts numbers into some of these (English one and
// other, Polish one, few, many, other, Arabic all six), and Intl.PluralRules says which one a
// number is in. A plural gets a slot for each, in this order, so any language's forms fit
static const char *const kCategories[] = {"zero", "one", "two", "few", "many", "other"};
static const int kCategoryCount = 6;

LangTable &LangTable::instance()
{
    static LangTable t;
    return t;
}

static std::string read_file(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// "settings": {"title": "Settings"} is "settings.title": groups are only for reading
static void flatten(const Json &j, const std::string &prefix, const std::string &file,
                    std::vector<std::pair<std::string, std::string>> &out)
{
    for (const auto &[k, v] : j.obj)
    {
        std::string key = prefix.empty() ? k : prefix + "." + k;
        if (v.kind == Json::Str)
            out.push_back({key, v.s});
        else if (v.kind == Json::Obj)
            flatten(v, key, file, out);
        else
            ErrorHandler::compiler_error(file + ": '" + key + "' is neither text nor a group of texts");
    }
}

static std::vector<std::pair<std::string, std::string>> read_texts(const fs::path &p)
{
    std::string file = "lang/" + p.filename().string();
    Json j;
    try
    {
        j = parse_json(read_file(p));
    }
    catch (const std::exception &e)
    {
        ErrorHandler::compiler_error(file + " isn't valid JSON: " + e.what());
    }
    if (j.kind != Json::Obj)
        ErrorHandler::compiler_error(file + " has to be one object of texts");
    std::vector<std::pair<std::string, std::string>> out;
    flatten(j, "", file, out);
    return out;
}

static bool is_category(const std::string &s)
{
    for (int i = 0; i < kCategoryCount; i++)
        if (s == kCategories[i]) return true;
    return false;
}

// ---- tags in a text ----

// a text cut at its tags: the runs between them, the words inside each, the order they come in
struct Cut
{
    std::vector<std::string> gaps;   // one more than there are tags
    std::vector<std::string> order;
    std::map<std::string, std::string> inner;
    std::map<std::string, bool> paired;
};

// <name>, </name> or <name/> at i; anything else with a '<' in it (a < b) is just text
static bool read_tag(const std::string &s, size_t i, std::string &name, bool &self, bool &close, size_t &end)
{
    size_t j = i + 1;
    close = j < s.size() && s[j] == '/';
    if (close) j++;
    size_t from = j;
    while (j < s.size() && (std::isalpha((unsigned char)s[j]) || (j > from && std::isdigit((unsigned char)s[j]))))
        j++;
    if (j == from) return false;
    name = s.substr(from, j - from);
    self = !close && j + 1 < s.size() && s[j] == '/' && s[j + 1] == '>';
    if (self) { end = j + 2; return true; }
    if (j < s.size() && s[j] == '>') { end = j + 1; return true; }
    return false;
}

static Cut cut_tags(const std::string &text, const std::string &file, const std::string &key)
{
    Cut c;
    std::string run;
    size_t i = 0;
    while (i < text.size())
    {
        std::string name;
        bool self = false, close = false;
        size_t end = 0;
        if (text[i] != '<' || !read_tag(text, i, name, self, close, end))
        {
            run += text[i++];
            continue;
        }
        if (close)
            ErrorHandler::compiler_error(file + ": '" + key + "' closes </" + name + "> without opening it");
        if (c.paired.count(name))
            ErrorHandler::compiler_error(file + ": '" + key + "' has <" + name + "> twice");
        c.gaps.push_back(run);
        run.clear();
        c.order.push_back(name);
        c.paired[name] = !self;
        i = end;
        if (self) continue;
        std::string closing = "</" + name + ">";
        size_t stop = text.find(closing, end);
        if (stop == std::string::npos)
            ErrorHandler::compiler_error(file + ": '" + key + "' opens <" + name + "> and never closes it");
        std::string inner = text.substr(end, stop - end);
        for (size_t j = 0; j < inner.size(); j++)
        {
            std::string n2;
            bool s2, c2;
            size_t e2;
            if (inner[j] == '<' && read_tag(inner, j, n2, s2, c2, e2))
                ErrorHandler::compiler_error(file + ": '" + key + "' has <" + n2 + "> inside <" + name + ">: tags don't nest");
        }
        c.inner[name] = inner;
        i = stop + closing.size();
    }
    c.gaps.push_back(run);
    return c;
}

// "<terms>…</terms>, <logo/>": a text's tags, for errors
static std::string describe_tags(const LangMarkup &m)
{
    std::string out;
    for (const auto &n : m.names)
    {
        if (!out.empty()) out += ", ";
        out += m.paired.at(n) ? "<" + n + ">…</" + n + ">" : "<" + n + "/>";
    }
    return out;
}

void LangTable::load(const fs::path &project_root)
{
    *this = LangTable{};
    fs::path dir = project_root / "lang";
    if (!fs::exists(dir / "en.json"))
        return;
    present = true;
    auto texts = read_texts(dir / "en.json");
    std::map<std::string, std::string> by_key;
    for (const auto &[k, v] : texts)
    {
        if (by_key.count(k))
            ErrorHandler::compiler_error("lang/en.json has '" + k + "' twice");
        by_key[k] = v;
    }
    // a plural (key.other there) gets a slot for every category, side by side, so a language
    // with more forms than English has somewhere to put them; English's missing ones are its other
    std::map<std::string, bool> placed;
    for (const auto &[k, v] : texts)
    {
        size_t dot = k.rfind('.');
        std::string base = dot == std::string::npos ? "" : k.substr(0, dot);
        if (!base.empty() && is_category(k.substr(dot + 1)) && by_key.count(base + ".other"))
        {
            if (placed[base]) continue;
            placed[base] = true;
            for (int c = 0; c < kCategoryCount; c++)
            {
                std::string slot = base + "." + kCategories[c];
                auto it = by_key.find(slot);
                add(slot, it != by_key.end() ? it->second : by_key[base + ".other"]);
            }
            continue;
        }
        add(k, v);
    }
    codes.push_back("en");
    std::vector<std::string> found;
    static const std::regex code_re("[a-z]{2,3}(-[A-Za-z0-9]{2,8})*");
    for (const auto &entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::string code = entry.path().stem().string();
        if (code == "en") continue;
        if (!std::regex_match(code, code_re))
            ErrorHandler::compiler_error("lang/" + entry.path().filename().string() + ": the name has to be a language code, like de.json or pt-BR.json");
        found.push_back(code);
    }
    std::sort(found.begin(), found.end());
    codes.insert(codes.end(), found.begin(), found.end());

    for (const std::string &code : found)
    {
        std::map<std::string, std::string> mine;
        for (const auto &[k, v] : read_texts(dir / (code + ".json")))
        {
            if (find(k) < 0)
                std::cerr << "Warning: lang/" << code << ".json has '" << k << "', which lang/en.json doesn't" << std::endl;
            mine[k] = v;
        }
        // in en.json's order; a plural form the language doesn't have is its other, and a text
        // it doesn't have at all is English's
        std::vector<std::string> list = en;
        for (size_t i = 0; i < keys.size(); i++)
        {
            const std::string &k = keys[i];
            auto it = mine.find(k);
            std::string v = it != mine.end() ? it->second : "";
            size_t dot = k.rfind('.');
            if (v.empty() && dot != std::string::npos && is_category(k.substr(dot + 1)) && plural(k.substr(0, dot)) >= 0)
            {
                auto other = mine.find(k.substr(0, dot) + ".other");
                if (other != mine.end()) v = other->second;
            }
            if (!v.empty()) list[i] = v;
        }
        others[code] = std::move(list);
    }
}

void LangTable::add(const std::string &key, const std::string &text)
{
    index[key] = (int)keys.size();
    keys.push_back(key);
    en.push_back(text);
    for (auto &[code, list] : others)
        list.push_back(text);
}

const LangMarkup *LangTable::cut(const std::string &key)
{
    auto have = markup.find(key);
    if (have != markup.end()) return &have->second;
    int i = find(key);
    if (i < 0) return nullptr;
    Cut cut = cut_tags(en[i], "lang/en.json", key);
    if (cut.order.empty()) return nullptr;
    // the pieces every language's text falls into, after the texts themselves
    LangMarkup m;
    m.names = cut.order;
    m.paired = cut.paired;
    m.orders.push_back(cut.order);
    std::vector<std::pair<std::string, Cut>> cuts;   // by language, English first
    cuts.push_back({"en", cut});
    for (const auto &[code, list] : others)
    {
        std::string file = "lang/" + code + ".json";
        Cut c = cut_tags(list[i], file, key);
        bool same = c.order.size() == m.names.size();
        for (const auto &n : c.order)
            same = same && m.paired.count(n) && m.paired[n] == c.paired[n];
        if (!same)
            ErrorHandler::compiler_error(file + ": '" + key + "' has to have the tags lang/en.json gives it: " + describe_tags(m));
        if (std::find(m.orders.begin(), m.orders.end(), c.order) == m.orders.end())
            m.orders.push_back(c.order);
        cuts.push_back({code, c});
    }
    auto piece = [&](const std::string &name, auto value) {
        std::string slot = key + "#" + name;
        add(slot, value(cuts[0].second));
        for (size_t c = 1; c < cuts.size(); c++)
            others[cuts[c].first][index[slot]] = value(cuts[c].second);
    };
    for (size_t g = 0; g <= cut.order.size(); g++)
    {
        bool used = false;
        for (const auto &[code, c] : cuts)
            used = used || !c.gaps[g].empty();
        m.gap_used.push_back(used);
        piece(std::to_string(g), [g](const Cut &c) { return c.gaps[g]; });
    }
    for (const auto &n : cut.order)
        if (cut.paired[n])
            piece(n, [&n](const Cut &c) { return c.inner.at(n); });
    piece("order", [&m](const Cut &c) {
        return std::to_string(std::find(m.orders.begin(), m.orders.end(), c.order) - m.orders.begin());
    });
    return &(markup[key] = m);
}

int LangTable::find(const std::string &key) const
{
    auto it = index.find(key);
    return it == index.end() ? -1 : it->second;
}

int LangTable::plural(const std::string &key) const
{
    int other = find(key + ".other");
    if (other < 0) return -1;
    return other - (kCategoryCount - 1);
}

void write_lang_files(const fs::path &out_dir)
{
    const LangTable &t = LangTable::instance();
    fs::path dir = out_dir / "lazy" / "lang";
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (t.others.empty()) return;
    fs::create_directories(dir);
    for (const auto &[code, texts] : t.others)
    {
        std::vector<Json> list;
        for (const std::string &v : texts)
            list.push_back(Json(v));
        std::ofstream(dir / (code + ".json")) << dump(Json::array(list));
    }
}

void emit_lang_runtime(std::ostream &out, const std::string &base, bool test_program)
{
    const LangTable &t = LangTable::instance();
    size_t n = t.keys.size();
    out << "// lang/: en.json built in, the visitor's language over it before the app mounts\n";
    out << "static const char* const __coi_lang_en[] = {";
    for (size_t i = 0; i < n; i++)
        out << (i ? ", " : "") << cpp_string_literal(t.en[i]);
    if (n == 0) out << "\"\"";
    out << "};\n";
    out << "static const char* const __coi_lang_codes[] = {";
    for (size_t i = 0; i < t.codes.size(); i++)
        out << (i ? ", " : "") << cpp_string_literal(t.codes[i]);
    out << "};\n";
    out << "static const int __coi_lang_n = " << n << ";\n";
    out << "static const int __coi_lang_code_count = " << t.codes.size() << ";\n";
    out << R"(static coi::string* __coi_lang_tab = nullptr;
static coi::string __coi_lang_code = "en";
static coi::string* __coi_lang_table() {
    if (!__coi_lang_tab) {
        __coi_lang_tab = new coi::string[__coi_lang_n > 0 ? __coi_lang_n : 1];
        for (int i = 0; i < __coi_lang_n; i++) __coi_lang_tab[i] = coi::string(__coi_lang_en[i]);
    }
    return __coi_lang_tab;
}
// {0}, {1}, ... in a text: the arguments, in order
static coi::string __coi_lang_fill(const coi::string& s, const coi::vector<coi::string>& args) {
    if (args.empty()) return s;
    webcc::hybrid_formatter<256> f;
    const char* d = s.c_str();
    uint32_t len = s.length(), start = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (d[i] != '{') continue;
        uint32_t j = i + 1, k = 0;
        while (j < len && d[j] >= '0' && d[j] <= '9') k = k * 10 + (d[j++] - '0');
        if (j == i + 1 || j >= len || d[j] != '}' || k >= args.size()) continue;
        f << webcc::string_view(d + start, i - start) << webcc::string_view(args[k].c_str(), args[k].length());
        start = j + 1;
        i = j;
    }
    f << webcc::string_view(d + start, len - start);
    return coi::string(f.c_str());
}
static coi::string __coi_lang_t(int i, const coi::vector<coi::string>& args = {}) {
    return __coi_lang_fill(__coi_lang_table()[i], args);
}
// the form for n: the language's plural rule picks one of the six slots from base; # is n
static coi::string __coi_lang_plural(int base, int64_t n, const coi::vector<coi::string>& args = {}) {
    coi::string c = webcc::system::plural_category(__coi_lang_code, (double)n);
    int k = c == "zero" ? 0 : c == "one" ? 1 : c == "two" ? 2 : c == "few" ? 3 : c == "many" ? 4 : c == "other" ? 5 : (n == 1 ? 1 : 5);
    const coi::string& s = __coi_lang_table()[base + k];
    webcc::hybrid_formatter<32> num;
    num << n;
    webcc::hybrid_formatter<256> f;
    const char* d = s.c_str();
    uint32_t start = 0;
    for (uint32_t i = 0; i < s.length(); i++) {
        if (d[i] != '#') continue;
        f << webcc::string_view(d + start, i - start) << num.c_str();
        start = i + 1;
    }
    f << webcc::string_view(d + start, s.length() - start);
    return __coi_lang_fill(coi::string(f.c_str()), args);
}
static coi::vector<coi::string> __coi_lang_available() {
    coi::vector<coi::string> out;
    for (int i = 0; i < __coi_lang_code_count; i++) out.push_back(coi::string(__coi_lang_codes[i]));
    return out;
}
static bool __coi_lang_known(const coi::string& code) {
    for (int i = 0; i < __coi_lang_code_count; i++) if (code == __coi_lang_codes[i]) return true;
    return false;
}
)";
    if (test_program)
    {
        // a test runs in English; Lang.use changes the language it reports, nothing more
        out << "static void __coi_lang_use(const coi::string& code) { if (__coi_lang_known(code)) __coi_lang_code = code; }\n";
        return;
    }
    out << R"(// a choice is kept and the app starts again in it
static void __coi_lang_use(const coi::string& code) {
    if (!__coi_lang_known(code)) return;
    webcc::storage::set_item("lang", code);
    webcc::system::reload();
}
// the language this visit wants: a choice kept by Lang.use, else the first of the browser's
// languages there's a file for ("de-AT" takes de when there's no de-AT)
static coi::string __coi_lang_pick() {
    coi::string kept = webcc::storage::get_item("lang");
    if (__coi_lang_known(kept)) return kept;
    coi::string all = webcc::system::get_languages();
    const char* d = all.c_str();
    uint32_t len = all.length(), start = 0;
    for (uint32_t i = 0; i <= len; i++) {
        if (i < len && d[i] != ',') continue;
        coi::string want(d + start, i - start);
        start = i + 1;
        if (__coi_lang_known(want)) return want;
        const char* w = want.c_str();
        uint32_t dash = 0;
        while (dash < want.length() && w[dash] != '-') dash++;
        coi::string primary(w, dash);
        if (__coi_lang_known(primary)) return primary;
    }
    return "en";
}
static webcc::FetchRequest __coi_lang_fetch;
static bool __coi_lang_waiting = false;
static void __coi_mount();
// true while the language's file is on its way: the app mounts when it's here
static bool __coi_lang_start() {
    if (__coi_lang_code_count < 2) return false;
    coi::string code = __coi_lang_pick();
    if (code == "en") return false;
    __coi_lang_code = code;
    __coi_lang_waiting = true;
)";
    out << "    __coi_lang_fetch = webcc::fetch::get(coi::string::concat(" << cpp_string_literal(base + "lazy/lang/") << ", code, \".json\"));\n";
    out << R"(    return true;
}
// the language's texts over English's, then the app; without them (offline before it was ever
// fetched) the app comes in English
static bool __coi_lang_event(const webcc::Event& e) {
    if (!__coi_lang_waiting) return false;
    if (auto s = e.as<webcc::fetch::SuccessEvent>()) {
        if (!(s->id == __coi_lang_fetch)) return false;
        coi::string text(s->data);
        auto r = )";
    out << generate_json_parse("string[]", "text");
    out << R"(;
        coi::string* tab = __coi_lang_table();
        if (r.ok && (int)r.value.size() == __coi_lang_n) {
            for (int i = 0; i < __coi_lang_n; i++) tab[i] = r.value[i];
        } else __coi_lang_code = "en";
        __coi_lang_waiting = false;
        __coi_mount();
        return true;
    }
    if (auto er = e.as<webcc::fetch::ErrorEvent>()) {
        if (!(er->id == __coi_lang_fetch)) return false;
        __coi_lang_code = "en";
        __coi_lang_waiting = false;
        __coi_mount();
        return true;
    }
    return false;
}
)";
}
