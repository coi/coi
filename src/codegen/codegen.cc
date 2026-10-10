#include <functional>
#include "codegen.h"
#include "codegen_utils.h"
#include "ast/ast.h"
#include "ast/codegen_state.h"
#include "../analysis/feature_detector.h"
#include "../analysis/dependency_resolver.h"
#include "json_codegen.h"
#include <iostream>

// one case per webcc event with a bound callback
static void emit_event_dispatch(std::ostream &out, const FeatureFlags &features)
{
    for (const auto &[key, ev] : g_used_events)
    {
        // keyboard feature dispatches these itself
        if (features.keyboard && (key == "input::KEY_DOWN" || key == "input::KEY_UP"))
            continue;
        out << "        } else if (e.opcode == " << ev.struct_name << "::OPCODE) {\n";
        out << "            if (auto evt = e.as<" << ev.struct_name << ">()) {\n";
        if (ev.key == "-")
        {
            // Page-wide: every component that registered
            out << "                coi_events<" << ev.struct_name << ">.dispatch_all(*evt);\n";
            out << "            }\n";
            continue;
        }
        out << "                coi_events<" << ev.struct_name << ">.dispatch(evt->" << ev.key << ", *evt);\n";
        if (ev.last)
            for (const auto &[key2, other] : g_used_events)
                if (other.handle_type == ev.handle_type)
                    out << "                coi_events<" << other.struct_name << ">.remove(evt->" << ev.key << ");\n";
        out << "            }\n";
    }
}

