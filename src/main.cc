#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "schema_loader.h"
#include "type_checker.h"
#include "coi_schema.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <functional>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <queue>
#include <algorithm>

// =========================================================
// INCLUDE DETECTION
// =========================================================

// Build type-to-header mapping from schema (handle types -> namespace)
static std::map<std::string, std::string> build_type_to_header() {
    std::map<std::string, std::string> result;
    for (size_t i = 0; i < coi::SCHEMA_COUNT; ++i) {
        const auto& entry = coi::SCHEMA[i];
        // Map return type (if it's a handle) to namespace
        if (!entry.return_type.empty()) {
            result[entry.return_type] = entry.ns;
        }
        // Map parameter types (if they're handles) to namespace
        for (const auto& param : entry.params) {
            // Check if it's a handle type (not a primitive)
            if (param.type != "string" && param.type != "int32" && param.type != "uint32" && 
                param.type != "uint8" && param.type != "float32" && param.type != "float64" &&
                param.type != "bool" && param.type != "func_ptr") {
                result[param.type] = entry.ns;
            }
        }
    }
    return result;
}

// Extract base type from array types (e.g., "Audio[]" -> "Audio")
static std::string get_base_type(const std::string& type) {
    size_t bracket = type.find('[');
    if (bracket != std::string::npos) {
        return type.substr(0, bracket);
    }
    return type;
}

// Collect all types used in a component
static void collect_used_types(const Component& comp, std::set<std::string>& types) {
    // Collect from state variables
    for (const auto& var : comp.state) {
        types.insert(get_base_type(var->type));
    }
    // Collect from parameters
    for (const auto& param : comp.params) {
        types.insert(get_base_type(param->type));
    }
    // Collect from method parameters and return types
    for (const auto& method : comp.methods) {
        types.insert(get_base_type(method.return_type));
        for (const auto& param : method.params) {
            types.insert(get_base_type(param.type));
        }
    }
}

// Determine which headers are needed based on used types
static std::set<std::string> get_required_headers(const std::vector<Component>& components) {
    static auto type_to_header = build_type_to_header();
    
    std::set<std::string> used_types;
    for (const auto& comp : components) {
        collect_used_types(comp, used_types);
    }
    
    std::set<std::string> headers;
    // Always include dom and system (needed for basic DOM operations and main loop)
    headers.insert("dom");
    headers.insert("system");
    
    for (const auto& type : used_types) {
        auto it = type_to_header.find(type);
        if (it != type_to_header.end()) {
            headers.insert(it->second);
        }
    }
    
    return headers;
}

// =========================================================
// MAIN COMPILER
// =========================================================

