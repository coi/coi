#include <sstream>
#include "cli/log.h"
#include "frontend/lexer.h"
#include "frontend/parser/parser.h"
#include "ast/ast.h"
#include "defs/def_parser.h"
#include "analysis/type_checker.h"
#include "cli/cli.h"
#include "cli/error.h"
#include "cli/package_manager.h"
#include "analysis/include_detector.h"
#include "analysis/feature_detector.h"
#include "analysis/dependency_resolver.h"
#include "defs/def_loader.h"
#include "codegen/codegen.h"
#include "codegen/css_generator.h"
#include "codegen/pwa_generator.h"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <queue>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <regex>
#include <set>

namespace fs = std::filesystem;

// app.prerender: the generated C++ is built for this machine by webcc and run once per static
// route of the root's router ("/" without one); the HTML of each first render goes into its
// own copy of index.html (route "/x" -> x/index.html); the loaded app hydrates it
static bool prerender_pages(const AppConfig &config, const std::vector<Component> &components,
                            const fs::path &app_cc, const fs::path &out_dir, const fs::path &cache_dir)
{
    std::vector<std::string> paths;
    for (const auto &c : components)
    {
        if (c.name != config.root_component || !c.router)
            continue;
        for (const auto &r : c.router->routes)
            if (!r.is_default && r.path_params.empty())
                paths.push_back(r.path);
    }
    if (paths.empty())
        paths.push_back("/");

    progress("prerendering " + std::to_string(paths.size()) + (paths.size() == 1 ? " page" : " pages"));
    fs::path pre_dir = cache_dir / "prerender";
    fs::create_directories(pre_dir);
    std::string cmd = "\"" + (fs::path(get_executable_dir()) / "deps" / "webcc" / "webcc").string() + "\" \"" + app_cc.string() +
                      "\" --cache-dir \"" + (cache_dir / "webcc-host").string() + "\"" + (g_verbose ? "" : " --quiet");
    for (size_t i = 0; i < paths.size(); i++)
        cmd += " --render '" + paths[i] + "=" + (pre_dir / ("page" + std::to_string(i) + ".html")).string() + "'";
    if (system(cmd.c_str()) != 0)
    {
        std::cerr << colors::RED << "Error:" << colors::RESET << " app.prerender: rendering the pages failed" << std::endl;
        return false;
    }

    std::ifstream index_in(out_dir / "index.html");
    std::string index((std::istreambuf_iterator<char>(index_in)), std::istreambuf_iterator<char>());
    index_in.close();
    const std::string marker = "<!--coi-pre-->";
    size_t at = index.find(marker);
    if (at == std::string::npos)
    {
        std::cerr << colors::RED << "Error:" << colors::RESET << " app.prerender: index.html has no place for the page" << std::endl;
        return false;
    }
    for (size_t i = 0; i < paths.size(); i++)
    {
        std::ifstream in(pre_dir / ("page" + std::to_string(i) + ".html"));
        std::string html((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // the root element is marked for the app to find and take over
        size_t tag_end = html.find_first_of(" >", html.find('<') + 1);
        if (html.empty() || html[0] != '<' || tag_end == std::string::npos)
        {
            std::cerr << colors::RED << "Error:" << colors::RESET << " app.prerender: " << paths[i] << " rendered no element" << std::endl;
            return false;
        }
        html.insert(tag_end, " data-hydrate");
        std::string page = index;
        page.replace(at, marker.size(), html);
        std::string rel = paths[i];
        while (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
        while (!rel.empty() && rel.back() == '/') rel.pop_back();
        fs::path file = rel.empty() ? out_dir / "index.html" : out_dir / rel / "index.html";
        fs::create_directories(file.parent_path());
        std::ofstream out(file);
        out << page;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_help(argv[0]);
        return 1;
    }

    std::string first_arg = argv[1];

    // Handle special commands
    if (first_arg == "help" || first_arg == "--help" || first_arg == "-h")
    {
        print_help(argv[0]);
        return 0;
    }

    if (first_arg == "version" || first_arg == "--version" || first_arg == "-v")
    {
        print_version();
        return 0;
    }

    if (first_arg == "init")
    {
        std::string project_name;
        TemplateType template_type = TemplateType::App;
        
        // Parse init arguments (name and --pkg can be in any order)
        for (int i = 2; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--pkg")
            {
                template_type = TemplateType::Pkg;
            }
            else if (arg[0] != '-' && project_name.empty())
            {
                project_name = arg;
            }
        }
        return init_project(project_name, template_type);
    }

    // Hidden command for build system to pre-generate cache
    if (first_arg == "--gen-def-cache")
    {
        load_def_schema();
        return 0;
    }

    // Return the absolute path to the bundled def/ directory next to the executable
    // TODO: Deprecate --def-path once VS Code extension v1.0.12 is released.
    if (first_arg == "--def-path" || first_arg == "--defs-path")
    {
        fs::path exe_dir = get_executable_dir();
        if (exe_dir.empty())
        {
            ErrorHandler::cli_error("could not determine executable directory");
            return 1;
        }
        fs::path def_dir = exe_dir / "defs";
        std::cout << def_dir.string() << std::endl;
        return 0;
    }

    // Parse build flags (shared by build, dev, and direct compilation)
    bool keep_cc = false;
    bool cc_only = false;
    for (int i = 2; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--keep-cc")
            keep_cc = true;
        else if (arg == "--cc-only")
            cc_only = true;
        else if (arg == "--verbose" || arg == "-V")
            g_verbose = true;
    }

    if (first_arg == "build")
    {
        bool rebuild = false;   // the dev server's rebuilds: no banner
        for (int i = 2; i < argc; ++i)
            if (std::string(argv[i]) == "--rebuild") rebuild = true;
        return build_project(keep_cc, cc_only, rebuild);
    }

    if (first_arg == "dev")
    {
        bool hot_reloading = true;  // Hot reload is now the default
        for (int i = 2; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--no-watch")
            {
                hot_reloading = false;
            }
        }
        return dev_project(keep_cc, cc_only, hot_reloading);
    }

    if (first_arg == "self-upgrade")
    {
        return self_upgrade();
    }

    // Package management commands
    if (first_arg == "add")
    {
        if (argc < 3)
        {
            std::cerr << colors::RED << "Error:" << colors::RESET << " Package name required" << std::endl;
            std::cerr << "  Usage: coi add <scope/name> [version]" << std::endl;
            return 1;
        }
        std::string requested_version = (argc >= 4) ? argv[3] : "";
        return add_package(argv[2], requested_version);
    }

    if (first_arg == "install")
    {
        return install_packages();
    }

    if (first_arg == "remove")
    {
        if (argc < 3)
        {
            std::cerr << colors::RED << "Error:" << colors::RESET << " Package name required" << std::endl;
            std::cerr << "  Usage: coi remove <scope/name>" << std::endl;
            return 1;
        }
        return remove_package(argv[2]);
    }

    if (first_arg == "list")
    {
        return list_packages();
    }

    if (first_arg == "upgrade")
    {
        if (argc >= 3)
        {
            return update_package(argv[2]);
        }
        return update_all_packages();
    }

    // Print the full AI/LLM context (llms-full.txt), or its path with --path.
    // Lets AI assistants and agents load the complete Coi language + API reference.
    if (first_arg == "llms")
    {
        bool path_only = false;
        for (int i = 2; i < argc; ++i)
        {
            if (std::string(argv[i]) == "--path")
                path_only = true;
        }
        return llms_command(path_only);
    }

    // From here on, we're doing actual compilation - load DefSchema
    load_def_schema();

    std::string input_file;
    std::string output_dir;
    bool dev = false;   // a dev build: quick to compile, no manifest or service worker

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--cc-only")
            cc_only = true;
        else if (arg == "--keep-cc")
            keep_cc = true;
        else if (arg == "--verbose" || arg == "-V")
            g_verbose = true;
        else if (arg == "--progress")
            g_progress = true;
        else if (arg == "--dev")
            dev = true;
        else if (arg == "--out" || arg == "-o")
        {
            if (i + 1 < argc)
            {
                output_dir = argv[++i];
            }
            else
            {
                ErrorHandler::cli_error("--out requires an argument");
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

    // Determine project root (where .coi/pkgs/ lives)
    // If input is src/App.coi, project root is the parent of src/
    fs::path project_root;
    try
    {
        fs::path input_abs = fs::canonical(input_file);
        if (input_abs.parent_path().filename() == "src")
        {
            project_root = input_abs.parent_path().parent_path();
        }
        else
        {
            // Fall back to current working directory
            project_root = fs::current_path();
        }
    }
    catch (const std::exception &e)
    {
        project_root = fs::current_path();
    }

    std::vector<Component> all_components;
    std::vector<std::unique_ptr<DataDef>> all_global_data;
    std::vector<std::unique_ptr<EnumDef>> all_global_enums;
    std::vector<std::unique_ptr<FunctionDef>> all_global_functions;
    AppConfig final_app_config;
    std::set<std::string> processed_files;
    std::queue<std::string> file_queue;
    // Track direct imports for each file (file -> set of directly imported files)
    std::map<std::string, std::set<std::string>> file_imports;
    // Track pub imports for re-export resolution (file -> set of pub imported files)
    std::map<std::string, std::set<std::string>> pub_imports;

    try
    {
        file_queue.push(fs::canonical(input_file).string());
    }
    catch (const std::exception &e)
    {
        std::cerr << colors::RED << "Error:" << colors::RESET << " resolving input file path: " << e.what() << std::endl;
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

            if (g_verbose) std::cerr << "Processing " << current_file_path << "..." << std::endl;
            progress("parsing " + fs::path(current_file_path).filename().string());

            std::ifstream file(current_file_path);
            if (!file)
            {
                std::cerr << colors::RED << "Error:" << colors::RESET << " Could not open file " << current_file_path << std::endl;
                return 1;
            }
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // Lexical analysis
            Lexer lexer(source);
            auto tokens = lexer.tokenize();

            // Parsing
            Parser parser(tokens);
            parser.parse_file();

            // Add components with duplicate name check (allow same name in different modules)
            for (auto &comp : parser.components)
            {
                bool duplicate = false;
                for (const auto &existing : all_components)
                {
                    if (existing.name == comp.name && existing.module_name == comp.module_name)
                    {
                        std::cerr << colors::RED << "Error:" << colors::RESET << " Component '" << comp.name << "' is defined multiple times (found in " << current_file_path << " at line " << comp.line << ")" << std::endl;
                        return 1;
                    }
                }
                comp.source_file = current_file_path; // Track which file this component is from
                all_components.push_back(std::move(comp));
            }

            // Collect global enums
            for (auto &enum_def : parser.global_enums)
            {
                enum_def->source_file = current_file_path;
                all_global_enums.push_back(std::move(enum_def));
            }

            // Collect global data types
            for (auto &data_def : parser.global_data)
            {
                data_def->source_file = current_file_path;
                all_global_data.push_back(std::move(data_def));
            }

            // top-level functions, same name ok across modules
            for (auto &func : parser.global_functions)
            {
                for (const auto &existing : all_global_functions)
                {
                    if (existing->name == func->name && existing->module_name == func->module_name)
                    {
                        std::cerr << colors::RED << "Error:" << colors::RESET << " Function '" << func->name << "' is defined multiple times (found in " << current_file_path << " at line " << func->line << ")" << std::endl;
                        return 1;
                    }
                }
                func->source_file = current_file_path;
                all_global_functions.push_back(std::move(func));
            }

            if (!parser.app_config.root_component.empty())
            {
                final_app_config = parser.app_config;
            }

            fs::path current_path(current_file_path);
            fs::path parent_path = current_path.parent_path();

            // Track direct imports and pub imports for this file
            std::set<std::string> direct_imports;
            std::set<std::string> current_pub_imports;
            for (const auto &import_decl : parser.imports)
            {
                fs::path import_path;
                const std::string &import_str = import_decl.path;
                
                if (!import_str.empty() && import_str[0] == '@')
                {
                    // Package import:
                    //   @scope/pkg-name -> .coi/pkgs/scope/pkg-name/Mod.coi
                    //   @scope/pkg-name/path -> .coi/pkgs/scope/pkg-name/path.coi
                    std::string pkg_path = import_str.substr(1);

                    size_t slash_count = static_cast<size_t>(std::count(pkg_path.begin(), pkg_path.end(), '/'));
                    if (slash_count == 0)
                    {
                        std::cerr << colors::RED << "Error:" << colors::RESET
                                  << " package import must use scoped format @scope/name: " << import_str << std::endl;
                        return 1;
                    }
                    
                    // @scope/pkg-name -> @scope/pkg-name/Mod.coi
                    if (slash_count == 1 && (pkg_path.size() < 4 || pkg_path.substr(pkg_path.size() - 4) != ".coi"))
                    {
                        pkg_path += "/Mod.coi";
                    }
                    else if (pkg_path.size() < 4 || pkg_path.substr(pkg_path.size() - 4) != ".coi")
                    {
                        pkg_path += ".coi";
                    }
                    
                    import_path = project_root / ".coi" / "pkgs" / pkg_path;
                }
                else
                {
                    // Relative import
                    import_path = parent_path / import_str;
                }
                
                try
                {
                    std::string abs_path = fs::canonical(import_path).string();
                    direct_imports.insert(abs_path);
                    if (import_decl.is_public)
                    {
                        current_pub_imports.insert(abs_path);
                    }
                    if (processed_files.find(abs_path) == processed_files.end())
                    {
                        file_queue.push(abs_path);
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << colors::RED << "Error:" << colors::RESET << " resolving import path " << import_decl.path << ": " << e.what() << std::endl;
                    return 1;
                }
            }
            file_imports[current_file_path] = std::move(direct_imports);
            if (!current_pub_imports.empty())
            {
                pub_imports[current_file_path] = std::move(current_pub_imports);
            }
        }

        // Expand file_imports to include transitively re-exported files via pub imports
        // If A imports B and B has `pub import C`, then A should also have access to C's exports
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (auto &[file, imports] : file_imports)
            {
                std::set<std::string> to_add;
                for (const auto &imported_file : imports)
                {
                    // Check if imported_file has pub imports
                    auto pub_it = pub_imports.find(imported_file);
                    if (pub_it != pub_imports.end())
                    {
                        for (const auto &reexported : pub_it->second)
                        {
                            if (imports.find(reexported) == imports.end())
                            {
                                to_add.insert(reexported);
                            }
                        }
                    }
                }
                if (!to_add.empty())
                {
                    imports.insert(to_add.begin(), to_add.end());
                    changed = true;
                }
            }
        }

        if (g_verbose) std::cerr << "All files processed. Total components: " << all_components.size() << std::endl;
        progress("generating code for " + std::to_string(all_components.size()) + " components");

        register_free_functions(all_global_functions);

        // platform enums are Coi enums in every program
        for (const auto &[type_name, type_def] : DefSchema::instance().types())
        {
            if (type_def.enum_cpp.empty())
                continue;
            for (const auto &e : all_global_enums)
            {
                if (e->name == type_name)
                {
                    std::cerr << colors::RED << "Error:" << colors::RESET << " enum '" << type_name << "' in " << e->source_file
                              << " has the name of a platform type. Rename it." << std::endl;
                    return 1;
                }
            }
            auto platform = std::make_unique<EnumDef>();
            platform->name = type_name;
            platform->values = type_def.enum_values;
            platform->is_public = true;
            all_global_enums.push_back(std::move(platform));
        }
        // event pods too
        for (const auto &[type_name, type_def] : DefSchema::instance().types())
        {
            if (!type_def.is_pod)
                continue;
            for (const auto &d : all_global_data)
            {
                if (d->name == type_name)
                {
                    std::cerr << colors::RED << "Error:" << colors::RESET << " pod '" << type_name << "' in " << d->source_file
                              << " has the name of a platform type. Rename it." << std::endl;
                    return 1;
                }
            }
            auto pod = std::make_unique<DataDef>();
            pod->name = type_name;
            pod->is_public = true;
            for (const auto &f : type_def.pod_fields)
                pod->fields.push_back({f.type, f.name});
            all_global_data.push_back(std::move(pod));
        }

        validate_view_hierarchy(all_components, all_global_functions, file_imports);
        validate_type_imports(all_components, all_global_enums, all_global_data, file_imports);
        validate_mutability(all_components);
        validate_types(all_components, all_global_enums, all_global_data, all_global_functions, file_imports);


        // Determine output filename
        fs::path input_path(input_file);
        fs::path output_path;
        fs::path final_output_dir;

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
            final_output_dir = out_dir_path;
        }
        else
        {
            final_output_dir = input_path.parent_path();
            if (final_output_dir.empty())
                final_output_dir = ".";
        }

        // Create cache directory in project folder (alongside output dir)
        fs::path cache_dir = final_output_dir.parent_path() / ".coi" / "cache";
        if (final_output_dir.filename() == ".")
        {
            cache_dir = fs::current_path() / ".coi" / "cache";
        }
        fs::create_directories(cache_dir);

        // Generate .cc in output dir if --keep-cc or --cc-only, otherwise in cache
        if (keep_cc || cc_only)
        {
            output_path = final_output_dir / "app.cc";
        }
        else
        {
            output_path = cache_dir / "app.cc";
        }

        std::string output_cc = output_path.string();

        // temp file + rename: a concurrent build (coi dev) may have app.cc mmapped in clang, truncating it in place is a SIGBUS
        fs::path tmp_path = output_path;
        tmp_path += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
        // a codegen error exits mid-write: don't leave the temp file next to the source
        static fs::path pending_tmp;
        pending_tmp = tmp_path;
        std::atexit([] { std::error_code ec; if (!pending_tmp.empty()) fs::remove(pending_tmp, ec); });
        std::ofstream out(tmp_path.string());
        if (!out)
        {
            std::cerr << "Error: Could not open output file " << output_cc << std::endl;
            return 1;
        }

        // Code generation - automatically detect required headers and features
        // only what the app reaches from its root goes into the build (every component was
        // checked above): what nothing uses costs no code and no CSS
        {
            std::vector<std::string> roots;
            for (const auto &c : all_components)
                if (c.name == final_app_config.root_component) roots.push_back(qualified_name(c.module_name, c.name));
            for (const auto &[route, comp] : final_app_config.routes)
                for (const auto &c : all_components)
                    if (c.name == comp) roots.push_back(qualified_name(c.module_name, c.name));
            std::set<std::string> keep;
            for (const auto &r : roots)
                for (const auto &q : reachable_components(all_components, r)) keep.insert(q);
            std::vector<Component> kept;
            for (auto &c : all_components)
            {
                if (keep.count(qualified_name(c.module_name, c.name))) kept.push_back(std::move(c));
                else if (g_verbose) std::cerr << "Not reachable from the root, left out: " << c.name << std::endl;
            }
            all_components = std::move(kept);
        }

        // a dev build serves the app as it is; prerendering is for what gets deployed
        if (dev || cc_only)
            final_app_config.prerender = false;

        std::set<std::string> required_headers = get_required_headers(all_components, all_global_functions);
        FeatureFlags features = detect_features(all_components, required_headers, all_global_functions);

        // Generate C++ code
        generate_cpp_code(out, all_components, all_global_data, all_global_enums, all_global_functions,
                          final_app_config, required_headers, features);

        out.close();
        std::error_code rename_err;
        fs::rename(tmp_path, output_path, rename_err);
        pending_tmp.clear();
        if (rename_err)
        {
            std::cerr << "Error: Could not write " << output_cc << ": " << rename_err.message() << std::endl;
            fs::remove(tmp_path);
            return 1;
        }
        if (keep_cc)
        {
            std::cerr << "Generated " << output_cc << std::endl;
        }

        if (!cc_only)
        {
            // Generate CSS file with all styles
            fs::path css_path = final_output_dir / "app.css";
            generate_css_file(css_path, input_file, all_components);
        }

        // Run WebCC if not cc-only
        if (!cc_only)
        {
            // Generate HTML template in cache directory
            // per-build name: two builds of one project (a `coi dev` watcher and a
            // manual `coi build`) must not read each other's half-written template
            const std::string template_name = "index.template." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".html";
            fs::path template_path = cache_dir / template_name;

            {
                std::string lang = final_app_config.lang.empty() ? "en" : final_app_config.lang;
                std::string title = final_app_config.title.empty() ? "Coi App" : final_app_config.title;
                std::string base = final_app_config.base.empty() ? "/" : final_app_config.base;

                std::ostringstream head;
                // Resolve assets/routes against the deploy base (see `base` in app{}).
                head << "    <base href=\"" << base << "\">\n";
                head << "    <meta charset=\"utf-8\">\n";
                head << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, viewport-fit=cover\">\n";
                head << "    <title>" << title << "</title>\n";
                if (!final_app_config.description.empty())
                {
                    head << "    <meta name=\"description\" content=\"" << final_app_config.description << "\">\n";
                }
                // Auto-include generated CSS using deploy-path-safe relative URL
                head << "    <link rel=\"stylesheet\" href=\"./app.css\">\n";
                head << pwa_head_tags(final_app_config);
                // app.head: the project's own lines for every page's <head>
                if (!final_app_config.head.empty())
                {
                    std::ifstream in(project_root / final_app_config.head);
                    if (!in)
                    {
                        std::cerr << colors::RED << "Error:" << colors::RESET << " app.head: could not read " << (project_root / final_app_config.head).string() << std::endl;
                        return 1;
                    }
                    head << std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) << "\n";
                }

                // a prerendered page's markup goes at the marker, one copy of the page per route
                std::string page = "<!DOCTYPE html>\n<html lang=\"" + lang + "\">\n<head>\n" + head.str() +
                                   "</head>\n<body>\n" + (final_app_config.prerender ? "<!--coi-pre-->\n" : "") +
                                   "{{script}}\n</body>\n</html>\n";
                std::ofstream tmpl_out(template_path);
                if (tmpl_out)
                {
                    tmpl_out << page;
                    tmpl_out.close();
                }
            }

            // Prepare WebCC command
            fs::path webcc_path = fs::path(get_executable_dir()) / "deps" / "webcc" / "webcc";
            fs::path abs_output_cc = fs::absolute(output_path);
            fs::path abs_output_dir = fs::absolute(final_output_dir);
            fs::path abs_template = fs::absolute(template_path);
            fs::path webcc_cache_dir = cache_dir / "webcc";

            if (!fs::exists(webcc_path))
            {
                std::cerr << colors::RED << "Error:" << colors::RESET << " Could not find webcc at " << webcc_path << std::endl;
                return 1;
            }

            std::string cmd = webcc_path.string() + " " + abs_output_cc.string();
            cmd += " --out " + abs_output_dir.string();
            cmd += " --cache-dir " + (dev ? webcc_cache_dir.string() + "-dev" : webcc_cache_dir.string());   // dev objects are compiled differently
            cmd += " --template " + abs_template.string();
            if (!g_verbose)
                cmd += " --quiet";
            else
                std::cerr << "Running: " << cmd << std::endl;
            if (g_progress)
                cmd += " --progress";
            if (dev)
                cmd += " --dev";
            progress("compiling to WebAssembly");
            int ret = system(cmd.c_str());

            fs::remove(template_path);

            if (ret != 0)
            {
                std::cerr << "Error: webcc compilation failed." << std::endl;
                return 1;
            }
            if (final_app_config.prerender && !prerender_pages(final_app_config, all_components, abs_output_cc, abs_output_dir, cache_dir))
                return 1;
            // Clean up intermediate files from cache (keep webcc cache for faster rebuilds)
            if (!keep_cc)
                fs::remove(cache_dir / "app.cc");
            if (final_app_config.pwa && !dev)
            {
                progress("bundling");
                generate_pwa_files(final_output_dir, final_app_config);
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << colors::RED << "Error:" << colors::RESET << " " << e.what() << std::endl;
        return 1;
    }

    return 0;
}