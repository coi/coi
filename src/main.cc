#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <functional>
#include <filesystem>
#include <cstdlib>
#include <cstdio>

// =========================================================
// MAIN COMPILER
// =========================================================

void validate_view_hierarchy(const std::vector<Component>& components) {
    std::map<std::string, const Component*> component_map;
    for (const auto& comp : components) {
        component_map[comp.name] = &comp;
    }

    std::function<void(ASTNode*)> validate_node = [&](ASTNode* node) {
        if (!node) return;

        if (auto* comp_inst = dynamic_cast<ComponentInstantiation*>(node)) {
            auto it = component_map.find(comp_inst->component_name);
            if (it != component_map.end()) {
                if (!it->second->render_root) {
                     throw std::runtime_error("Component '" + comp_inst->component_name + "' is used in a view but has no view definition (logic-only component) at line " + std::to_string(comp_inst->line));
                }
            }
        } else if (auto* el = dynamic_cast<HTMLElement*>(node)) {
            for (const auto& child : el->children) {
                validate_node(child.get());
            }
        }
    };

    for (const auto& comp : components) {
        if (comp.render_root) {
            validate_node(comp.render_root.get());
        }
    }
}

int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "Usage: " << argv[0] << " <input.coi> [--cc-only] [--keep-cc] [--out <dir> | -o <dir>]" << std::endl;
        return 1;
    }

    std::string input_file;
    std::string output_dir;
    bool cc_only = false;
    bool keep_cc = false;

    for(int i=1; i<argc; ++i) {
        std::string arg = argv[i];
        if(arg == "--cc-only") cc_only = true;
        else if(arg == "--keep-cc") keep_cc = true;
        else if(arg == "--out" || arg == "-o") {
            if(i+1 < argc) {
                output_dir = argv[++i];
            } else {
                std::cerr << "Error: --out requires an argument" << std::endl;
                return 1;
            }
        }
        else if(input_file.empty()) input_file = arg;
        else {
            std::cerr << "Unknown argument or multiple input files: " << arg << std::endl;
            return 1;
        }
    }

    if(input_file.empty()) {
        std::cerr << "No input file specified." << std::endl;
        return 1;
    }

    // Read source file
    std::ifstream file(input_file);
    if(!file){
        std::cerr << "Error: Could not open file " << input_file << std::endl;
        return 1;
    }

    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    try {
        // Lexical analysis
        std::cerr << "Lexing..." << std::endl;
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        std::cerr << "Lexing done. Tokens: " << tokens.size() << std::endl;

        // Parsing
        std::cerr << "Parsing..." << std::endl;
        Parser parser(tokens);
        parser.parse_file();
        std::cerr << "Parsing done. Components: " << parser.components.size() << std::endl;

        validate_view_hierarchy(parser.components);

        // Determine output filename
        namespace fs = std::filesystem;
        fs::path input_path(input_file);
        fs::path output_path;

        if(!output_dir.empty()) {
            fs::path out_dir_path(output_dir);
            try {
                fs::create_directories(out_dir_path);
            } catch(const fs::filesystem_error& e) {
                 std::cerr << "Error: Could not create output directory " << output_dir << ": " << e.what() << std::endl;
                 return 1;
            }
            
            output_path = out_dir_path / input_path.stem();
            output_path += ".cc";
        } else {
            output_path = input_path;
            output_path.replace_extension(".cc");
        }

        std::string output_cc = output_path.string();

        std::ofstream out(output_cc);
        if(!out) {
             std::cerr << "Error: Could not open output file " << output_cc << std::endl;
             return 1;
        }

        // Code generation
        // This should in best case be automated based on what is used in the coi source files
        out << "#include \"webcc/canvas.h\"\n";
        out << "#include \"webcc/dom.h\"\n";
        out << "#include \"webcc/system.h\"\n";
        out << "#include \"webcc/input.h\"\n";
        out << "#include \"webcc/core/function.h\"\n";
        out << "#include \"webcc/core/allocator.h\"\n";
        out << "#include \"webcc/core/new.h\"\n\n";

        out << "struct Listener {\n";
        out << "    int32_t handle;\n";
        out << "    webcc::function<void()> callback;\n";
        out << "};\n\n";

        out << "struct EventDispatcher {\n";
        out << "    static constexpr int MAX_LISTENERS = 128;\n";
        out << "    Listener listeners[MAX_LISTENERS];\n";
        out << "    int count = 0;\n";
        out << "    void register_click(webcc::handle h, webcc::function<void()> cb) {\n";
        out << "        if (count < MAX_LISTENERS) {\n";
        out << "            listeners[count].handle = (int32_t)h;\n";
        out << "            listeners[count].callback = cb;\n";
        out << "            count++;\n";
        out << "        }\n";
        out << "    }\n";
        out << "    void dispatch(const webcc::Event* events, uint32_t event_count) {\n";
        out << "        for(uint32_t i=0; i<event_count; ++i) {\n";
        out << "            const auto& e = events[i];\n";
        out << "            if (e.opcode == webcc::dom::ClickEvent::OPCODE) {\n";
        out << "                auto click = e.as<webcc::dom::ClickEvent>();\n";
        out << "                if (click) {\n";
        out << "                    for(int j=0; j<count; ++j) {\n";
        out << "                        if (listeners[j].handle == (int32_t)click->handle) {\n";
        out << "                            listeners[j].callback();\n";
        out << "                        }\n";
        out << "                    }\n";
        out << "                }\n";
        out << "            }\n";
        out << "        }\n";
        out << "    }\n";
        out << "};\n";
        out << "EventDispatcher g_dispatcher;\n\n";

        // Forward declarations
        for(auto& comp : parser.components) {
            out << "class " << comp.name << ";\n";
        }
        out << "\n";

        for(auto& comp : parser.components) {
            out << comp.to_webcc();
        }

        if(parser.app_config.root_component.empty()) {
             std::cerr << "Error: No root component defined. Use 'app { root = ComponentName }' to define the entry point." << std::endl;
             return 1;
        }

        out << "\n" << parser.app_config.root_component << "* app = nullptr;\n";
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
            out << "    g_dispatcher.dispatch(events, count);\n";
            out << "    if (app) app->tick(dt);\n";
            out << "    webcc::flush();\n";
            out << "}\n\n";

            out << "int main() {\n";
            out << "    // We allocate the app on the heap because the stack is destroyed when main() returns.\n";
            out << "    // The app needs to persist for the event loop (update_wrapper).\n";
            out << "    // We use webcc::malloc to ensure memory is tracked by the framework.\n";
            out << "    void* app_mem = webcc::malloc(sizeof(" << parser.app_config.root_component << "));\n";
            out << "    app = new (app_mem) " << parser.app_config.root_component << "();\n";
            
            // Inject CSS
            std::string all_css;
            for(const auto& comp : parser.components) {
                if(!comp.global_css.empty()) {
                    all_css += comp.global_css + "\\n";
                }
                if(!comp.css.empty()) {
                    // Simple CSS scoping: prefix selectors with [coi-scope="ComponentName"]
                    std::string scoped_css;
                    std::string raw = comp.css;
                    size_t pos = 0;
                    while (pos < raw.length()) {
                        size_t brace = raw.find('{', pos);
                        if (brace == std::string::npos) {
                            scoped_css += raw.substr(pos);
                            break;
                        }
                        
                        std::string selector_group = raw.substr(pos, brace - pos);
                        std::stringstream ss_sel(selector_group);
                        std::string selector;
                        bool first = true;
                        while (std::getline(ss_sel, selector, ',')) {
                            if (!first) scoped_css += ",";
                            size_t start = selector.find_first_not_of(" \t\n\r");
                            size_t end = selector.find_last_not_of(" \t\n\r");
                            if (start != std::string::npos) {
                                std::string trimmed = selector.substr(start, end - start + 1);
                                size_t colon = trimmed.find(':');
                                if (colon != std::string::npos) {
                                    scoped_css += trimmed.substr(0, colon) + "[coi-scope=\"" + comp.name + "\"]" + trimmed.substr(colon);
                                } else {
                                    scoped_css += trimmed + "[coi-scope=\"" + comp.name + "\"]";
                                }
                            }
                            first = false;
                        }
                        
                        size_t end_brace = raw.find('}', brace);
                        if (end_brace == std::string::npos) {
                            scoped_css += raw.substr(brace);
                            break;
                        }
                        scoped_css += raw.substr(brace, end_brace - brace + 1);
                        pos = end_brace + 1;
                    }
                    all_css += scoped_css + "\\n";
                }
            }
            if(!all_css.empty()) {
                // Escape quotes in CSS string for C++ string literal
                std::string escaped_css;
                for(char c : all_css) {
                    if(c == '"') escaped_css += "\\\"";
                    else if(c == '\n') escaped_css += "\\n";
                    else escaped_css += c;
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

        if (!cc_only) {
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
            if (ret != 0) {
                std::cerr << "Error: webcc compilation failed." << std::endl;
                return 1;
            }
            
            if (!keep_cc) {
                fs::remove(output_cc);
            }
        }

    } catch(const std::exception& e){
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
