#include <functional>
#include "codegen.h"
#include <filesystem>
namespace fs = std::filesystem;
#include "codegen_utils.h"
#include "ast/ast.h"
#include "ast/codegen_state.h"
#include "../analysis/feature_detector.h"
#include "../analysis/dependency_resolver.h"
#include "json_codegen.h"
#include "cli/error.h"
#include <iostream>

// one case per webcc event with a bound callback
static void emit_event_dispatch(std::ostream &out, const FeatureFlags &features)
{
    for (const auto &[key, ev] : g_used_events)
    {
        // keyboard feature dispatches these itself, the router popstate
        if (features.keyboard && (key == "input::KEY_DOWN" || key == "input::KEY_UP"))
            continue;
        if (features.router && key == "system::POPSTATE")
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

// every event of a frame to its handlers
static void emit_dispatch_events(std::ostream &out, const FeatureFlags &features)
{
    out << "void dispatch_events(const webcc::Event* events, uint32_t event_count) {\n";
    out << "    for (uint32_t i = 0; i < event_count; i++) {\n";
    out << "        const auto& e = events[i];\n";
    out << "        if (false) {\n"; // Dummy to allow all handlers to use "} else if"
    emit_feature_event_handlers(out, features);
    emit_event_dispatch(out, features);
    out << "        }\n";
    out << "    }\n";
    out << "}\n\n";
}

// A module's pods name each other without the module (Login, not Account::Login), and any
// pod may name another module's as Geo::Point. Written out as C++ every name carries its
// module (Account_Login, Geo_Point); the structs, the JSON code and its registry all read
// the field types, so they are rewritten once here
static std::string qualify_pod_type(const std::string &t, const std::string &module, const std::set<std::string> &pods)
{
    auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::string out;
    for (size_t i = 0; i < t.size();)
    {
        if (!std::isalpha(static_cast<unsigned char>(t[i])) && t[i] != '_') { out += t[i++]; continue; }
        size_t j = i;
        while (j < t.size() && ident(t[j])) j++;
        std::string word = t.substr(i, j - i);
        if (j + 1 < t.size() && t[j] == ':' && t[j + 1] == ':')
        {
            size_t k = j + 2, m = k;
            while (m < t.size() && ident(t[m])) m++;
            std::string q = qualified_name(word, t.substr(k, m - k));
            out += pods.count(q) ? q : t.substr(i, m - i);
            i = m;
            continue;
        }
        out += !module.empty() && pods.count(qualified_name(module, word)) ? qualified_name(module, word) : word;
        i = j;
    }
    return out;
}

// what a test's expect() calls: one failure ends the test, printed as it happens under the
// test's name. Values print as JSON, which every Coi value can be written as. mount() puts a
// component on the host's page (webcc/host.h), cleared after each test
static void emit_test_runtime(std::ostream &out)
{
    out << R"(static const char* coi_test_current = "";
static bool coi_test_failed_now = false;
static const char* coi_test_where = "";
static int coi_test_line = 0;
static bool coi_test_color() { static int c = -1; if (c < 0) { const char* e = getenv("COI_COLOR"); c = e && *e == '1'; } return c == 1; }
template<typename T> static coi::string coi_test_show(const T& v) { return __coi_json_stringify(v); }
static void coi_test_fail(const char* file, int line, const char* expr, const coi::string& left, const coi::string& right) {
    if (coi_test_failed_now) return;
    coi_test_failed_now = true;
    printf("  %s✗%s %s\n", coi_test_color() ? "\033[31m" : "", coi_test_color() ? "\033[0m" : "", coi_test_current);
    printf("    %s:%d  expect(%s)\n", file, line, expr);
    if (left.length() || right.length()) {
        printf("      left   %.*s\n", (int)left.length(), left.data());
        printf("      right  %.*s\n", (int)right.length(), right.data());
    }
    printf("\n");
}
void dispatch_events(const webcc::Event* events, uint32_t event_count);
static void coi_test_page_fail(const char* action, const coi::string& selector) {
    if (coi_test_failed_now) return;
    coi_test_failed_now = true;
    printf("  %s✗%s %s\n", coi_test_color() ? "\033[31m" : "", coi_test_color() ? "\033[0m" : "", coi_test_current);
    printf("    %s:%d  %s(\"%.*s\"): nothing on the page matches\n", coi_test_where, coi_test_line, action, (int)selector.length(), selector.data());
    coi::string page = webcc::host::html(webcc::DOMElement(0));
    int shown = page.length() > 2000 ? 2000 : (int)page.length();
    printf("      page   %.*s%s\n\n", shown, page.data(), page.length() > 2000 ? " ..." : "");
}
static void coi_test_update() {
    static webcc::Event events[64];
    webcc::Event e;
    for (;;) {
        uint32_t count = 0;
        while (count < 64 && webcc::poll_event(e)) events[count++] = e;
        if (count == 0) break;
        dispatch_events(events, count);
        if (count < 64) break;
    }
    webcc::flush();
    coi_reap();
}
struct Mounted {
    webcc::DOMElement find(const char* action, const coi::string& selector) const {
        if (coi_test_failed_now) return webcc::DOMElement();
        webcc::DOMElement el = webcc::host::query(selector);
        if (!el.is_valid()) coi_test_page_fail(action, selector);
        return el;
    }
    void click(const coi::string& selector) const {
        webcc::DOMElement el = find("click", selector);
        if (el.is_valid()) { webcc::host::click(el); coi_test_update(); }
    }
    void fill(const coi::string& selector, const coi::string& text) const {
        webcc::DOMElement el = find("fill", selector);
        if (el.is_valid()) { webcc::host::input(el, text); coi_test_update(); }
    }
    void select(const coi::string& selector, const coi::string& value) const {
        webcc::DOMElement el = find("select", selector);
        if (el.is_valid()) { webcc::host::change(el, value); coi_test_update(); }
    }
    void check(const coi::string& selector, bool on) const {
        webcc::DOMElement el = find(on ? "check" : "uncheck", selector);
        if (el.is_valid()) { webcc::host::change(el, on ? "true" : "false"); coi_test_update(); }
    }
    void key(const coi::string& selector, int32_t code) const {
        webcc::DOMElement el = find("key", selector);
        if (el.is_valid()) { webcc::host::keydown(el, code); coi_test_update(); }
    }
    coi::string text(const coi::string& selector) const {
        webcc::DOMElement el = find("text", selector);
        return el.is_valid() ? webcc::host::text(el) : coi::string();
    }
    coi::string attr(const coi::string& selector, const coi::string& name) const {
        webcc::DOMElement el = find("attr", selector);
        return el.is_valid() ? webcc::dom::get_attribute(el, name) : coi::string();
    }
    bool has(const coi::string& selector) const { return webcc::host::query(selector).is_valid(); }
    int32_t count(const coi::string& selector) const { return webcc::host::count(selector); }
    coi::string html() const { return webcc::host::html(webcc::DOMElement(0)); }
};
struct CoiMounted { void* p; void (*drop)(void*); };
static webcc::vector<CoiMounted> coi_test_mounted;
template<typename T> static void coi_test_drop(void* p) { T* c = static_cast<T*>(p); c->_destroy(); c->~T(); webcc::free(c); }
template<typename T> static Mounted coi_test_mount(T* c) {
    c->_view();
    webcc::flush();
    coi_test_mounted.push_back(CoiMounted{c, &coi_test_drop<T>});
    return Mounted{};
}
static void coi_test_unmount() {
    for (size_t i = coi_test_mounted.size(); i-- > 0;) coi_test_mounted[i].drop(coi_test_mounted[i].p);
    coi_test_mounted.clear();
    coi_reap();
    webcc::host::clear();
}
)";
}

// runs every test in order; the last line is for the tool that started the program
static void emit_test_main(std::ostream &out, const std::vector<std::unique_ptr<FunctionDef>> &functions, const FeatureFlags &features)
{
    out << "void g_app_navigate(const coi::string& route) {}\n";
    out << "coi::string g_app_get_route() { return \"\"; }\n\n";
    emit_dispatch_events(out, features);
    out << "int main() {\n";
    out << "    struct CoiTest { const char* name; void (*run)(); };\n";
    out << "    static const CoiTest tests[] = {\n";
    size_t count = 0;
    for (const auto &func : functions)
    {
        if (func->test_name.empty()) continue;
        std::string shown = func->module_name.empty() ? func->test_name : func->module_name + ": " + func->test_name;
        out << "        {" << cpp_string_literal(shown) << ", " << cpp_name(qualified_name(func->module_name, func->name)) << "},\n";
        count++;
    }
    out << "    };\n";
    out << "    bool verbose = getenv(\"COI_TEST_VERBOSE\") != nullptr;\n";
    out << "    int passed = 0, failed = 0;\n";
    out << "    for (size_t i = 0; i < " << count << "; i++) {\n";
    out << "        coi_test_current = tests[i].name;\n";
    out << "        coi_test_failed_now = false;\n";
    out << "        tests[i].run();\n";
    out << "        coi_test_unmount();\n";
    out << "        if (coi_test_failed_now) failed++;\n";
    out << "        else { passed++; if (verbose) printf(\"  %s✓%s %s\\n\", coi_test_color() ? \"\\033[32m\" : \"\", coi_test_color() ? \"\\033[0m\" : \"\", tests[i].name); }\n";
    out << "    }\n";
    out << "    printf(\">> tests %d %d\\n\", passed, failed);\n";
    out << "    fflush(stdout);\n";
    out << "    return failed ? 1 : 0;\n";
    out << "}\n";
}

void generate_cpp_code(
    std::ostream &out,
    std::vector<Component> &all_components,
    const std::vector<std::unique_ptr<DataDef>> &all_global_data,
    const std::vector<std::unique_ptr<EnumDef>> &all_global_enums,
    const std::vector<std::unique_ptr<FunctionDef>> &all_global_functions,
    const AppConfig &final_app_config,
    const std::set<std::string> &required_headers,
    const FeatureFlags &features,
    bool test_program)
{
    // Include required headers
    for (const auto &header : required_headers)
    {
        out << "#include \"webcc/" << header << ".h\"\n";
    }
    if (test_program)   // built for the host: the C library is there, and the page to mount on
        out << "#include <cstdio>\n#include <cstdlib>\n#include \"webcc/dom.h\"\n#include \"webcc/host.h\"\n";
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
    // a component kept in an array: its handlers and its children's callbacks hold `this`, so it
    // lives at one address for life, and one taken out while an event is being handled stays in
    // memory until the frame is over, so the handler that took it out can finish (a route
    // component gets the same from _apply_route). The slots hold pointers; generated code reads
    // the container like a vector of the components themselves
    out << R"(struct CoiGrave { void* p; void (*bury)(void*); };
inline webcc::vector<CoiGrave> g_coi_graves;
inline void coi_reap() {
    while (!g_coi_graves.empty()) {   // a burial can dig more: a component with arrays of its own
        webcc::vector<CoiGrave> due(webcc::move(g_coi_graves));
        for (size_t i = 0; i < due.size(); i++) due[i].bury(due[i].p);
    }
}
namespace coi {
template<typename T> class components {
    webcc::vector<T*> slots;
    static void bury(void* p) { T* t = static_cast<T*>(p); t->~T(); webcc::free(t); }
    // out of the array already; its handlers, timers and DOM go now, its memory with the frame
    static void let_go(T* p, bool skip_dom) { p->_destroy(skip_dom); g_coi_graves.push_back(CoiGrave{p, &bury}); }
public:
    components() = default;
    template<typename... Args> components(T&& first, Args&&... rest) { push_back(webcc::move(first)); (push_back(webcc::move(rest)), ...); }
    // assigned from another array, as any array is: the components are copied as values
    components(const components& o) { slots.reserve(o.size()); for (size_t i = 0; i < o.size(); i++) push_back(T(*o.slots[i])); }
    components& operator=(const components& o) { if (this != &o) { components copy(o); *this = webcc::move(copy); } return *this; }
    components(components&& o) noexcept : slots(webcc::move(o.slots)) {}
    components& operator=(components&& o) noexcept { if (this != &o) { clear(); slots = webcc::move(o.slots); } return *this; }
    ~components() { clear(); }
    struct iterator {
        T** p;
        T& operator*() const { return **p; }
        T* operator->() const { return *p; }
        iterator& operator++() { ++p; return *this; }
        bool operator==(const iterator& o) const { return p == o.p; }
        bool operator!=(const iterator& o) const { return p != o.p; }
    };
    struct const_iterator {
        T* const* p;
        const T& operator*() const { return **p; }
        const T* operator->() const { return *p; }
        const_iterator& operator++() { ++p; return *this; }
        bool operator==(const const_iterator& o) const { return p == o.p; }
        bool operator!=(const const_iterator& o) const { return p != o.p; }
    };
    iterator begin() { return iterator{slots.data()}; }
    iterator end() { return iterator{slots.data() + slots.size()}; }
    const_iterator begin() const { return const_iterator{slots.data()}; }
    const_iterator end() const { return const_iterator{slots.data() + slots.size()}; }
    size_t size() const { return slots.size(); }
    bool empty() const { return slots.empty(); }
    T& operator[](size_t i) { return *slots[i]; }
    const T& operator[](size_t i) const { return *slots[i]; }
    T& back() { return *slots.back(); }
    const T& back() const { return *slots.back(); }
    T& front() { return *slots.front(); }
    const T& front() const { return *slots.front(); }
    void reserve(size_t n) { slots.reserve(n); }
    void push_back(T&& v) { T* p = static_cast<T*>(webcc::malloc(sizeof(T))); new (p) T(webcc::move(v)); slots.push_back(p); }
    // one already in an array (a loop variable): copied as a value, like assignment does
    void push_back(const T& v) { push_back(T(v)); }
    template<typename... Args> void emplace_back(Args&&... args) { T* p = static_cast<T*>(webcc::malloc(sizeof(T))); new (p) T(webcc::forward<Args>(args)...); slots.push_back(p); }
    void pop_back(bool skip_dom = false) { if (slots.empty()) return; T* p = slots.back(); slots.pop_back(); let_go(p, skip_dom); }
    void erase(size_t i, bool skip_dom = false) { if (i >= slots.size()) return; T* p = slots[i]; slots.erase(i); let_go(p, skip_dom); }
    void remove(size_t i) { erase(i); }
    void clear(bool skip_dom = false) { webcc::vector<T*> gone(webcc::move(slots)); for (size_t i = 0; i < gone.size(); i++) let_go(gone[i], skip_dom); }
};
}
)";
    // bytes point into the event buffer, copy them
        // an array method's inline function (b => ...) runs here, inlined into one loop
    out << R"(template<typename V, typename F> inline int32_t coi_index_where(const V& v, F pred) {
    for (int32_t i = 0; i < (int32_t)v.size(); i++) if (pred(v[i])) return i;
    return -1;
}
template<typename V, typename F> inline bool coi_any(const V& v, F pred) { return coi_index_where(v, pred) >= 0; }
template<typename V, typename F> inline bool coi_all(const V& v, F pred) {
    for (int32_t i = 0; i < (int32_t)v.size(); i++) if (!pred(v[i])) return false;
    return true;
}
template<typename V, typename F> inline int32_t coi_count_where(const V& v, F pred) {
    int32_t n = 0;
    for (int32_t i = 0; i < (int32_t)v.size(); i++) if (pred(v[i])) n++;
    return n;
}
template<typename V, typename F> inline void coi_remove_where(V& v, F pred) {
    for (int32_t i = 0; i < (int32_t)v.size();) { if (pred(v[i])) v.remove(i); else i++; }
}
template<typename V> inline void coi_append(V& v, const V& other) {
    if (&v == &other) { V copy = other; coi_append(v, copy); return; }
    v.reserve(v.size() + other.size());
    for (int32_t i = 0; i < (int32_t)other.size(); i++) v.push_back(other[i]);
}
template<typename V> inline V coi_slice(const V& v, int32_t start, int32_t count) {
    int32_t n = (int32_t)v.size();
    if (start < 0) start = 0;
    int32_t end = count < 0 ? n : (start + count > n ? n : start + count);
    V out;
    if (start < end) out.reserve(end - start);
    for (int32_t i = start; i < end; i++) out.push_back(v[i]);
    return out;
}
template<typename V, typename F> inline V coi_filter(const V& v, F pred) {
    V out;
    for (int32_t i = 0; i < (int32_t)v.size(); i++) if (pred(v[i])) out.push_back(v[i]);
    return out;
}
)";
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
    // Url.encode / Url.decode
    out << "namespace url {\n";
    out << "inline string encode(const string& s) {\n";
    out << "    static const char* hex = \"0123456789ABCDEF\"; string out;\n";
    out << "    for (uint32_t i = 0; i < s.length(); i++) { unsigned char c = (unsigned char)s.data()[i];\n";
    out << "        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\\'' || c == '(' || c == ')';\n";
    out << "        if (keep) { char k = (char)c; out += string(&k, 1); }\n";
    out << "        else { char e[3] = {'%', hex[c >> 4], hex[c & 15]}; out += string(e, 3); } }\n";
    out << "    return out;\n";
    out << "}\n";
    out << "inline int hex_value(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }\n";
    out << "inline string decode(const string& s) {\n";
    out << "    string out; uint32_t n = s.length(); const char* d = s.data();\n";
    out << "    for (uint32_t i = 0; i < n; i++) {\n";
    out << "        if (d[i] == '%' && i + 2 < n && hex_value(d[i + 1]) >= 0 && hex_value(d[i + 2]) >= 0) { char c = (char)(hex_value(d[i + 1]) * 16 + hex_value(d[i + 2])); out += string(&c, 1); i += 2; }\n";
    out << "        else out += string(d + i, 1); }\n";
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
        out << "// Literal segments must match exactly and the segment counts must agree, unless `rest`:\n";
        out << "// then the path may go on below the pattern (\"/app/*\")\n";
        out << "inline bool match(const char* pp, uint32_t pl, const coi::string& path, coi::string* caps, bool rest = false) {\n";
        out << "    const char* sp = path.data(); uint32_t sl = path.length();\n";
        out << "    uint32_t pi = 0, si = 0; int cap = 0;\n";
        out << "    while (true) {\n";
        out << "        while (pi < pl && pp[pi] == '/') pi++;\n";
        out << "        while (si < sl && sp[si] == '/') si++;\n";
        out << "        bool pend = pi >= pl, send = si >= sl;\n";
        out << "        if (pend && rest) return true;\n";
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

    {
        std::set<std::string> pods;
        for (const auto &data_def : all_global_data)
            pods.insert(qualified_name(data_def->module_name, data_def->name));
                for (const auto &data_def : all_global_data)
        {
            for (auto &field : data_def->fields)
                field.type = qualify_pod_type(field.type, data_def->module_name, pods);
            for (auto &alt : data_def->alts)
                alt.type = qualify_pod_type(alt.type, data_def->module_name, pods);
        }
    }

    // Register all data types in the DataTypeRegistry for JSON codegen
    // Component-local types are prefixed with ComponentName_
    DataTypeRegistry::instance().clear();
        for (const auto &data_def : all_global_data)
    {
        DataTypeRegistry::instance().register_type(qualified_name(data_def->module_name, data_def->name), data_def->fields);
        if (data_def->is_union)
            DataTypeRegistry::instance().register_union(qualified_name(data_def->module_name, data_def->name), data_def->tag, data_def->alts);
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
    // keys for page listeners: negative, stable across moves
    out << "inline int32_t g_coi_page_keys = 0;\n";
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
            if (param->is_reference && param->is_mutable)
                info.mut_ref_params.insert(param->name);
        }
        // pub mut state has the same onXChange hook as a pub mut param
        for (const auto &var : comp->state)
            if (var->is_public && var->is_mutable)
                info.pub_mut_members.insert(var->name);
        session.component_info[qualified_name(comp->module_name, comp->name)] = info;
    }

    // what each component's methods read of its pub mut members, for owners that call them
    g_component_method_reads.clear();
    for (auto *comp : sorted_components)
    {
        const auto &pub = session.component_info[qualified_name(comp->module_name, comp->name)].pub_mut_members;
        std::map<std::string, std::set<std::string>> direct, closed;
        comp->compute_method_reads(direct, closed);
        auto &out = g_component_method_reads[qualified_name(comp->module_name, comp->name)];
        for (const auto &[method, reads] : closed)
            for (const auto &r : reads)
                if (pub.count(r)) out[method].insert(r);
    }
    g_method_reads.clear();

    // Populate global data type names for module-level type resolution
    for (const auto &data_def : all_global_data)
    {
        session.data_type_names.insert(qualified_name(data_def->module_name, data_def->name));
    }
    // and module enums
    for (const auto &enum_def : all_global_enums)
    {
        session.data_type_names.insert(qualified_name(enum_def->module_name, enum_def->name));
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
    // fields use; files are read in import order, so one can use a pod from a later file.
    // The same order serves the Meta structs below, which hold each other the same way
    std::vector<const DataDef *> ordered_global_data;
    {
        std::map<std::string, const DataDef *> by_name;
        for (const auto &data_def : all_global_data)
            by_name[qualified_name(data_def->module_name, data_def->name)] = data_def.get();
        std::set<const DataDef *> done, visiting;
        std::function<void(const DataDef *)> visit = [&](const DataDef *d) {
            if (done.count(d) || visiting.count(d))
                return;
                        visiting.insert(d);
            // a union holds its alternatives by value: they come first, like field types
            std::vector<std::string> types;
            for (const auto &field : d->fields)
                types.push_back(field.type);
            for (const auto &alt : d->alts)
                types.push_back(alt.type);
            for (const std::string &t : types)
            {
                // every name in the type: Geo::Point[], Point[string], Pair<A, B>; field types
                // were qualified above (Geo_Point), so a word is tried as it is first
                for (size_t i = 0; i < t.size();)
                {
                    if (!std::isalpha((unsigned char)t[i]) && t[i] != '_') { i++; continue; }
                    size_t j = i;
                    while (j < t.size() && (std::isalnum((unsigned char)t[j]) || t[j] == '_' || (t[j] == ':' && j + 1 < t.size() && t[j + 1] == ':')))
                        j += t[j] == ':' ? 2 : 1;
                    std::string word = t.substr(i, j - i);
                    size_t dc = word.find("::");
                    auto it = by_name.find(word);
                    if (it == by_name.end())
                        it = by_name.find(dc == std::string::npos ? qualified_name(d->module_name, word)
                                                                  : qualified_name(word.substr(0, dc), word.substr(dc + 2)));
                    if (it != by_name.end() && it->second != d)
                        visit(it->second);
                    i = j;
                }
            }
            visiting.erase(d);
            done.insert(d);
            ordered_global_data.push_back(d);
        };
        for (const auto &data_def : all_global_data)
            visit(data_def.get());
        for (const DataDef *d : ordered_global_data)
            out << const_cast<DataDef *>(d)->to_webcc();
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
            std::string local = qualified_name(comp.module_name, comp.name) + "_" + data_def->name;
            out << "struct " << local << " {\n";
            for (const auto &field : data_def->fields)
            {
                out << "    " << convert_type(field.type) << " " << cpp_name(field.name) << ";\n";
            }
            out << "    bool operator==(const " << local << "&) const = default;\n";
            out << "};\n";
        }

        ComponentTypeContext::instance().clear();
    }
    out << "\n";

    {
                std::vector<JsonPod> pods;
        for (const auto &data_def : all_global_data)
            if (!data_def->source_file.empty())
                pods.push_back({qualified_name(data_def->module_name, data_def->name), data_def->type_params, data_def->fields, data_def->is_union, data_def->alts});
        for (const auto &comp : all_components)
            for (const auto &data_def : comp.data)
                pods.push_back({qualified_name(comp.module_name, comp.name) + "_" + data_def->name, data_def->type_params, data_def->fields, false, {}});
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
        for (const DataDef *data_def : ordered_global_data)
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
        ErrorHandler::current_file = func.source_file;   // an error here names its file
        // a test's failures name its file as the project sees it
        g_test_file = func.test_name.empty() ? "" : fs::relative(func.source_file, fs::current_path()).string();
    };

    // every function declared first, so a generic body can call any function in any order;
    // then the generic definitions, which have to come before the components that use them
    for (const auto &func : all_global_functions)
    {
        enter_function_scope(*func);
        out << func->free_declaration();
    }
    for (const auto &func : all_global_functions)
    {
        if (func->type_params.empty()) continue;
        enter_function_scope(*func);
        out << func->free_definition();
    }
    ComponentTypeContext::instance().clear();
    g_test_file.clear();
    if (!all_global_functions.empty())
    {
        out << "\n";
    }
    if (test_program)
        emit_test_runtime(out);

    for (auto *comp : sorted_components)
    {
        ErrorHandler::current_file = comp->source_file;
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
    g_test_file.clear();
    ErrorHandler::current_file.clear();

    if (test_program)
    {
        emit_test_main(out, all_global_functions, features);
        return;
    }

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
        // at build time a page is the page at its path: a redirect in a mount (to a login, say) waits for the browser
        out << "void g_app_navigate(const coi::string& route) { if (app && !webcc::system::is_prerender()) app->navigate(route); }\n";
        out << "coi::string g_app_get_route() { return app ? app->_current_route : \"\"; }\n";
    }
    else
    {
        // Stub functions if no router - prevents linker errors
        out << "void g_app_navigate(const coi::string& route) {}\n";
        out << "coi::string g_app_get_route() { return \"\"; }\n";
    }

    emit_dispatch_events(out, features);

    out << "void update_wrapper(double time) {\n";
    out << "    static double last_time = 0;\n";
    out << "    double dt = (time - last_time) / 1000.0;\n";
    out << "    last_time = time;\n";
    out << "    if (dt > 0.1) dt = 0.1; // Cap dt to avoid huge jumps\n";
    // every queued event, 64 at a time: one left behind would wait for the next input on an
    // app that draws on demand (and the poll that finds a 65th must not throw it away)
    out << "    static webcc::Event events[64];\n";
    out << "    webcc::Event e;\n";
    out << "    for (;;) {\n";
    out << "        uint32_t count = 0;\n";
    out << "        while (count < 64 && webcc::poll_event(e)) events[count++] = e;\n";
    out << "        if (count == 0) break;\n";
    out << "        dispatch_events(events, count);\n";
    out << "        if (count < 64) break;\n";
    out << "    }\n";
    
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
    // components taken out of arrays during this frame: no handler of theirs is on the stack now
    out << "    coi_reap();\n";
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
        // the page arrived prerendered: its elements stay and take on the first render
        // (handlers, handles, whatever differs now), the fresh copy was never shown
        out << "    webcc::dom::hydrate(app->_get_root_element());\n";
        out << "    webcc::flush();\n";
    }
    out << "    return 0;\n";
    out << "}\n";
}
