#include "mcp.h"
#include "cli.h"
#include "package_manager.h"
#include "version.h"
#include "json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace
{

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// JSON-RPC errors
struct MethodNotFound : std::runtime_error { using std::runtime_error::runtime_error; };
struct InvalidParams : std::runtime_error { using std::runtime_error::runtime_error; };

// a tool's answer: text for the model, and whether it reports a failure
struct ToolResult
{
    std::string text;
    bool error = false;
};

std::string lower(std::string s)
{
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string read_file(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string strip_ansi(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[')
        {
            i += 2;
            while (i < s.size() && !std::isalpha((unsigned char)s[i])) i++;
            continue;
        }
        out += s[i];
    }
    return out;
}

std::string sh_quote(const std::string &s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

// runs a shell command with stdin closed (ours is the client's), output and errors together
int run(const std::string &cmd, std::string &out)
{
    out.clear();
    FILE *p = popen((cmd + " < /dev/null 2>&1").c_str(), "r");
    if (!p) return -1;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, got);
    int status = pclose(p);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

fs::path coi_binary() { return get_executable_dir() / "coi"; }

int current_pond()
{
    try { return std::stoi(COI_POND_NUMBER); } catch (...) { return 0; }
}

int current_drop()
{
    try
    {
        int d = std::stoi(GIT_COMMIT_COUNT) - std::stoi(COI_POND_START_COMMIT_COUNT);
        return d < 0 ? 0 : d;
    }
    catch (...) { return 0; }
}

bool is_project(const fs::path &dir)
{
    std::error_code ec;
    return fs::exists(dir / "src" / "App.coi", ec) || fs::exists(dir / "Mod.coi", ec) || fs::exists(dir / "coi.lock", ec);
}

// the project a tool works in: the one named, else the working directory or the nearest one above it
fs::path project_dir(const Json &args)
{
    std::string named = args.str("project");
    if (!named.empty()) return fs::absolute(named).lexically_normal();
    fs::path cwd = fs::current_path();
    for (fs::path d = cwd; !d.empty(); d = d.parent_path())
    {
        if (is_project(d)) return d;
        if (d == d.parent_path()) break;
    }
    return cwd;
}

std::string shown_path(const fs::path &p, const fs::path &base)
{
    fs::path rel = p.lexically_relative(base);
    if (rel.empty() || *rel.begin() == "..") return p.string();
    return rel.string();
}

// every .coi file of a project: src/ and a package's Mod.coi, tests included
std::vector<std::string> project_files(const fs::path &dir)
{
    std::vector<std::string> files;
    std::error_code ec;
    if (fs::exists(dir / "Mod.coi", ec)) files.push_back((dir / "Mod.coi").string());
    if (fs::is_directory(dir / "src", ec))
    {
        std::vector<std::string> src;
        for (auto it = fs::recursive_directory_iterator(dir / "src", ec); it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) break;
            if (it->is_regular_file() && it->path().extension() == ".coi") src.push_back(it->path().string());
        }
        std::sort(src.begin(), src.end());
        files.insert(files.end(), src.begin(), src.end());
    }
    return files;
}

// the files a tool was given (relative to the project), or the whole project
bool resolve_files(const Json &args, const fs::path &dir, std::vector<std::string> &files, std::string &problem)
{
    const Json *given = args.get("files");
    if (given && given->kind == Json::Arr && !given->arr.empty())
    {
        for (const auto &f : given->arr)
        {
            fs::path p = f.s;
            if (p.is_relative()) p = dir / p;
            std::error_code ec;
            if (!fs::is_regular_file(p, ec))
            {
                problem = "No such file: " + p.string();
                return false;
            }
            files.push_back(p.lexically_normal().string());
        }
        return true;
    }
    files = project_files(dir);
    if (files.empty())
    {
        problem = "No Coi project in " + dir.string() +
                  " (no src/ with .coi files, no Mod.coi). Pass \"project\" (the folder with src/App.coi), \"files\", or \"code\".";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// compiler errors, made readable for a model
// ---------------------------------------------------------------------------

// habits from JavaScript and TypeScript, and what Coi has instead
const std::vector<std::pair<std::string, std::string>> HINTS = {
    {"Unknown type 'let'", "Coi has no let/const/var: declare with the type, `int count = 0;`, and `mut int count = 0;` for one that changes."},
    {"Unknown type 'const'", "Coi has no let/const/var: a variable without `mut` is already read-only: `string name = \"x\";`."},
    {"Unknown type 'var'", "Coi has no let/const/var: declare with the type, `int count = 0;`."},
    {"Unknown type 'auto'", "Coi has no `auto`: write the type, `int count = 0;`."},
    {"Unknown type 'number'", "Coi numbers are `int` and `float` (sized ones too: int64, float64, uint8...)."},
    {"Unknown type 'boolean'", "The boolean type is `bool`."},
    {"Unknown type 'function'", "Functions are declared `def name(int a) : int { ... }`."},
    {"Unknown type 'async'", "Coi has no async/await or promises: platform calls take callbacks (see docs \"FetchRequest\")."},
    {"Unknown type 'await'", "Coi has no async/await or promises: platform calls take callbacks (see docs \"FetchRequest\")."},
    {"Unknown type 'export'", "Visibility is `pub` (pub component, pub def), not export."},
    {"'null'", "Coi has no null or undefined: every type has a default value (0, \"\", false, empty)."},
    {"'undefined'", "Coi has no null or undefined: every type has a default value (0, \"\", false, empty)."},
};

// "Error: <message> at line N in <file>" as "file:N: message", the line's code below it
std::string format_compiler_output(const std::string &raw, const fs::path &base,
                                   const std::string &snippet_path, int &error_count)
{
    std::ostringstream out;
    error_count = 0;
    for (std::string line : split_lines(strip_ansi(raw)))
    {
        if (trim(line).empty() || line.rfind("[DefSchema]", 0) == 0 || line.rfind(">> ", 0) == 0) continue;
        size_t err = line.find("Error:");
        if (err == std::string::npos)
        {
            out << line << "\n";
            continue;
        }
        error_count++;
        std::string msg = trim(line.substr(err + 6));
        std::string file;
        int at = 0;
        if (size_t in = msg.rfind(" in /"); in != std::string::npos)
        {
            file = msg.substr(in + 4);
            msg = msg.substr(0, in);
        }
        if (size_t l = msg.rfind(" at line "); l != std::string::npos)
        {
            std::string num = msg.substr(l + 9);
            if (!num.empty() && std::all_of(num.begin(), num.end(), ::isdigit))
            {
                at = std::stoi(num);
                msg = msg.substr(0, l);
            }
        }
        std::string where = file.empty() ? "" : (file == snippet_path ? "code" : shown_path(file, base));
        if (!snippet_path.empty())
            for (size_t p; (p = msg.find(snippet_path)) != std::string::npos;) msg.replace(p, snippet_path.size(), "code");
        if (!where.empty()) out << where << (at > 0 ? ":" + std::to_string(at) : "") << ": ";
        else if (at > 0) out << "line " << at << ": ";
        out << "error: " << msg << "\n";
        if (!file.empty() && at > 0)
        {
            auto lines = split_lines(read_file(file));
            if (at <= (int)lines.size())
            {
                std::string code = lines[at - 1];
                if (code.size() > 200) code = code.substr(0, 200) + "...";
                out << "  " << at << " | " << code << "\n";
            }
        }
        for (const auto &[needle, hint] : HINTS)
            if (msg.find(needle) != std::string::npos)
            {
                out << "  hint: " << hint << "\n";
                break;
            }
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// check
// ---------------------------------------------------------------------------

ToolResult tool_check(const Json &args)
{
    fs::path dir = project_dir(args);
    std::vector<std::string> files;
    std::string snippet_path;
    fs::path tmp;

    std::string code = args.str("code");
    if (!code.empty())
    {
        std::string tmpl = (fs::temp_directory_path() / "coi-mcp-XXXXXX").string();
        if (!mkdtemp(tmpl.data())) return {"Could not create a temporary directory for the code.", true};
        tmp = tmpl;
        snippet_path = (tmp / "Code.coi").string();
        std::ofstream(snippet_path) << code;
        files.push_back(snippet_path);
    }
    else
    {
        std::string problem;
        if (!resolve_files(args, dir, files, problem)) return {problem, true};
    }

    std::string cmd = "cd " + sh_quote(dir.string()) + " && " + sh_quote(coi_binary().string()) + " --check";
    for (const auto &f : files) cmd += " " + sh_quote(f);
    std::string raw;
    int code_exit = run(cmd, raw);
    int errors = 0;
    std::string report = format_compiler_output(raw, dir, snippet_path, errors);   // reads the lines it names
    if (!tmp.empty())
    {
        std::error_code ec;
        fs::remove_all(tmp, ec);
    }
    std::string what = !code.empty() ? "the code" : files.size() == 1 ? shown_path(files[0], dir) : std::to_string(files.size()) + " files";
    if (code_exit == 0)
        return {"No errors in " + what + (!code.empty() ? "." : files.size() == 1 ? " (and what it imports)." : " (and what they import).") +
                (report.empty() ? "" : "\n\n" + report)};
    if (code_exit > 128)
        return {"The compiler crashed (signal " + std::to_string(code_exit - 128) + ") checking " + what +
                    ". That is a compiler bug, not a mistake in the code.\n" + report, true};
    if (errors == 0)
        return {"The check of " + what + " failed:\n" + report, true};
    return {report + "\nThe compiler stops at the first error: fix it and check again.", true};
}

// ---------------------------------------------------------------------------
// lookup: the platform and builtin APIs, from the compiler's own .d.coi files
// ---------------------------------------------------------------------------

struct DefMember
{
    std::string name;
    std::string text;   // comments, then the declaration
};

struct DefType
{
    std::string name;     // "array" for array<T>
    std::string header;   // type Canvas extends DOMElement
    std::string parent;
    std::string file;
    std::vector<std::string> notes;   // comments and @tags before it
    std::vector<DefMember> members;
};

// "@inline("...") def f(): int" -> "def f(): int"
std::string strip_annotations(std::string s)
{
    s = trim(s);
    while (!s.empty() && s[0] == '@')
    {
        size_t i = 1;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
        if (i < s.size() && s[i] == '(')
        {
            int depth = 0;
            bool in_str = false;
            for (; i < s.size(); i++)
            {
                char c = s[i];
                if (in_str)
                {
                    if (c == '\\') i++;
                    else if (c == '"') in_str = false;
                    continue;
                }
                if (c == '"') in_str = true;
                else if (c == '(') depth++;
                else if (c == ')' && --depth == 0) { i++; break; }
            }
        }
        s = trim(s.substr(i));
    }
    return s;
}

int paren_balance(const std::string &s)
{
    int depth = 0;
    for (char c : s)
    {
        if (c == '(') depth++;
        else if (c == ')') depth--;
    }
    return depth;
}

std::string member_name(const std::string &decl)
{
    size_t def = decl.find("def ");
    if (def != std::string::npos)
    {
        size_t a = def + 4, b = a;
        while (b < decl.size() && (std::isalnum((unsigned char)decl[b]) || decl[b] == '_')) b++;
        return decl.substr(a, b - a);
    }
    // a field or a choice: the last word before ; or ,
    std::string s = decl;
    while (!s.empty() && (s.back() == ';' || s.back() == ',' || s.back() == ' ')) s.pop_back();
    size_t b = s.size(), a = b;
    while (a > 0 && (std::isalnum((unsigned char)s[a - 1]) || s[a - 1] == '_')) a--;
    return s.substr(a, b - a);
}

std::vector<DefType> &def_types()
{
    static std::vector<DefType> types;
    static bool loaded = false;
    if (loaded) return types;
    loaded = true;

    fs::path defs = get_executable_dir() / "defs";
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(defs, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec) break;
        const fs::path &p = it->path();
        // index.d.coi only repeats the others
        if (p.filename() != "index.d.coi" && p.string().ends_with(".d.coi") && p.string().find("/.cache/") == std::string::npos)
            files.push_back(p);
    }
    std::sort(files.begin(), files.end());

    for (const auto &path : files)
    {
        auto lines = split_lines(read_file(path));
        std::vector<std::string> notes, member_notes;
        DefType *cur = nullptr;
        for (size_t i = 0; i < lines.size(); i++)
        {
            std::string t = trim(lines[i]);
            if (!cur)
            {
                if (t.empty()) { notes.clear(); continue; }
                if (t.rfind("//", 0) == 0)
                {
                    if (t.find("=====") == std::string::npos && t.find("GENERATED FILE") == std::string::npos &&
                        t.find("Maps to:") == std::string::npos && t.find("Coi definitions for") == std::string::npos)
                        notes.push_back(t);
                    continue;
                }
                if (t[0] == '@')
                {
                    // what matters to the code using the type: can it be copied, is it a pod, enum or flags
                    for (const char *tag : {"@nocopy", "@pod", "@enum", "@flags"})
                        if (t.rfind(tag, 0) == 0) notes.push_back(tag);
                    continue;
                }
                if (t.rfind("type ", 0) != 0) { notes.clear(); continue; }

                std::string header = trim(t.substr(0, t.find('{')));
                std::string rest = trim(header.substr(5));
                size_t e = 0;
                while (e < rest.size() && (std::isalnum((unsigned char)rest[e]) || rest[e] == '_')) e++;
                std::string name = rest.substr(0, e);
                std::string parent;
                if (size_t ext = rest.find(" extends "); ext != std::string::npos) parent = trim(rest.substr(ext + 9));

                DefType *type = nullptr;
                for (auto &existing : types)   // array<T> and array<T, N> stay apart
                    if (existing.header == header) type = &existing;
                if (!type)
                {
                    types.push_back({name, header, parent, shown_path(path, get_executable_dir()), notes, {}});
                    type = &types.back();
                }
                notes.clear();
                bool one_line = t.find('{') != std::string::npos && t.find('}') != std::string::npos;
                cur = one_line ? nullptr : type;
                member_notes.clear();
                continue;
            }

            if (t == "}") { cur = nullptr; continue; }
            if (t.empty()) { member_notes.clear(); continue; }
            if (t.rfind("//", 0) == 0) { member_notes.push_back(t); continue; }
            std::string decl = strip_annotations(t);
            if (decl.empty()) continue;   // an annotation on its own line
            // a declaration over several lines
            while (paren_balance(decl) > 0 && i + 1 < lines.size())
            {
                std::string next = trim(lines[++i]);
                decl += (decl.back() == '(' ? "" : " ") + next;
            }
            DefMember m{member_name(decl), ""};
            for (const auto &n : member_notes) m.text += "    " + n + "\n";
            m.text += "    " + decl + "\n";
            cur->members.push_back(m);
            member_notes.clear();
        }
    }
    return types;
}

DefType *find_type(const std::string &name)
{
    for (auto &t : def_types())
        if (lower(t.name) == lower(name)) return &t;
    return nullptr;
}

std::vector<DefType *> find_types(const std::string &name)
{
    std::vector<DefType *> found;
    for (auto &t : def_types())
        if (lower(t.name) == lower(name)) found.push_back(&t);
    return found;
}

std::string render_type(const DefType &t)
{
    std::string out = "// " + t.file + "\n";
    for (const auto &n : t.notes) out += n + "\n";
    out += t.header + " {\n";
    for (const auto &m : t.members) out += m.text;
    out += "}\n";
    if (!t.parent.empty()) out += "// also has every member of " + t.parent + " (lookup \"" + t.parent + "\")\n";
    return out;
}

ToolResult tool_docs(const Json &args);

ToolResult tool_lookup(const Json &args)
{
    std::string q = trim(args.str("query"));
    if (q.empty()) return {"Give a query: a type (\"Canvas\", \"string\"), a member (\"string.split\", \"System.setTimeout\"), or a word (\"clipboard\").", true};
    if (q.size() > 2 && q.compare(q.size() - 2, 2, "()") == 0) q = q.substr(0, q.size() - 2);
    if (size_t lt = q.find('<'); lt != std::string::npos && q.find('.') == std::string::npos) q = q.substr(0, lt);
    if (q.size() > 2 && q.compare(q.size() - 2, 2, "[]") == 0) q = "array";
    for (size_t p; (p = q.find("::")) != std::string::npos;) q.replace(p, 2, ".");

    std::string type_part, member_part;
    if (size_t dot = q.find('.'); dot != std::string::npos)
    {
        type_part = q.substr(0, dot);
        member_part = lower(q.substr(dot + 1));
        if (type_part.size() > 2 && type_part.ends_with("[]")) type_part = "array";
    }

    // Type.member: that type and the ones it extends
    if (!type_part.empty())
    {
        auto types = find_types(type_part);
        if (types.empty()) return {"No type named '" + type_part + "'. Try lookup \"" + type_part + "\" alone to search for it.", true};
        std::string out, names;
        int found = 0;
        for (DefType *t : types)
            for (DefType *cur = t; cur; cur = cur->parent.empty() ? nullptr : find_type(cur->parent))
            {
                std::string part;
                for (const auto &m : cur->members)
                {
                    if (lower(m.name).find(member_part) != std::string::npos) { part += m.text; found++; }
                    if (cur == t && names.find(m.name + ",") == std::string::npos) names += m.name + ", ";
                }
                if (!part.empty()) out += cur->header + " {   // " + cur->file + "\n" + part + "}\n";
            }
        if (found == 0)
        {
            if (names.size() > 2) names.resize(names.size() - 2);
            return {types[0]->name + " has no member like '" + member_part + "'. Its members: " + names +
                        (types[0]->parent.empty() ? "" : " (and " + types[0]->parent + "'s)") + ".\nDo not guess an API: Coi has only what the definitions declare.", true};
        }
        return {out};
    }

    std::string lq = lower(q);
    std::string out;

    // a type by name: all of it
    for (DefType *t : find_types(q)) out += (out.empty() ? "" : "\n") + render_type(*t);

    // a word: the members and types whose names have it
    int members = 0;
    std::vector<std::string> type_names;
    std::string found;
    for (const auto &t : def_types())
    {
        if (lower(t.name) == lq) continue;   // shown whole above
        if (lower(t.name).find(lq) != std::string::npos) type_names.push_back(t.name);
        std::string part;
        for (const auto &m : t.members)
            if (lower(m.name).find(lq) != std::string::npos && members < 40) { part += m.text; members++; }
        if (!part.empty()) found += t.header + " {   // " + t.file + "\n" + part + "}\n";
    }
    if (!type_names.empty())
    {
        std::string list;
        for (const auto &n : type_names) list += (list.empty() ? "" : ", ") + n;
        found = "Types: " + list + " (lookup one by name for all of it)\n\n" + found;
    }
    if (!found.empty()) out += (out.empty() ? "" : "\nMembers named like '" + q + "' elsewhere:\n\n") + found;
    if (!out.empty()) return {out};

    // no API: maybe the guide has it (a keyword, a view element, a pattern)
    Json docs_args = Json::object({{"topic", Json(q)}});
    ToolResult docs = tool_docs(docs_args);
    if (!docs.error)
        return {"No API named '" + q + "' in the definitions. From the guide:\n\n" + docs.text};
    return {"Nothing named '" + q + "' in the definitions or the guide. Do not guess an API: Coi has only what the definitions declare. "
            "lookup a type (string, array, Math, System, Canvas, DOMElement, FetchRequest, Storage, Json...) to see its members.", true};
}

// ---------------------------------------------------------------------------
// docs: the guide (llms-full.txt) by section
// ---------------------------------------------------------------------------

struct Section
{
    std::string heading;
    int level;
    size_t start, end, block_end;   // lines; block_end includes the subsections
};

std::vector<std::string> &guide_lines()
{
    static std::vector<std::string> lines = split_lines(read_file(get_executable_dir() / "llms-full.txt"));
    return lines;
}

std::vector<Section> guide_sections()
{
    const auto &lines = guide_lines();
    std::vector<Section> sections;
    bool in_code = false;
    for (size_t i = 0; i < lines.size(); i++)
    {
        if (lines[i].rfind("```", 0) == 0) in_code = !in_code;
        if (in_code) continue;
        int level = lines[i].rfind("### ", 0) == 0 ? 3 : lines[i].rfind("## ", 0) == 0 ? 2 : 0;
        if (!level) continue;
        if (!sections.empty()) sections.back().end = i;
        sections.push_back({lines[i].substr(level + 1), level, i, lines.size(), lines.size()});
    }
    for (size_t k = 0; k < sections.size(); k++)
    {
        sections[k].block_end = sections[k].end;
        for (size_t m = k + 1; m < sections.size() && sections[m].level > sections[k].level; m++)
            sections[k].block_end = sections[m].end;
    }
    return sections;
}

std::string guide_text(size_t from, size_t to)
{
    const auto &lines = guide_lines();
    std::string out;
    for (size_t i = from; i < to && i < lines.size(); i++) out += lines[i] + "\n";
    while (out.size() > 1 && out.ends_with("\n\n")) out.pop_back();
    if (out.ends_with("---\n")) out.resize(out.size() - 4);
    return out;
}

int count_of(const std::string &hay, const std::string &needle)
{
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) n++;
    return n;
}

ToolResult tool_docs(const Json &args)
{
    if (guide_lines().empty()) return {"The guide (llms-full.txt) is missing next to the compiler: rebuild or reinstall Coi.", true};
    auto sections = guide_sections();
    std::string topic = trim(args.str("topic"));
    std::string lt = lower(topic);

    if (topic.empty())
    {
        std::string out = "The Coi guide's sections (docs with a topic returns one; \"all\" returns the whole guide, about 12k tokens):\n\n";
        for (const auto &s : sections) out += std::string(s.level == 3 ? "    " : "") + s.heading + "\n";
        return {out};
    }
    if (lt == "all" || lt == "full" || lt == "everything") return {guide_text(0, guide_lines().size())};

    // a heading that names the topic: that section with its subsections
    for (const auto &s : sections)
    {
        std::string h = lower(s.heading);
        // "4. Components" or "Components"
        if (h == lt || (h.find(". ") != std::string::npos && h.substr(h.find(". ") + 2) == lt))
            return {guide_text(s.start, s.block_end)};
    }

    std::vector<std::string> words;
    {
        std::string w;
        for (char c : lt + " ")
        {
            if (std::isalnum((unsigned char)c) || c == '_' || c == '<' || c == '>' || c == '@') w += c;
            else
            {
                if (w.size() >= 2) words.push_back(w);
                w.clear();
            }
        }
    }
    if (words.empty()) return {"No section about '" + topic + "'. docs with no topic lists them.", true};

    std::vector<std::pair<int, size_t>> scored;
    for (size_t k = 0; k < sections.size(); k++)
    {
        const auto &s = sections[k];
        std::string h = lower(s.heading), body = lower(guide_text(s.start + 1, s.end));
        if (trim(body).empty()) continue;   // a heading with only subsections
        int score = h.find(lt) != std::string::npos ? 30 : 0;
        for (const auto &w : words)
        {
            if (h.find(w) != std::string::npos) score += 10;
            score += std::min(count_of(body, w), 8);
        }
        if (score > 0) scored.push_back({score, k});
    }
    if (scored.empty()) return {"No section about '" + topic + "'. docs with no topic lists them.", true};
    std::stable_sort(scored.begin(), scored.end(), [](auto &a, auto &b) { return a.first > b.first; });

    std::string out;
    for (size_t r = 0; r < scored.size() && r < 3; r++)
    {
        if (r > 0 && scored[r].first * 3 < scored[0].first) break;   // much weaker than the best
        const auto &s = sections[scored[r].second];
        std::string text = guide_text(s.start, s.end);
        if (!out.empty() && out.size() + text.size() > 12000) break;
        out += (out.empty() ? "" : "\n") + text;
    }
    return {out};
}

// ---------------------------------------------------------------------------
// search_packages: the registry, downloaded once
// ---------------------------------------------------------------------------

const char *REGISTRY_TARBALL = "https://codeload.github.com/coi-lang/registry/tar.gz/refs/heads/main";

bool registry_packages(std::vector<Json> &packages, std::string &problem)
{
    static std::vector<Json> cached;
    static bool fetched = false;
    if (fetched)
    {
        packages = cached;
        return true;
    }
    std::string tmpl = (fs::temp_directory_path() / "coi-mcp-XXXXXX").string();
    if (!mkdtemp(tmpl.data()))
    {
        problem = "Could not create a temporary directory.";
        return false;
    }
    fs::path tmp = tmpl;
    std::string tgz = sh_quote((tmp / "registry.tar.gz").string());
    std::string out;
    int code = run("curl -sfL --max-time 30 -o " + tgz + " " + sh_quote(REGISTRY_TARBALL), out);
    std::string json;
    if (code == 0)
        // GNU tar needs --wildcards, bsdtar matches patterns without it
        code = run("tar -xzOf " + tgz + " --wildcards '*/packages/*.json' 2>/dev/null || tar -xzOf " + tgz + " '*/packages/*.json'", json);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    if (code != 0)
    {
        problem = "Could not download the package registry (" + std::string(REGISTRY_TARBALL) + "). Is the machine online?";
        return false;
    }
    // the package files, one after another
    for (const Json &v : parse_json_all(json))
        if (v.kind == Json::Obj && !v.str("name").empty()) cached.push_back(v);
    fetched = true;
    packages = cached;
    return true;
}

ToolResult tool_search_packages(const Json &args)
{
    std::vector<Json> packages;
    std::string problem;
    if (!registry_packages(packages, problem)) return {problem, true};

    fs::path dir = project_dir(args);
    std::map<std::string, LockEntry> installed;
    std::error_code ec;
    if (fs::exists(dir / "coi.lock", ec)) installed = read_lock_file(dir / "coi.lock");

    std::string q = lower(trim(args.str("query")));
    std::vector<std::string> words;
    {
        std::istringstream in(q);
        for (std::string w; in >> w;) words.push_back(w);
    }

    std::vector<std::pair<int, const Json *>> hits;
    for (const auto &pkg : packages)
    {
        std::string name = lower(pkg.str("name")), desc = lower(pkg.str("description"));
        std::vector<std::string> keywords;
        if (const Json *k = pkg.get("keywords"); k && k->kind == Json::Arr)
            for (const auto &kw : k->arr) keywords.push_back(lower(kw.s));
        int score = words.empty() ? 1 : 0;
        for (const auto &w : words)
        {
            if (name.find(w) != std::string::npos) score += 10;
            for (const auto &kw : keywords)
                if (kw == w) score += 6;
                else if (kw.find(w) != std::string::npos) score += 3;
            if (desc.find(w) != std::string::npos) score += 2;
        }
        if (score > 0) hits.push_back({score, &pkg});
    }
    std::stable_sort(hits.begin(), hits.end(), [](auto &a, auto &b) { return a.first > b.first; });

    if (hits.empty())
    {
        std::string names;
        for (const auto &pkg : packages) names += (names.empty() ? "" : ", ") + pkg.str("name");
        return {"No package matches '" + q + "'. The registry has " + std::to_string(packages.size()) + ": " + names + "."};
    }

    int pond = current_pond(), drop = current_drop();
    std::string out = std::to_string(hits.size()) + (hits.size() == 1 ? " package" : " packages") +
                      " (this compiler: pond " + std::to_string(pond) + ", drop " + std::to_string(drop) + ")\n";
    for (const auto &[score, pkg] : hits)
    {
        std::string name = pkg->str("name");
        // the newest release this compiler can use, as coi add picks it
        std::string version, newest;
        int needs_drop = 0;
        if (const Json *rel = pkg->get("releases"); rel && rel->kind == Json::Arr)
            for (const auto &r : rel->arr)
            {
                if (newest.empty()) newest = r.str("version");
                const Json *c = r.get("compiler");
                int rpond = c && c->get("pond") ? (int)c->get("pond")->n : 0;
                int rdrop = c && c->get("min-drop") ? (int)c->get("min-drop")->n : 0;
                if (rpond != pond) continue;
                if (drop > 0 && rdrop > drop)
                {
                    if (!needs_drop) needs_drop = rdrop;
                    continue;
                }
                version = r.str("version");
                break;
            }
        out += "\n" + name + " " + (version.empty() ? newest : version) + ": " + pkg->str("description") + "\n";
        if (const Json *k = pkg->get("keywords"); k && k->kind == Json::Arr && !k->arr.empty())
        {
            std::string kws;
            for (const auto &kw : k->arr) kws += (kws.empty() ? "" : ", ") + kw.s;
            out += "  keywords: " + kws + "\n";
        }
        if (!pkg->str("repository").empty()) out += "  source: " + pkg->str("repository") + "\n";
        if (version.empty())
            out += needs_drop ? "  needs a newer compiler (drop " + std::to_string(needs_drop) + "): coi self-upgrade first\n"
                              : "  no release for this compiler's pond\n";
        auto inst = installed.find(name);
        if (inst != installed.end())
            out += "  installed here: " + inst->second.version + "\n";
        else
            out += "  add: coi add " + name + "\n";
        out += "  use: import \"@" + name + "\";   (its API: .coi/pkgs/" + name + "/Mod.coi and README.md once added)\n";
    }
    return {out};
}

// ---------------------------------------------------------------------------
// test
// ---------------------------------------------------------------------------

ToolResult tool_test(const Json &args)
{
    fs::path dir = project_dir(args);
    std::vector<std::string> files;
    std::string problem;
    if (!resolve_files(args, dir, files, problem)) return {problem, true};

    std::string cmd = "cd " + sh_quote(dir.string()) + " && " + sh_quote(coi_binary().string()) + " --test";
    for (const auto &f : files) cmd += " " + sh_quote(f);
    cmd += " --out " + sh_quote((dir / ".coi" / "cache" / "test").string()) + " --progress";
    std::string raw;
    int code = run(cmd, raw);

    int passed = -1, failed = 0;
    for (const auto &line : split_lines(raw))
        if (line.rfind(">> tests ", 0) == 0) std::sscanf(line.c_str(), ">> tests %d %d", &passed, &failed);

    int errors = 0;
    std::string report = format_compiler_output(raw, dir, "", errors);
    if (passed < 0)
    {
        if (code > 128) return {"The test program crashed (signal " + std::to_string(code - 128) + ").\n" + report, true};
        return {"The tests did not build:\n" + report + (errors ? "\nFix the error and run the tests again." : ""), true};
    }
    if (passed == 0 && failed == 0)
        return {"No tests in " + (files.size() == 1 ? shown_path(files[0], dir) : std::to_string(files.size()) + " files") + ". A test: test \"what it checks\" { expect(...); } at the top level of any file (docs \"Tests\")."};
    if (failed == 0) return {std::to_string(passed) + (passed == 1 ? " test" : " tests") + " passed."};
    return {std::to_string(failed) + " of " + std::to_string(passed + failed) + " tests failed:\n\n" + report, true};
}

// ---------------------------------------------------------------------------
// the protocol
// ---------------------------------------------------------------------------

const char *INSTRUCTIONS =
    "Coi is a statically-typed, component-based language for reactive web apps that compile to "
    "WebAssembly (.coi files). It looks like JSX but is NOT JavaScript or TypeScript: declarations are "
    "typed (`int count = 0;`, `mut` for one that changes), there is no let/const/var, null, async/await, "
    "promises, npm or JS array callbacks, and no API exists unless Coi declares it.\n"
    "- Before writing Coi you don't know well, read the guide: docs with no topic lists its sections, "
    "docs \"all\" returns the whole guide (Golden Rules and Common Mistakes are the most useful parts).\n"
    "- Before calling a platform or builtin API, lookup it (\"string\", \"string.split\", \"Canvas\", "
    "\"System.setTimeout\"). Never guess method names.\n"
    "- After writing or editing .coi files, run check and fix every error before you finish; "
    "check also takes a standalone snippet as code. The compiler reports one error at a time.\n"
    "- When the project has test blocks, run test after changes.\n"
    "- Before building something library-sized, search_packages.";

Json tool_def(const char *name, const char *title, const char *description, const std::string &schema, bool read_only, bool open_world)
{
    return Json::object({
        {"name", name},
        {"title", title},
        {"description", description},
        {"inputSchema", parse_json(schema)},
        {"annotations", Json::object({{"readOnlyHint", read_only}, {"destructiveHint", false}, {"idempotentHint", true}, {"openWorldHint", open_world}})},
    });
}

Json tool_list()
{
    const char *project = R"("project": {"type": "string", "description": "The project folder (with src/App.coi, or a package's Mod.coi). Default: the working directory or the nearest project above it."})";
    return Json::array({
        tool_def("check", "Check Coi code",
                 "Compile-check Coi code with this compiler, writing nothing: parse, type and code generation errors as file:line with the line's code. "
                 "With no arguments, checks every .coi file of the project (tests included). With files, checks those and what they import. "
                 "With code, checks a standalone snippet (no app block needed; @scope/name package imports resolve in the project). "
                 "The compiler stops at the first error, so check again after each fix.",
                 (std::string(R"({"type": "object", "properties": {)") + project + R"(,
                    "files": {"type": "array", "items": {"type": "string"}, "description": "Files to check, relative to the project."},
                    "code": {"type": "string", "description": "Coi source to check on its own, instead of files."}}})"),
                 true, false),
        tool_def("lookup", "Look up a Coi API",
                 "The exact declarations of Coi's builtin and platform APIs, from this compiler's definition files: "
                 "a type (\"string\", \"array\", \"Math\", \"System\", \"Canvas\", \"DOMElement\", \"FetchRequest\", \"Storage\", \"Json\"), "
                 "a member (\"string.split\", \"Canvas.getContext2d\"), or a word to search member names (\"clipboard\", \"timeout\"). "
                 "Falls back to the guide for language features. `shared def` is called on the type (System.log(...)), `def` on a value.",
                 R"({"type": "object", "properties": {"query": {"type": "string", "description": "Type, Type.member, or a word."}}, "required": ["query"]})",
                 true, false),
        tool_def("docs", "Read the Coi guide",
                 "The Coi language guide written for AI assistants, by section: syntax, components, view syntax, styling, routing, "
                 "platform APIs, modules and packages, tests, complete examples, common mistakes. No topic lists the sections; "
                 "a topic (\"view syntax\", \"routing\", \"pods\", \"common mistakes\", \"examples\") returns the best matching sections; \"all\" returns the whole guide.",
                 R"({"type": "object", "properties": {"topic": {"type": "string", "description": "A section name or a few words."}}})",
                 true, false),
        tool_def("search_packages", "Search Coi packages",
                 "Search the Coi package registry by name, keyword or description (no query lists every package). "
                 "Shows the release this compiler can use, whether the project has it, and how to add and import it.",
                 (std::string(R"({"type": "object", "properties": {"query": {"type": "string", "description": "Words to search for."}, )") + project + "}}"),
                 true, true),
        tool_def("test", "Run Coi tests",
                 "Build the project's test blocks (test \"name\" { expect(...); }) into a native program and run them: "
                 "about a second. Reports failures with file:line, the condition, and both sides of a failed comparison. "
                 "Writes only to the project's .coi/cache/test.",
                 (std::string(R"({"type": "object", "properties": {)") + project + R"(,
                    "files": {"type": "array", "items": {"type": "string"}, "description": "Files whose tests to run, relative to the project. Default: all of src/."}}})"),
                 false, false),
    });
}

const char *GUIDE_URI = "coi://guide";

Json handle(const std::string &method, const Json &params)
{
    if (method == "initialize")
    {
        std::string asked = params.str("protocolVersion");
        std::string version = "2025-06-18";
        for (const char *known : {"2025-06-18", "2025-03-26", "2024-11-05"})
            if (asked == known) version = asked;
        return Json::object({
            {"protocolVersion", version},
            {"capabilities", Json::object({{"tools", Json::object()}, {"resources", Json::object()}})},
            {"serverInfo", Json::object({{"name", "coi"}, {"title", "Coi"}, {"version", "pond " + std::to_string(current_pond()) + " drop " + std::to_string(current_drop())}})},
            {"instructions", INSTRUCTIONS},
        });
    }
    if (method == "ping") return Json::object();
    if (method == "tools/list") return Json::object({{"tools", tool_list()}});
    if (method == "tools/call")
    {
        std::string name = params.str("name");
        Json args = Json::object();
        if (const Json *a = params.get("arguments"); a && a->kind == Json::Obj) args = *a;
        ToolResult r;
        if (name == "check") r = tool_check(args);
        else if (name == "lookup") r = tool_lookup(args);
        else if (name == "docs") r = tool_docs(args);
        else if (name == "search_packages") r = tool_search_packages(args);
        else if (name == "test") r = tool_test(args);
        else throw InvalidParams("Unknown tool: " + name);
        return Json::object({
            {"content", Json::array({Json::object({{"type", "text"}, {"text", r.text}})})},
            {"isError", r.error},
        });
    }
    if (method == "resources/list")
        return Json::object({{"resources", Json::array({Json::object({
                                               {"uri", GUIDE_URI},
                                               {"name", "coi-guide"},
                                               {"title", "Coi guide for AI assistants"},
                                               {"description", "The whole Coi language and API reference with examples and common mistakes (llms-full.txt)."},
                                               {"mimeType", "text/markdown"},
                                           })})}});
    if (method == "resources/read")
    {
        if (params.str("uri") != GUIDE_URI) throw InvalidParams("Unknown resource: " + params.str("uri"));
        return Json::object({{"contents", Json::array({Json::object({
                                              {"uri", GUIDE_URI},
                                              {"mimeType", "text/markdown"},
                                              {"text", guide_text(0, guide_lines().size())},
                                          })})}});
    }
    if (method == "resources/templates/list") return Json::object({{"resourceTemplates", Json::array()}});
    throw MethodNotFound(method);
}

void send(const Json &msg)
{
    std::cout << dump(msg) << "\n" << std::flush;
}

Json error_response(const Json &id, int code, const std::string &message)
{
    return Json::object({{"jsonrpc", "2.0"}, {"id", id}, {"error", Json::object({{"code", code}, {"message", message}})}});
}

} // namespace

int mcp_command()
{
    for (std::string line; std::getline(std::cin, line);)
    {
        if (trim(line).empty()) continue;
        Json msg;
        try
        {
            msg = parse_json(line);
        }
        catch (const std::exception &e)
        {
            send(error_response(Json(), -32700, std::string("Parse error: ") + e.what()));
            continue;
        }
        const Json *id = msg.get("id");
        std::string method = msg.str("method");
        if (!id) continue;   // a notification (initialized, cancelled): nothing to answer
        if (method.empty()) continue;   // a response to us; we send no requests
        Json params = Json::object();
        if (const Json *p = msg.get("params"); p && p->kind == Json::Obj) params = *p;
        try
        {
            send(Json::object({{"jsonrpc", "2.0"}, {"id", *id}, {"result", handle(method, params)}}));
        }
        catch (const MethodNotFound &)
        {
            send(error_response(*id, -32601, "Method not found: " + method));
        }
        catch (const InvalidParams &e)
        {
            send(error_response(*id, -32602, e.what()));
        }
        catch (const std::exception &e)
        {
            send(error_response(*id, -32603, e.what()));
        }
    }
    return 0;
}
