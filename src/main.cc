#include "frontend/lexer.h"
#include "frontend/parser/parser.h"
#include "ast/ast.h"
#include "defs/def_parser.h"
#include "analysis/type_checker.h"
#include "cli/cli.h"
#include "cli/error.h"
#include "analysis/include_detector.h"
#include "analysis/feature_detector.h"
#include "analysis/dependency_resolver.h"
#include "defs/def_loader.h"
#include "codegen/codegen.h"
#include "codegen/css_generator.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <algorithm>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

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
        if (argc >= 3)
        {
            project_name = argv[2];
        }
        return init_project(project_name);
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
    }

    if (first_arg == "build")
    {
        return build_project(keep_cc, cc_only);
    }

    if (first_arg == "dev")
    {
        return dev_project(keep_cc, cc_only);
    }

    // From here on, we're doing actual compilation - load DefSchema
    load_def_schema();

    std::string input_file;
    std::string output_dir;

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

    std::vector<Component> all_components;
    std::vector<std::unique_ptr<DataDef>> all_global_data;
    std::vector<std::unique_ptr<EnumDef>> all_global_enums;
    AppConfig final_app_config;
    std::set<std::string> processed_files;
    std::queue<std::string> file_queue;
    // Track direct imports for each file (file -> set of directly imported files)
    std::map<std::string, std::set<std::string>> file_imports;

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

            std::cerr << "Processing " << current_file_path << "..." << std::endl;

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
                comp.source_file = current_file_path;  // Track which file this component is from
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

            if (!parser.app_config.root_component.empty())
            {
                final_app_config = parser.app_config;
            }

            fs::path current_path(current_file_path);
            fs::path parent_path = current_path.parent_path();

            // Track direct imports for this file
            std::set<std::string> direct_imports;
            for (const auto &import_path_str : parser.imports)
            {
                fs::path import_path = parent_path / import_path_str;
                try
                {
                    std::string abs_path = fs::canonical(import_path).string();
                    direct_imports.insert(abs_path);
                    if (processed_files.find(abs_path) == processed_files.end())
                    {
                        file_queue.push(abs_path);
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << colors::RED << "Error:" << colors::RESET << " resolving import path " << import_path_str << ": " << e.what() << std::endl;
                    return 1;
                }
            }
            file_imports[current_file_path] = std::move(direct_imports);
        }

        std::cerr << "All files processed. Total components: " << all_components.size() << std::endl;

        validate_view_hierarchy(all_components, file_imports);
        validate_type_imports(all_components, all_global_enums, all_global_data, file_imports);
        validate_mutability(all_components);
        validate_types(all_components, all_global_enums, all_global_data);

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
        fs::path cache_dir = final_output_dir.parent_path() / ".coi_cache";
        if (final_output_dir.filename() == ".")
        {
            cache_dir = fs::current_path() / ".coi_cache";
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

        std::ofstream out(output_cc);
        if (!out)
        {
            std::cerr << "Error: Could not open output file " << output_cc << std::endl;
            return 1;
        }

        // Code generation - automatically detect required headers and features
        std::set<std::string> required_headers = get_required_headers(all_components);
        FeatureFlags features = detect_features(all_components, required_headers);

        // Generate C++ code
        generate_cpp_code(out, all_components, all_global_data, all_global_enums,
                          final_app_config, required_headers, features);

        out.close();
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
            fs::path template_path = cache_dir / "index.template.html";
            {
                std::ofstream tmpl_out(template_path);
                if (tmpl_out)
                {
                    std::string lang = final_app_config.lang.empty() ? "en" : final_app_config.lang;
                    std::string title = final_app_config.title.empty() ? "Coi App" : final_app_config.title;

                    tmpl_out << "<!DOCTYPE html>\n";
                    tmpl_out << "<html lang=\"" << lang << "\">\n";
                    tmpl_out << "<head>\n";
                    tmpl_out << "    <meta charset=\"utf-8\">\n";
                    tmpl_out << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, viewport-fit=cover\">\n";
                    tmpl_out << "    <title>" << title << "</title>\n";
                    if (!final_app_config.description.empty())
                    {
                        tmpl_out << "    <meta name=\"description\" content=\"" << final_app_config.description << "\">\n";
                    }
                    // Auto-include generated CSS
                    tmpl_out << "    <link rel=\"stylesheet\" href=\"app.css\">\n";
                    tmpl_out << "</head>\n";
                    tmpl_out << "<body>\n";
                    tmpl_out << "{{script}}\n";
                    tmpl_out << "</body>\n";
                    tmpl_out << "</html>\n";
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
            cmd += " --cache-dir " + webcc_cache_dir.string();
            cmd += " --template " + abs_template.string();

            std::cerr << "Running: " << cmd << std::endl;
            int ret = system(cmd.c_str());

            // Clean up intermediate files from cache (keep webcc cache for faster rebuilds)
            if (!keep_cc)
            {
                fs::remove(cache_dir / "app.cc");
            }
            fs::remove(cache_dir / "index.template.html");

            if (ret != 0)
            {
                std::cerr << "Error: webcc compilation failed." << std::endl;
                return 1;
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