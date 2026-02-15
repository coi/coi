#include "css_generator.h"
#include "ast/ast.h"
#include "../cli/error.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

void generate_css_file(
    const fs::path &css_path,
    const fs::path &input_file,
    const std::vector<Component> &all_components)
{
    std::ofstream css_out(css_path);
    if (!css_out)
    {
        return;
    }

    // Bundle external stylesheets from styles/ folder at project root
    // Project root is the parent of src/
    fs::path input_dir = fs::path(input_file).parent_path();
    fs::path project_root = (input_dir.filename() == "src") ? input_dir.parent_path() : input_dir;
    fs::path styles_dir = project_root / "styles";

    if (fs::exists(styles_dir) && fs::is_directory(styles_dir))
    {
        std::vector<fs::path> css_files;
        for (const auto &entry : fs::recursive_directory_iterator(styles_dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".css")
            {
                css_files.push_back(entry.path());
            }
        }

        if (!css_files.empty())
        {
            // Sort for deterministic order
            std::sort(css_files.begin(), css_files.end());

            for (const auto &css_file_path : css_files)
            {
                std::ifstream style_file(css_file_path);
                if (style_file)
                {
                    fs::path rel_path = fs::relative(css_file_path, styles_dir.parent_path());
                    css_out << "/* " << rel_path.string() << " */\n";
                    css_out << std::string((std::istreambuf_iterator<char>(style_file)),
                                           std::istreambuf_iterator<char>());
                    css_out << "\n";
                }
                else
                {
                    ErrorHandler::warning("Could not open stylesheet: " + css_file_path.string());
                }
            }
        }
    }

    // Collect all CSS from components
    for (const auto &comp : all_components)
    {
        bool has_styles = !comp.global_css.empty() || !comp.css.empty();
        if (has_styles)
        {
            css_out << "/* " << comp.name << " */\n";
        }

        // Global CSS (no scoping)
        if (!comp.global_css.empty())
        {
            css_out << comp.global_css << "\n";
        }

        // Scoped CSS: prefix selectors with [coi-scope="ComponentName"]
        // Handle @keyframes and @media specially
        if (!comp.css.empty())
        {
            std::string raw = comp.css;
            std::string scope_name = qualified_name(comp.module_name, comp.name);
            size_t pos = 0;

            // Helper lambda to scope a single selector
            auto scope_selector = [&](const std::string &sel) -> std::string
            {
                size_t start = sel.find_first_not_of(" \t\n\r");
                size_t end = sel.find_last_not_of(" \t\n\r");
                if (start == std::string::npos)
                    return sel;
                std::string trimmed = sel.substr(start, end - start + 1);
                size_t colon = trimmed.find(':');
                if (colon != std::string::npos)
                {
                    return trimmed.substr(0, colon) + "[coi-scope=\"" + scope_name + "\"]" + trimmed.substr(colon);
                }
                else
                {
                    return trimmed + "[coi-scope=\"" + scope_name + "\"]";
                }
            };

            while (pos < raw.length())
            {
                // Skip whitespace
                while (pos < raw.length() && std::isspace(raw[pos]))
                {
                    css_out << raw[pos];
                    pos++;
                }
                if (pos >= raw.length())
                    break;

                // Check for @keyframes
                if (raw.substr(pos, 10) == "@keyframes")
                {
                    size_t kf_start = pos;
                    size_t kf_brace = raw.find('{', pos);
                    if (kf_brace == std::string::npos)
                    {
                        css_out << raw.substr(pos);
                        break;
                    }
                    // Output @keyframes name as-is (no scoping)
                    css_out << raw.substr(pos, kf_brace - pos + 1);
                    pos = kf_brace + 1;

                    // Find matching closing brace for @keyframes block
                    int brace_depth = 1;
                    size_t kf_end = pos;
                    while (kf_end < raw.length() && brace_depth > 0)
                    {
                        if (raw[kf_end] == '{')
                            brace_depth++;
                        else if (raw[kf_end] == '}')
                            brace_depth--;
                        kf_end++;
                    }
                    // Output keyframes content as-is (from, to, percentages don't get scoped)
                    css_out << raw.substr(pos, kf_end - pos);
                    pos = kf_end;
                    continue;
                }

                // Check for @media
                if (raw.substr(pos, 6) == "@media")
                {
                    size_t media_brace = raw.find('{', pos);
                    if (media_brace == std::string::npos)
                    {
                        css_out << raw.substr(pos);
                        break;
                    }
                    // Output @media query as-is
                    css_out << raw.substr(pos, media_brace - pos + 1) << "\n";
                    pos = media_brace + 1;

                    // Find matching closing brace for @media block
                    int brace_depth = 1;
                    size_t media_end = pos;
                    while (media_end < raw.length() && brace_depth > 0)
                    {
                        if (raw[media_end] == '{')
                            brace_depth++;
                        else if (raw[media_end] == '}')
                            brace_depth--;
                        media_end++;
                    }
                    media_end--; // Back up to the closing brace

                    // Process selectors inside @media
                    while (pos < media_end)
                    {
                        size_t brace = raw.find('{', pos);
                        if (brace == std::string::npos || brace >= media_end)
                            break;

                        std::string selector_group = raw.substr(pos, brace - pos);
                        std::stringstream ss_sel(selector_group);
                        std::string selector;
                        bool first = true;
                        while (std::getline(ss_sel, selector, ','))
                        {
                            if (!first)
                                css_out << ",";
                            css_out << scope_selector(selector);
                            first = false;
                        }

                        size_t end_brace = raw.find('}', brace);
                        if (end_brace == std::string::npos || end_brace >= media_end)
                        {
                            css_out << raw.substr(brace, media_end - brace);
                            break;
                        }
                        css_out << raw.substr(brace, end_brace - brace + 1) << "\n";
                        pos = end_brace + 1;
                    }
                    css_out << "}\n";
                    pos = media_end + 1;
                    continue;
                }

                // Regular selector
                size_t brace = raw.find('{', pos);
                if (brace == std::string::npos)
                {
                    css_out << raw.substr(pos);
                    break;
                }

                std::string selector_group = raw.substr(pos, brace - pos);
                std::stringstream ss_sel(selector_group);
                std::string selector;
                bool first = true;
                while (std::getline(ss_sel, selector, ','))
                {
                    if (!first)
                        css_out << ",";
                    css_out << scope_selector(selector);
                    first = false;
                }

                size_t end_brace = raw.find('}', brace);
                if (end_brace == std::string::npos)
                {
                    css_out << raw.substr(brace);
                    break;
                }
                css_out << raw.substr(brace, end_brace - brace + 1) << "\n";
                pos = end_brace + 1;
            }
            css_out << "\n";
        }
    }
    css_out.close();
    std::cerr << "Generated " << css_path.string() << std::endl;
}