// Collect child component names from a node
void collect_component_deps(ASTNode *node, std::set<std::string> &deps)
{
    if (!node)
        return;
    if (auto *comp_inst = dynamic_cast<ComponentInstantiation *>(node))
    {
        deps.insert(comp_inst->component_name);
    }
    else if (auto *el = dynamic_cast<HTMLElement *>(node))
    {
        for (const auto &child : el->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewIf = dynamic_cast<ViewIfStatement *>(node))
    {
        for (const auto &child : viewIf->then_children)
        {
            collect_component_deps(child.get(), deps);
        }
        for (const auto &child : viewIf->else_children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewFor = dynamic_cast<ViewForRangeStatement *>(node))
    {
        for (const auto &child : viewFor->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
    else if (auto *viewForEach = dynamic_cast<ViewForEachStatement *>(node))
    {
        for (const auto &child : viewForEach->children)
        {
            collect_component_deps(child.get(), deps);
        }
    }
}

// Topologically sort components so dependencies come first
std::vector<Component *> topological_sort_components(std::vector<Component> &components)
{
    std::map<std::string, Component *> comp_map;
    std::map<std::string, std::set<std::string>> dependencies;
    std::map<std::string, int> in_degree;

    for (auto &comp : components)
    {
        comp_map[comp.name] = &comp;
        in_degree[comp.name] = 0;
    }

    // Build dependency graph
    for (auto &comp : components)
    {
        std::set<std::string> deps;
        for (const auto &root : comp.render_roots)
        {
            collect_component_deps(root.get(), deps);
        }
        dependencies[comp.name] = deps;
    }

    // Calculate in-degrees
    for (auto &[name, deps] : dependencies)
    {
        for (auto &dep : deps)
        {
            if (comp_map.count(dep))
            {
                in_degree[name]++;
            }
        }
    }

    // Kahn's algorithm
    std::queue<std::string> queue;
    for (auto &[name, degree] : in_degree)
    {
        if (degree == 0)
        {
            queue.push(name);
        }
    }

    std::vector<Component *> sorted;
    while (!queue.empty())
    {
        std::string curr = queue.front();
        queue.pop();
        sorted.push_back(comp_map[curr]);

        // For each component that depends on curr, decrease in-degree
        for (auto &[name, deps] : dependencies)
        {
            if (deps.count(curr))
            {
                in_degree[name]--;
                if (in_degree[name] == 0)
                {
                    queue.push(name);
                }
            }
        }
    }

    // Check for cycles
    if (sorted.size() != components.size())
    {
        throw std::runtime_error("Circular dependency detected among components");
    }

    return sorted;
}

int main(int argc, char **argv)
{
    // Initialize SchemaLoader with embedded schema
    SchemaLoader::instance().init();

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <input.coi> [--cc-only] [--keep-cc] [--out <dir> | -o <dir>]" << std::endl;
        return 1;
    }

    std::string input_file;
    std::string output_dir;
    bool cc_only = false;
    bool keep_cc = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--cc-only")
            cc_only = true;
        else if (arg == "--keep-cc")
            keep_cc = true;
        else if (arg == "--out" || arg == "-o")
        {
            if (i + 1 < argc)
            {
                output_dir = argv[++i];
            }
            else
            {
                std::cerr << "Error: --out requires an argument" << std::endl;
                return 1;
            }
        }
        else if (input_file.empty())
            input_file = arg;
        else
        {
            std::cerr << "Unknown argument or multiple input files: " << arg << std::endl;
            return 1;
        }
    }

    if (input_file.empty())
    {
        std::cerr << "No input file specified." << std::endl;
        return 1;
    }

    std::vector<Component> all_components;
    std::vector<std::unique_ptr<EnumDef>> all_global_enums;
    AppConfig final_app_config;
    std::set<std::string> processed_files;
    std::queue<std::string> file_queue;

    namespace fs = std::filesystem;
    try
    {
        file_queue.push(fs::canonical(input_file).string());
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error resolving input file path: " << e.what() << std::endl;
        return 1;
    }

    try
    {
        while (!file_queue.empty())
        {
            std::string current_file_path = file_queue.front();
            file_queue.pop();

            if (processed_files.count(current_file_path))
                continue;
            processed_files.insert(current_file_path);

            std::cerr << "Processing " << current_file_path << "..." << std::endl;

            std::ifstream file(current_file_path);
            if (!file)
            {
                std::cerr << "Error: Could not open file " << current_file_path << std::endl;
                return 1;
            }
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // Lexical analysis
            Lexer lexer(source);
            auto tokens = lexer.tokenize();

            // Parsing
            Parser parser(tokens);
            parser.parse_file();

            all_components.insert(all_components.end(), std::make_move_iterator(parser.components.begin()), std::make_move_iterator(parser.components.end()));
            
            // Collect global enums
            for (auto& enum_def : parser.global_enums) {
                all_global_enums.push_back(std::move(enum_def));
            }

            if (!parser.app_config.root_component.empty())
            {
                final_app_config = parser.app_config;
            }

            fs::path current_path(current_file_path);
            fs::path parent_path = current_path.parent_path();

            for (const auto &import_path_str : parser.imports)
            {
                fs::path import_path = parent_path / import_path_str;
                try
                {
                    std::string abs_path = fs::canonical(import_path).string();
                    if (processed_files.find(abs_path) == processed_files.end())
                    {
                        file_queue.push(abs_path);
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error resolving import path " << import_path_str << ": " << e.what() << std::endl;
                    return 1;
                }
            }
        }

        std::cerr << "All files processed. Total components: " << all_components.size() << std::endl;

        validate_view_hierarchy(all_components);
        validate_mutability(all_components);
        validate_types(all_components, all_global_enums);

        // Determine output filename
        namespace fs = std::filesystem;
        fs::path input_path(input_file);
        fs::path output_path;

        if (!output_dir.empty())
        {
            fs::path out_dir_path(output_dir);
            try
            {
                fs::create_directories(out_dir_path);
            }
            catch (const fs::filesystem_error &e)
            {
                std::cerr << "Error: Could not create output directory " << output_dir << ": " << e.what() << std::endl;
                return 1;
            }

            output_path = out_dir_path / input_path.stem();
            output_path += ".cc";
        }
        else
        {
            output_path = input_path;
            output_path.replace_extension(".cc");
        }

        std::string output_cc = output_path.string();

        std::ofstream out(output_cc);
        if (!out)
        {
            std::cerr << "Error: Could not open output file " << output_cc << std::endl;
            return 1;
        }

        // Code generation - automatically detect required headers
        std::set<std::string> required_headers = get_required_headers(all_components);
        for (const auto& header : required_headers) {
            out << "#include \"webcc/" << header << ".h\"\n";
        }
        out << "#include \"webcc/core/function.h\"\n";
        out << "#include \"webcc/core/allocator.h\"\n";
        out << "#include \"webcc/core/new.h\"\n";
        out << "#include \"webcc/core/vector.h\"\n";
        out << "#include \"webcc/core/unordered_map.h\"\n\n";

        // Generic event dispatcher template
        out << "template<typename Callback, int MaxListeners = 64>\n";
        out << "struct Dispatcher {\n";
        out << "    int32_t handles[MaxListeners];\n";
        out << "    Callback callbacks[MaxListeners];\n";
        out << "    int count = 0;\n";
        out << "    void set(webcc::handle h, Callback cb) {\n";
        out << "        int32_t hid = (int32_t)h;\n";
        out << "        for (int i = 0; i < count; i++) {\n";
        out << "            if (handles[i] == hid) { callbacks[i] = cb; return; }\n";
        out << "        }\n";
        out << "        if (count < MaxListeners) {\n";
        out << "            handles[count] = hid;\n";
        out << "            callbacks[count] = cb;\n";
        out << "            count++;\n";
        out << "        }\n";
        out << "    }\n";
        out << "    void remove(webcc::handle h) {\n";
        out << "        int32_t hid = (int32_t)h;\n";
        out << "        for (int i = 0; i < count; i++) {\n";
        out << "            if (handles[i] == hid) {\n";
        out << "                handles[i] = handles[count-1];\n";
        out << "                callbacks[i] = callbacks[count-1];\n";
        out << "                count--;\n";
        out << "                return;\n";
        out << "            }\n";
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
        out << "};\n\n";
        out << "Dispatcher<webcc::function<void()>, 128> g_dispatcher;\n";
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_input_dispatcher;\n";
        out << "Dispatcher<webcc::function<void(const webcc::string&)>> g_change_dispatcher;\n";
        out << "Dispatcher<webcc::function<void(int)>> g_keydown_dispatcher;\n\n";

        // Sort components topologically so dependencies come first
        auto sorted_components = topological_sort_components(all_components);

        // Create compiler session for cross-component state
        CompilerSession session;

        // Output global enums (defined outside components)
        for (const auto& enum_def : all_global_enums) {
            out << enum_def->to_webcc();
        }
        if (!all_global_enums.empty()) {
            out << "\n";
        }
        
        // Forward declarations
        for (auto *comp : sorted_components)
        {
            out << "class " << comp->name << ";\n";
        }
        out << "\n";

        for (auto *comp : sorted_components)
        {
            out << comp->to_webcc(session);
        }

        if (final_app_config.root_component.empty())
        {
            std::cerr << "Error: No root component defined. Use 'app { root = ComponentName }' to define the entry point." << std::endl;
            return 1;
        }

        out << "\n"
            << final_app_config.root_component << "* app = nullptr;\n";
        out << "void dispatch_events(const webcc::Event* events, uint32_t event_count) {\n";
        out << "    for (uint32_t i = 0; i < event_count; i++) {\n";
        out << "        const auto& e = events[i];\n";
        out << "        if (e.opcode == webcc::dom::ClickEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::ClickEvent>()) g_dispatcher.dispatch(evt->handle);\n";
        out << "        } else if (e.opcode == webcc::dom::InputEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::InputEvent>()) g_input_dispatcher.dispatch(evt->handle, webcc::string(evt->value));\n";
        out << "        } else if (e.opcode == webcc::dom::ChangeEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::ChangeEvent>()) g_change_dispatcher.dispatch(evt->handle, webcc::string(evt->value));\n";
        out << "        } else if (e.opcode == webcc::dom::KeydownEvent::OPCODE) {\n";
        out << "            if (auto evt = e.as<webcc::dom::KeydownEvent>()) g_keydown_dispatcher.dispatch(evt->handle, evt->keycode);\n";
        out << "        }\n";
        out << "    }\n";
        out << "}\n\n";
        out << "void update_wrapper(float time) {\n";
        out << "    static float last_time = 0;\n";
        out << "    float dt = (time - last_time) / 1000.0f;\n";
        out << "    last_time = time;\n";
        out << "    if (dt > 0.1f) dt = 0.1f; // Cap dt to avoid huge jumps\n";
        out << "    static webcc::Event events[64];\n";
        out << "    uint32_t count = 0;\n";
        out << "    webcc::Event e;\n";
        out << "    while (webcc::poll_event(e) && count < 64) {\n";
        out << "        events[count++] = e;\n";
        out << "    }\n";
        out << "    dispatch_events(events, count);\n";
        out << "    if (app) app->tick(dt);\n";
        out << "    webcc::flush();\n";
        out << "}\n\n";

        out << "int main() {\n";
        out << "    // We allocate the app on the heap because the stack is destroyed when main() returns.\n";
        out << "    // The app needs to persist for the event loop (update_wrapper).\n";
        out << "    // We use webcc::malloc to ensure memory is tracked by the framework.\n";
        out << "    void* app_mem = webcc::malloc(sizeof(" << final_app_config.root_component << "));\n";
        out << "    app = new (app_mem) " << final_app_config.root_component << "();\n";

        // Inject CSS
        std::string all_css;
        for (const auto &comp : all_components)
        {
            if (!comp.global_css.empty())
            {
                all_css += comp.global_css + "\\n";
            }
            if (!comp.css.empty())
            {
                // Simple CSS scoping: prefix selectors with [coi-scope="ComponentName"]
                std::string scoped_css;
                std::string raw = comp.css;
                size_t pos = 0;
                while (pos < raw.length())
                {
                    size_t brace = raw.find('{', pos);
                    if (brace == std::string::npos)
                    {
                        scoped_css += raw.substr(pos);
                        break;
                    }

                    std::string selector_group = raw.substr(pos, brace - pos);
                    std::stringstream ss_sel(selector_group);
                    std::string selector;
                    bool first = true;
                    while (std::getline(ss_sel, selector, ','))
                    {
                        if (!first)
                            scoped_css += ",";
                        size_t start = selector.find_first_not_of(" \t\n\r");
                        size_t end = selector.find_last_not_of(" \t\n\r");
                        if (start != std::string::npos)
                        {
                            std::string trimmed = selector.substr(start, end - start + 1);
                            size_t colon = trimmed.find(':');
                            if (colon != std::string::npos)
                            {
                                scoped_css += trimmed.substr(0, colon) + "[coi-scope=\"" + comp.name + "\"]" + trimmed.substr(colon);
                            }
                            else
                            {
                                scoped_css += trimmed + "[coi-scope=\"" + comp.name + "\"]";
                            }
                        }
                        first = false;
                    }

                    size_t end_brace = raw.find('}', brace);
                    if (end_brace == std::string::npos)
                    {
                        scoped_css += raw.substr(brace);
                        break;
                    }
                    scoped_css += raw.substr(brace, end_brace - brace + 1);
                    pos = end_brace + 1;
                }
                all_css += scoped_css + "\\n";
            }
        }
        if (!all_css.empty())
        {
            // Escape quotes in CSS string for C++ string literal
            std::string escaped_css;
            for (char c : all_css)
            {
                if (c == '"')
                    escaped_css += "\\\"";
                else if (c == '\n')
                    escaped_css += "\\n";
                else
                    escaped_css += c;
            }

            out << "    // Inject CSS\n";
            out << "    webcc::handle style_el = webcc::dom::create_element(\"style\");\n";
            out << "    webcc::dom::set_inner_text(style_el, \"" << escaped_css << "\");\n";
            out << "    webcc::dom::append_child(webcc::dom::get_body(), style_el);\n";
        }

        out << "    app->view();\n";
        out << "    webcc::system::set_main_loop(update_wrapper);\n";
        out << "    webcc::flush();\n";
        out << "    return 0;\n";
        out << "}\n";

        out.close();
        std::cerr << "Generated " << output_cc << std::endl;

        if (!cc_only)
        {
            namespace fs = std::filesystem;
            fs::path cwd = fs::current_path();
            fs::path abs_output_cc = fs::absolute(output_cc);
            fs::path abs_output_dir = output_dir.empty() ? cwd : fs::absolute(output_dir);

            fs::create_directories("build/.webcc_cache");
            std::string cmd = "webcc " + abs_output_cc.string();
            cmd += " --out " + abs_output_dir.string();
            cmd += " --cache-dir build/.webcc_cache";

            std::cerr << "Running: " << cmd << std::endl;
            int ret = system(cmd.c_str());
            if (ret != 0)
            {
                std::cerr << "Error: webcc compilation failed." << std::endl;
                return 1;
            }

            if (!keep_cc)
            {
                fs::remove(output_cc);
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