void generate_cpp_code(
    std::ostream &out,
    std::vector<Component> &all_components,
    const std::vector<std::unique_ptr<DataDef>> &all_global_data,
    const std::vector<std::unique_ptr<EnumDef>> &all_global_enums,
    const std::vector<std::unique_ptr<FunctionDef>> &all_global_functions,
    const AppConfig &final_app_config,
    const std::set<std::string> &required_headers,
    const FeatureFlags &features)
{
    // Include required headers
    for (const auto &header : required_headers)
    {
        out << "#include \"webcc/" << header << ".h\"\n";
    }
    out << "#include \"webcc/core/function.h\"\n";
    out << "#include \"webcc/core/allocator.h\"\n";
    out << "#include \"webcc/core/new.h\"\n";
    out << "#include \"webcc/core/string.h\"\n";
    out << "#include \"webcc/core/array.h\"\n";
    out << "#include \"webcc/core/vector.h\"\n";
    out << "#include \"webcc/core/unordered_map.h\"\n";
    out << "#include \"webcc/core/random.h\"\n";
    out << "#include \"webcc/core/math.h\"\n";
    out << "\n";
    out << "namespace coi {\n";
    out << "using string = webcc::string;\n";
    out << "using string_view = webcc::string_view;\n";
    out << "template<typename T> using vector = webcc::vector<T>;\n";
    out << "}\n";
    // bytes point into the event buffer, copy them
    out << "inline coi::vector<uint8_t> coi_bytes(webcc::bytes_view v) {\n";
    out << "    coi::vector<uint8_t> out; out.reserve(v.length());\n";
    out << "    for (uint32_t i = 0; i < v.length(); i++) out.push_back(v.data()[i]);\n";
    out << "    return out;\n";
    out << "}\n";
    out << "namespace coi {\n";
    out << "template<typename T, size_t N> using array = webcc::array<T, N>;\n";
    out << "template<typename K, typename V> using map = webcc::unordered_map<K, V>;\n";
    out << "template<typename Signature> using function = webcc::function<Signature>;\n";
    out << "using webcc::move;\n";
    // view updates after a method, on every return
    out << "template<typename F> struct OnExit { F f; ~OnExit() { f(); } };\n";
    out << "template<typename F> OnExit<F> on_exit(F f) { return OnExit<F>{f}; }\n";
    out << "using webcc::malloc;\n";
    out << "namespace math {\n";
    out << "inline constexpr double PI = 3.14159265358979323846;\n";
    out << "inline constexpr double HALF_PI = PI / 2;\n";
    out << "inline constexpr double TAU = PI * 2;\n";
    out << "inline constexpr double DEG2RAD = PI / 180;\n";
    out << "inline constexpr double RAD2DEG = 180 / PI;\n";
    out << "inline constexpr double E = 2.71828182845904523536;\n";
    out << "using webcc::abs; using webcc::sqrt; using webcc::floor; using webcc::ceil; using webcc::round; using webcc::trunc;\n";
    out << "using webcc::min; using webcc::max; using webcc::clamp; using webcc::lerp; using webcc::hypot;\n";
    out << "using webcc::sin; using webcc::cos; using webcc::tan; using webcc::asin; using webcc::acos; using webcc::atan; using webcc::atan2;\n";
    out << "using webcc::exp; using webcc::log; using webcc::log2; using webcc::log10; using webcc::pow;\n";
    out << "}\n";
    // Html.escape / Html.toText
    out << "namespace html {\n";
    out << "inline string escape(const string& s) {\n";
    out << "    string out; for (uint32_t i = 0; i < s.length(); i++) { char c = s.data()[i];\n";
    out << "        if (c == '&') out += \"&amp;\"; else if (c == '<') out += \"&lt;\"; else if (c == '>') out += \"&gt;\";\n";
    out << "        else if (c == '\"') out += \"&quot;\"; else if (c == '\\'') out += \"&#39;\"; else out += string(&c, 1); }\n";
    out << "    return out;\n";
    out << "}\n";
    // tags go, a <br> or the end of a block element is a line break, the common entities are decoded
    out << "inline bool html_block_end(const string& tag) {\n";
    out << "    static const char* ends[] = {\"/p\", \"/div\", \"/li\", \"/h1\", \"/h2\", \"/h3\", \"/h4\", \"/h5\", \"/h6\", \"/tr\", \"/blockquote\", \"/pre\", \"br\", \"br/\", \"br /\"};\n";
    out << "    for (const char* e : ends) { uint32_t n = 0; while (e[n]) n++;\n";
    out << "        if (tag.length() >= n && tag.substr(0, n).to_lower() == string(e) && (tag.length() == n || tag.data()[n] == ' ')) return true; }\n";
    out << "    return false;\n";
    out << "}\n";
    out << "inline string to_text(const string& h) {\n";
    out << "    string out; uint32_t i = 0, n = h.length(); const char* d = h.data();\n";
    out << "    while (i < n) { char c = d[i];\n";
    out << "        if (c == '<') { int end = h.index_of(\">\", i); if (end < 0) break;\n";
    out << "            if (html_block_end(h.substr(i + 1, end - i - 1))) out += \"\\n\"; i = end + 1; continue; }\n";
    out << "        if (c == '&') { int end = h.index_of(\";\", i);\n";
    out << "            if (end > (int)i && end - (int)i < 8) { string e = h.substr(i + 1, end - i - 1);\n";
    out << "                if (e == \"amp\") out += \"&\"; else if (e == \"lt\") out += \"<\"; else if (e == \"gt\") out += \">\";\n";
    out << "                else if (e == \"quot\") out += \"\\\"\"; else if (e == \"apos\" || e == \"#39\") out += \"'\"; else if (e == \"nbsp\") out += \" \";\n";
    out << "                else { out += string(d + i, end - i + 1); }\n";
    out << "                i = end + 1; continue; } }\n";
    out << "        out += string(&c, 1); i++; }\n";
    out << "    return out;\n";
    out << "}\n";
    out << "}\n";
    out << "// Cast helpers backing the builtin toInt/toFloat/toString methods.\n";
    out << "// Overloaded so one inline template stays valid for any receiver type.\n";
    out << "inline int to_int(const string& s) { return s.to_int(); }\n";
    out << "inline int to_int(double v) { return (int)v; }\n";
    out << "inline int to_int(float v) { return (int)v; }\n";
    out << "inline int to_int(int v) { return v; }\n";
    out << "inline int to_int(unsigned int v) { return (int)v; }\n";
    out << "inline int to_int(long v) { return (int)v; }\n";
    out << "inline int to_int(unsigned long v) { return (int)v; }\n";
    out << "inline int to_int(long long v) { return (int)v; }\n";
    out << "inline int to_int(unsigned long long v) { return (int)v; }\n";
    out << "inline double to_float(const string& s) { return s.to_float(); }\n";
    out << "inline double to_float(double v) { return v; }\n";
    out << "inline double to_float(float v) { return (double)v; }\n";
    out << "inline double to_float(int v) { return (double)v; }\n";
    out << "inline double to_float(unsigned int v) { return (double)v; }\n";
    out << "inline double to_float(long v) { return (double)v; }\n";
    out << "inline double to_float(unsigned long v) { return (double)v; }\n";
    out << "inline double to_float(long long v) { return (double)v; }\n";
    out << "inline double to_float(unsigned long long v) { return (double)v; }\n";
    out << "inline vector<uint8_t> string_to_bytes(const string& s) { vector<uint8_t> v; v.reserve(s.length()); for (uint32_t i = 0; i < s.length(); i++) v.push_back((uint8_t)s.data()[i]); return v; }\n";
    out << "inline string string_from_bytes(const vector<uint8_t>& b) { return b.size() ? string((const char*)&b[0], (uint32_t)b.size()) : string(); }\n";
    out << "inline string to_string(const string& s) { return s; }\n";
    out << "inline string to_string(bool v) { return string(v ? \"true\" : \"false\"); }\n";
    out << "template<typename T> inline string to_string(T v) { webcc::formatter<32> f; f << v; return string(f.c_str()); }\n";
    // Date.format: civil-from-days (Howard Hinnant's algorithm) on the local instant, then the pattern
    out << "namespace date {\n";
    out << "inline long long fdiv(long long a, long long b) { return (a >= 0 ? a : a - b + 1) / b; }\n";
    out << "inline string two(long long n) { return n < 10 ? string(\"0\") + to_string((int)n) : to_string((int)n); }\n";
    out << "inline string format(double ms, const string& pattern) {\n";
    out << "    static const char* wd[] = {\"Sunday\", \"Monday\", \"Tuesday\", \"Wednesday\", \"Thursday\", \"Friday\", \"Saturday\"};\n";
    out << "    static const char* mo[] = {\"January\", \"February\", \"March\", \"April\", \"May\", \"June\", \"July\", \"August\", \"September\", \"October\", \"November\", \"December\"};\n";
    out << "    long long local = (long long)(ms + webcc::system::get_timezone_offset_at(ms));\n";
    out << "    long long days = fdiv(local, 86400000), rest = local - days * 86400000;\n";
    out << "    long long hour = rest / 3600000, minute = rest / 60000 % 60, second = rest / 1000 % 60;\n";
    out << "    long long z = days + 719468, era = fdiv(z, 146097), doe = z - era * 146097;\n";
    out << "    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);\n";
    out << "    long long mp = (5 * doy + 2) / 153, day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;\n";
    out << "    long long year = yoe + era * 400 + (month <= 2 ? 1 : 0), weekday = ((days + 4) % 7 + 7) % 7, h12 = hour % 12 ? hour % 12 : 12;\n";
    out << "    string out; const char* p = pattern.data(); uint32_t n = pattern.length(), i = 0;\n";
    out << "    auto has = [&](const char* t) { uint32_t k = 0; while (t[k]) { if (i + k >= n || p[i + k] != t[k]) return false; k++; } return true; };\n";
    out << "    while (i < n) {\n";
    out << "        if (p[i] == '[') { uint32_t e = i + 1; while (e < n && p[e] != ']') e++; out += string(p + i + 1, e - i - 1); i = e < n ? e + 1 : e; continue; }\n";
    out << "        if (has(\"YYYY\")) { out += to_string((int)year); i += 4; } else if (has(\"YY\")) { out += two(year % 100); i += 2; }\n";
    out << "        else if (has(\"MMMM\")) { out += string(mo[month - 1]); i += 4; } else if (has(\"MMM\")) { out += string(mo[month - 1], 3); i += 3; }\n";
    out << "        else if (has(\"MM\")) { out += two(month); i += 2; } else if (has(\"M\")) { out += to_string((int)month); i += 1; }\n";
    out << "        else if (has(\"dddd\")) { out += string(wd[weekday]); i += 4; } else if (has(\"ddd\")) { out += string(wd[weekday], 3); i += 3; }\n";
    out << "        else if (has(\"DD\")) { out += two(day); i += 2; } else if (has(\"D\")) { out += to_string((int)day); i += 1; }\n";
    out << "        else if (has(\"HH\")) { out += two(hour); i += 2; } else if (has(\"H\")) { out += to_string((int)hour); i += 1; }\n";
    out << "        else if (has(\"hh\")) { out += two(h12); i += 2; } else if (has(\"h\")) { out += to_string((int)h12); i += 1; }\n";
    out << "        else if (has(\"mm\")) { out += two(minute); i += 2; } else if (has(\"m\")) { out += to_string((int)minute); i += 1; }\n";
    out << "        else if (has(\"ss\")) { out += two(second); i += 2; } else if (has(\"s\")) { out += to_string((int)second); i += 1; }\n";
    out << "        else if (has(\"A\")) { out += string(hour < 12 ? \"AM\" : \"PM\"); i += 1; } else if (has(\"a\")) { out += string(hour < 12 ? \"am\" : \"pm\"); i += 1; }\n";
    out << "        else { out += string(p + i, 1); i++; }\n";
    out << "    }\n";
    out << "    return out;\n";
    out << "}\n";
    out << "}\n";
    out << "}\n";

    // Client-side route matcher + param parsers (only if a router block is used)
    bool any_router = false;
    for (const auto &comp : all_components)
    {
        if (comp.router) { any_router = true; break; }
    }
    if (any_router)
    {
        out << "namespace __coi_route {\n";
        out << "// \"/app/\" is \"/app\": a folder's page is served with the slash\n";
        out << "inline coi::string path(const coi::string& p) {\n";
        out << "    uint32_t n = p.length();\n";
        out << "    while (n > 1 && p.data()[n - 1] == '/') n--;\n";
        out << "    return n == p.length() ? p : coi::string(p.data(), n);\n";
        out << "}\n";
        out << "// Match `path` against `pattern`, capturing each ':' segment into caps[] in order.\n";
        out << "// Literal segments must match exactly and the segment counts must agree.\n";
        out << "inline bool match(const char* pp, uint32_t pl, const coi::string& path, coi::string* caps) {\n";
        out << "    const char* sp = path.data(); uint32_t sl = path.length();\n";
        out << "    uint32_t pi = 0, si = 0; int cap = 0;\n";
        out << "    while (true) {\n";
        out << "        while (pi < pl && pp[pi] == '/') pi++;\n";
        out << "        while (si < sl && sp[si] == '/') si++;\n";
        out << "        bool pend = pi >= pl, send = si >= sl;\n";
        out << "        if (pend || send) return pend && send;\n";
        out << "        uint32_t ps = pi; while (pi < pl && pp[pi] != '/') pi++;\n";
        out << "        uint32_t ss = si; while (si < sl && sp[si] != '/') si++;\n";
        out << "        uint32_t plen = pi - ps, slen = si - ss;\n";
        out << "        if (plen > 0 && pp[ps] == ':') { caps[cap++] = coi::string(sp + ss, slen); }\n";
        out << "        else {\n";
        out << "            if (plen != slen) return false;\n";
        out << "            for (uint32_t k = 0; k < plen; k++) if (pp[ps + k] != sp[ss + k]) return false;\n";
        out << "        }\n";
        out << "    }\n";
        out << "}\n";
        out << "// Parsers leave `ok` untouched on success and set it false on failure, so a\n";
        out << "// route's params can share one `ok` flag (any failure => the route doesn't match).\n";
        out << "inline coi::string to_string(const coi::string& s, bool&) { return s; }\n";
        out << "inline int to_int(const coi::string& s, bool& ok) {\n";
        out << "    const char* d = s.data(); uint32_t n = s.length();\n";
        out << "    if (n == 0) { ok = false; return 0; }\n";
        out << "    uint32_t i = 0; bool neg = (d[0] == '-'); if (neg) { i = 1; if (n == 1) { ok = false; return 0; } }\n";
        out << "    long v = 0;\n";
        out << "    for (; i < n; i++) { char c = d[i]; if (c < '0' || c > '9') { ok = false; return 0; } v = v * 10 + (c - '0'); }\n";
        out << "    return (int)(neg ? -v : v);\n";
        out << "}\n";
        out << "inline bool to_bool(const coi::string& s, bool& ok) {\n";
        out << "    if (s == \"true\") return true;\n";
        out << "    if (s == \"false\") return false;\n";
        out << "    ok = false; return false;\n";
        out << "}\n";
        out << "}\n\n";
    }

    // Sort components topologically so dependencies come first
    auto sorted_components = topological_sort_components(all_components);

    // Emit JSON runtime helpers inline if Json.parse is used
    if (features.json)
    {
        emit_json_runtime(out);
    }
    out << "\n";

    // Register all data types in the DataTypeRegistry for JSON codegen
    // Component-local types are prefixed with ComponentName_
    DataTypeRegistry::instance().clear();
    for (const auto &data_def : all_global_data)
    {
        DataTypeRegistry::instance().register_type(qualified_name(data_def->module_name, data_def->name), data_def->fields);
    }
    for (const auto &comp : all_components)
    {
        std::string prefix = qualified_name(comp.module_name, comp.name) + "_";
        std::set<std::string> local;
        for (const auto &data_def : comp.data)
            local.insert(data_def->name);
        for (const auto &data_def : comp.data)
        {
            // fields naming a sibling local pod get its C++ name
            std::vector<DataField> fields = data_def->fields;
            for (auto &f : fields)
            {
                bool arr = f.type.ends_with("[]");
                std::string base = arr ? f.type.substr(0, f.type.size() - 2) : f.type;
                if (local.count(base))
                    f.type = prefix + base + (arr ? "[]" : "");
            }
            DataTypeRegistry::instance().register_type(prefix + data_def->name, fields);
        }
    }

    // Populate global set of components with scoped CSS (for view.cc to conditionally emit scope attributes)
    extern std::set<std::string> g_components_with_scoped_css;
    g_components_with_scoped_css.clear();
    for (const auto &comp : all_components)
    {
        if (!comp.css.empty())
        {
            g_components_with_scoped_css.insert(qualified_name(comp.module_name, comp.name));
        }
    }

    // callbacks keyed by handle
    out << "struct DispatcherBase {\n";
    out << "    static inline DispatcherBase* g_first = nullptr;\n";
    out << "    DispatcherBase* next = nullptr;\n";
    out << "    void (*forget)(DispatcherBase*, const void*) = nullptr;\n";
    out << "    void (*forget_handle)(DispatcherBase*, webcc::handle) = nullptr;\n";
    out << "    void (*forget_range)(DispatcherBase*, int32_t, int32_t) = nullptr;\n";
    out << "};\n";
    out << "template<typename Callback, int MaxListeners = 512>\n";
    out << "struct Dispatcher : DispatcherBase {\n";
    out << "    int32_t handles[MaxListeners];\n";
    out << "    Callback callbacks[MaxListeners];\n";
    out << "    const void* owners[MaxListeners];\n";
    out << "    int count = 0;\n";
    // owner: the component whose method the callback calls into; its
    // _destroy() (coi_forget_owner) removes every entry it owns, so a late
    // event never calls into freed memory and dead entries don't pile up
    out << "    void set(webcc::handle h, Callback cb, const void* owner = nullptr) {\n";
    out << "        if (!forget) {\n";
    out << "            forget = [](DispatcherBase* b, const void* o) { static_cast<Dispatcher*>(b)->remove_owner(o); };\n";
    out << "            forget_handle = [](DispatcherBase* b, webcc::handle h) { static_cast<Dispatcher*>(b)->remove(h); };\n";
    out << "            forget_range = [](DispatcherBase* b, int32_t lo, int32_t hi) { static_cast<Dispatcher*>(b)->remove_range(lo, hi); };\n";
    out << "            next = g_first; g_first = this;\n";
    out << "        }\n";
    out << "        int32_t hid = (int32_t)h;\n";
    out << "        for (int i = 0; i < count; i++) {\n";
    out << "            if (handles[i] == hid) { callbacks[i] = cb; owners[i] = owner; return; }\n";
    out << "        }\n";
    out << "        if (count < MaxListeners) {\n";
    out << "            handles[count] = hid;\n";
    out << "            callbacks[count] = cb;\n";
    out << "            owners[count] = owner;\n";
    out << "            count++;\n";
    out << "        }\n";
    out << "    }\n";
    out << "    void remove_owner(const void* owner) {\n";
    out << "        for (int i = 0; i < count; ) {\n";
    out << "            if (owners[i] == owner) {\n";
    out << "                handles[i] = handles[count-1];\n";
    out << "                callbacks[i] = callbacks[count-1];\n";
    out << "                owners[i] = owners[count-1];\n";
    out << "                count--;\n";
    out << "            } else { i++; }\n";
    out << "        }\n";
    out << "    }\n";
    out << "    void remove(webcc::handle h) {\n";
    out << "        int32_t hid = (int32_t)h;\n";
    out << "        for (int i = 0; i < count; i++) {\n";
    out << "            if (handles[i] == hid) {\n";
    out << "                handles[i] = handles[count-1];\n";
    out << "                callbacks[i] = callbacks[count-1];\n";
    out << "                owners[i] = owners[count-1];\n";
    out << "                count--;\n";
    out << "                return;\n";
    out << "            }\n";
    out << "        }\n";
    out << "    }\n";
    // every handle in [lo, hi): what a removed loop item allocated
    out << "    void remove_range(int32_t lo, int32_t hi) {\n";
    out << "        for (int i = 0; i < count; ) {\n";
    out << "            if (handles[i] >= lo && handles[i] < hi) {\n";
    out << "                handles[i] = handles[count-1];\n";
    out << "                callbacks[i] = callbacks[count-1];\n";
    out << "                owners[i] = owners[count-1];\n";
    out << "                count--;\n";
    out << "            } else { i++; }\n";
    out << "        }\n";
    out << "    }\n";
    out << "    template<typename... Args>\n";
    out << "    bool dispatch(webcc::handle h, Args&&... args) {\n";
    out << "        int32_t hid = (int32_t)h;\n";
    out << "        for (int i = 0; i < count; i++) {\n";
    out << "            if (handles[i] == hid) { callbacks[i](args...); return true; }\n";
    out << "        }\n";
    out << "        return false;\n";
    out << "    }\n";
    // handlers may register while this runs, stop at the starting count
    out << "    template<typename... Args>\n";
    out << "    void dispatch_all(Args&&... args) {\n";
    out << "        int n = count;\n";
    out << "        for (int i = 0; i < n && i < count; i++) callbacks[i](args...);\n";
    out << "    }\n";
    out << "};\n";
    out << "template<typename E> Dispatcher<coi::function<void(const E&)>> coi_events;\n";
    out << "inline void coi_forget_owner(const void* owner) {\n";
    out << "    for (DispatcherBase* d = DispatcherBase::g_first; d; d = d->next) d->forget(d, owner);\n";
    out << "}\n";
    // a loop item's handles are consecutive (handles only count up): the item notes the span
    // under its root element, and forgetting the root forgets every handler inside it, nested
    // rows included, along with the spans of loops nested in it
    out << "struct CoiSpan { int32_t root; int32_t lo; int32_t hi; };\n";
    out << "inline coi::vector<CoiSpan> g_coi_spans;\n";
    out << "inline void coi_note_span(webcc::handle root, int32_t lo) {\n";
    out << "    g_coi_spans.push_back(CoiSpan{(int32_t)root, lo, webcc::deferred_handle_counter()});\n";
    out << "}\n";
    out << "inline void coi_forget_handle(webcc::handle h) {\n";
    out << "    for (DispatcherBase* d = DispatcherBase::g_first; d; d = d->next) d->forget_handle(d, h);\n";
    out << "    int32_t hid = (int32_t)h;\n";
    out << "    for (int i = 0; i < (int)g_coi_spans.size(); i++) {\n";
    out << "        if (g_coi_spans[i].root != hid) continue;\n";
    out << "        int32_t lo = g_coi_spans[i].lo, hi = g_coi_spans[i].hi;\n";
    out << "        for (DispatcherBase* d = DispatcherBase::g_first; d; d = d->next) d->forget_range(d, lo, hi);\n";
    out << "        for (int j = 0; j < (int)g_coi_spans.size(); ) {\n";
    out << "            if (g_coi_spans[j].root >= lo && g_coi_spans[j].root < hi) { g_coi_spans[j] = g_coi_spans[g_coi_spans.size() - 1]; g_coi_spans.pop_back(); }\n";
    out << "            else j++;\n";
    out << "        }\n";
    out << "        return;\n";
    out << "    }\n";
    out << "}\n\n";

    // a forgotten loop row (removed, or morphed into a fresh one) takes the child components it
    // made with it: destroyed in place, leaving the DOM the row's removal or morph owns. They keep
    // their slot in the vector (handlers hold their address); _destroy is idempotent after that
    out << "template<typename V> inline void coi_drop_row_children(V& v, webcc::handle row) {\n";
    out << "    for (const CoiSpan& s : g_coi_spans) {\n";
    out << "        if (s.root != (int32_t)row) continue;\n";
    out << "        for (auto& c : v) if ((int32_t)c._el[0] >= s.lo && (int32_t)c._el[0] < s.hi) c._destroy(true);\n";
    out << "        return;\n";
    out << "    }\n";
    out << "}\n";
    // a keyed loop row's key, comparable across syncs whatever its type
    out << "template<typename T> coi::string coi_loop_key(const T& v) { webcc::hybrid_formatter<128> f; f << v; return coi::string(f.c_str()); }\n";
    out << "int g_view_depth = 0;\n";

    // Emit feature-specific globals (dispatchers, callbacks, etc.)
    emit_feature_globals(out, features);
    out << "\n";

    g_used_events.clear();

    // Create compiler session for cross-component state
    CompilerSession session;

    // Populate component info for parent-child reactivity wiring
    for (auto *comp : sorted_components)
    {
        ComponentMemberInfo info;
        for (const auto &param : comp->params)
        {
            if (param->is_public && param->is_mutable)
            {
                info.pub_mut_members.insert(param->name);
            }
            info.param_names.push_back(param->name);
            if (param->is_reference)
                info.ref_params.insert(param->name);
        }
        // pub mut state has the same onXChange hook as a pub mut param
        for (const auto &var : comp->state)
            if (var->is_public && var->is_mutable)
                info.pub_mut_members.insert(var->name);
        session.component_info[qualified_name(comp->module_name, comp->name)] = info;
    }

    // Populate global data type names for module-level type resolution
    for (const auto &data_def : all_global_data)
    {
        session.data_type_names.insert(qualified_name(data_def->module_name, data_def->name));
    }

    // Output global enums (defined outside components)
    for (const auto &enum_def : all_global_enums)
    {
        out << enum_def->to_webcc();
    }
    if (!all_global_enums.empty())
    {
        out << "\n";
    }

    // Output component-local enums (flattened with ComponentName_ prefix)
    for (const auto &comp : all_components)
    {
        for (const auto &enum_def : comp.enums)
        {
            out << emit_coi_enum(qualified_name(comp.module_name, comp.name) + "_" + enum_def->name, enum_def->values);
        }
    }

    // Output global data types (defined outside components). A pod comes after the pods its
    // fields use; files are read in import order, so one can use a pod from a later file
    {
        std::map<std::string, const DataDef *> by_name;
        for (const auto &data_def : all_global_data)
            by_name[qualified_name(data_def->module_name, data_def->name)] = data_def.get();
        std::set<const DataDef *> done, visiting;
        std::function<void(const DataDef *)> emit = [&](const DataDef *d) {
            if (done.count(d) || visiting.count(d))
                return;
            visiting.insert(d);
            for (const auto &field : d->fields)
            {
                // every name in the type: Geo::Point[], Point[string], Pair<A, B>
                const std::string &t = field.type;
                for (size_t i = 0; i < t.size();)
                {
                    if (!std::isalpha((unsigned char)t[i]) && t[i] != '_') { i++; continue; }
                    size_t j = i;
                    while (j < t.size() && (std::isalnum((unsigned char)t[j]) || t[j] == '_' || (t[j] == ':' && j + 1 < t.size() && t[j + 1] == ':')))
                        j += t[j] == ':' ? 2 : 1;
                    std::string word = t.substr(i, j - i);
                    size_t dc = word.find("::");
                    std::string key = dc == std::string::npos ? qualified_name(d->module_name, word)
                                                              : qualified_name(word.substr(0, dc), word.substr(dc + 2));
                    auto it = by_name.find(key);
                    if (it != by_name.end() && it->second != d)
                        emit(it->second);
                    i = j;
                }
            }
            visiting.erase(d);
            done.insert(d);
            out << const_cast<DataDef *>(d)->to_webcc();
        };
        for (const auto &data_def : all_global_data)
            emit(data_def.get());
    }
    if (!all_global_data.empty())
    {
        out << "\n";
    }

    // Output component-local data types (flattened with ComponentName_ prefix)
    for (const auto &comp : all_components)
    {
        // Set up ComponentTypeContext so convert_type can resolve nested local types
        std::set<std::string> local_data_names;
        std::set<std::string> local_enum_names;
        for (const auto &d : comp.data)
        {
            local_data_names.insert(d->name);
        }
        for (const auto &e : comp.enums)
        {
            local_enum_names.insert(e->name);
        }
        ComponentTypeContext::instance().set(qualified_name(comp.module_name, comp.name), local_data_names, local_enum_names);

        for (const auto &data_def : comp.data)
        {
            out << "struct " << qualified_name(comp.module_name, comp.name) << "_" << data_def->name << " {\n";
            for (const auto &field : data_def->fields)
            {
                out << "    " << convert_type(field.type) << " " << cpp_name(field.name) << ";\n";
            }
            out << "};\n";
        }

        ComponentTypeContext::instance().clear();
    }
    out << "\n";

    {
        std::vector<JsonPod> pods;
        for (const auto &data_def : all_global_data)
            if (!data_def->source_file.empty())
                pods.push_back({qualified_name(data_def->module_name, data_def->name), data_def->type_params, data_def->fields});
        for (const auto &comp : all_components)
            for (const auto &data_def : comp.data)
                pods.push_back({qualified_name(comp.module_name, comp.name) + "_" + data_def->name, data_def->type_params, data_def->fields});
        emit_json_writer_runtime(out);
        out << generate_json_writers(pods) << "\n";
    }

    // Output field token constants for Meta.has(Type.field)
    if (features.json)
    {
        for (const auto &data_def : all_global_data)
        {
            if (data_def->source_file.empty())
                continue;
            out << generate_field_token_constants(qualified_name(data_def->module_name, data_def->name));
        }
        for (const auto &comp : all_components)
        {
            for (const auto &data_def : comp.data)
            {
                out << generate_field_token_constants(qualified_name(comp.module_name, comp.name) + "_" + data_def->name);
            }
        }
        out << "\n";
    }

    // Output Meta structs for JSON parsing (if Json.parse is used)
    if (features.json)
    {
        for (const auto &data_def : all_global_data)
        {
            if (data_def->source_file.empty())
                continue;
            out << generate_meta_struct(qualified_name(data_def->module_name, data_def->name));
        }
        for (const auto &comp : all_components)
        {
            for (const auto &data_def : comp.data)
            {
                // Use prefixed name for component-local types
                out << generate_meta_struct(qualified_name(comp.module_name, comp.name) + "_" + data_def->name);
            }
        }
        out << "\n";
    }

    // Forward declarations
    for (auto *comp : sorted_components)
    {
        out << "struct " << qualified_name(comp->module_name, comp->name) << ";\n";
    }
    out << "\n";

    // Forward declare global navigation functions (defined after components)
    out << "void g_app_navigate(const coi::string& route);\n";
    out << "coi::string g_app_get_route();\n\n";

    // Top-level functions are generated with no component active
    auto enter_function_scope = [&](const FunctionDef &func)
    {
        ComponentTypeContext::instance().clear();
        ComponentTypeContext::instance().set_module_scope(func.module_name, session.data_type_names);
        g_ref_props.clear();
    };

    // declarations first, generics are defined here too
    for (const auto &func : all_global_functions)
    {
        enter_function_scope(*func);
        out << (func->type_params.empty() ? func->free_declaration() : func->free_definition());
    }
    ComponentTypeContext::instance().clear();
    if (!all_global_functions.empty())
    {
        out << "\n";
    }

    for (auto *comp : sorted_components)
    {
        out << comp->to_webcc(session);
    }

    // non-generic definitions after components
    for (const auto &func : all_global_functions)
    {
        if (!func->type_params.empty()) continue;
        enter_function_scope(*func);
        out << func->free_definition() << "\n";
    }
    ComponentTypeContext::instance().clear();
    g_ref_props.clear();

    if (final_app_config.root_component.empty())
    {
        std::cerr << "Error: No root component defined. Use 'app { root = ComponentName }' to define the entry point." << std::endl;
        exit(1);
    }

    // Find root component and get its qualified name
    std::string root_qualified;
    for (const auto &comp : all_components)
    {
        if (comp.name == final_app_config.root_component)
        {
            root_qualified = qualified_name(comp.module_name, comp.name);
            break;
        }
    }
    if (root_qualified.empty())
    {
        std::cerr << "Error: Root component '" << final_app_config.root_component << "' not found." << std::endl;
        exit(1);
    }

    out << "\n"
        << root_qualified << "* app = nullptr;\n";

    if (features.router)
    {
        out << "void g_app_navigate(const coi::string& route) { if (app) app->navigate(route); }\n";
        out << "coi::string g_app_get_route() { return app ? app->_current_route : \"\"; }\n";
    }
    else
    {
        // Stub functions if no router - prevents linker errors
        out << "void g_app_navigate(const coi::string& route) {}\n";
        out << "coi::string g_app_get_route() { return \"\"; }\n";
    }

    out << "void dispatch_events(const webcc::Event* events, uint32_t event_count) {\n";
    out << "    for (uint32_t i = 0; i < event_count; i++) {\n";
    out << "        const auto& e = events[i];\n";
    out << "        if (false) {\n"; // Dummy to allow all handlers to use "} else if"
    emit_feature_event_handlers(out, features);
    emit_event_dispatch(out, features);
    out << "        }\n";
    out << "    }\n";
    out << "}\n\n";

    out << "void update_wrapper(double time) {\n";
    out << "    static double last_time = 0;\n";
    out << "    double dt = (time - last_time) / 1000.0;\n";
    out << "    last_time = time;\n";
    out << "    if (dt > 0.1) dt = 0.1; // Cap dt to avoid huge jumps\n";
    out << "    static webcc::Event events[64];\n";
    out << "    uint32_t count = 0;\n";
    out << "    webcc::Event e;\n";
    out << "    while (webcc::poll_event(e) && count < 64) {\n";
    out << "        events[count++] = e;\n";
    out << "    }\n";
    out << "    dispatch_events(events, count);\n";
    
    // Only call tick if the root component has a tick method
    if (session.components_with_tick.count(root_qualified))
    {
        out << "    if (app) app->tick(dt);\n";
    }
    if (features.router)
    {
        // Apply any route change requested during event dispatch or tick.
        // Deferring the swap here guarantees no route component method is on
        // the stack when its component gets destroyed (see emit_router.cc).
        out << "    if (app) app->_apply_route();\n";
    }
    out << "    webcc::flush();\n";
    out << "}\n\n";

    out << "int main() {\n";
    out << "    // We allocate the app on the heap because the stack is destroyed when main() returns.\n";
    out << "    // The app needs to persist for the event loop (update_wrapper).\n";
    out << "    // We use coi::malloc so backend allocation is abstracted per target.\n";
    out << "    void* app_mem = coi::malloc(sizeof(" << root_qualified << "));\n";
    out << "    app = new (app_mem) " << root_qualified << "();\n";
    emit_feature_init(out, features, root_qualified);
    out << "    app->_view();\n";
    // no rAF loop without a tick or with tick = demand
    bool has_tick = session.components_with_tick.count(root_qualified) > 0;
    if (!has_tick || final_app_config.tick_on_demand)
    {
        out << "    webcc::system::set_update(update_wrapper);\n";
        out << "    webcc::system::request_frame(); // first frame\n";
    }
    else
        out << "    webcc::system::set_main_loop(update_wrapper);\n";
    out << "    webcc::flush();\n";
    if (final_app_config.prerender)
    {
        // the page arrived prerendered: the app has drawn its own copy, the old one goes
        // in the same task, so no frame shows both or neither
        out << "    {\n";
        out << "        webcc::DOMElement pre = webcc::dom::get_element_by_id(\"coi-pre\");\n";
        out << "        if (pre.is_valid() && (int32_t)pre >= 0) webcc::dom::remove_element(pre);\n";
        out << "        webcc::flush();\n";
        out << "    }\n";
    }
    out << "    return 0;\n";
    out << "}\n";
}
