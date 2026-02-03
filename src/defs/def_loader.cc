#include "def_loader.h"
#include "def_parser.h"
#include "../cli/cli.h"
#include "../cli/error.h"
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

void load_def_schema()
{
    // Initialize DefSchema from defs files (for @intrinsic, @inline, @map)
    // Always use the defs directory next to the executable
    fs::path exe_dir = get_executable_dir();
    std::string def_dir;

    if (!exe_dir.empty() && fs::exists(exe_dir / "defs"))
    {
        def_dir = (exe_dir / "defs").string();
    }
    else
    {
        std::string hint;
        if (exe_dir.empty())
        {
            hint = "Could not determine executable location.\n"
                   "  If you see this error, please open an issue at:\n"
                   "  https://github.com/io-eric/coi/issues\n"
                   "  Include your OS, how you installed coi, and how you ran the command.";
        }
        else
        {
            hint = "Expected location: " + (exe_dir / "defs").string();
        }
        ErrorHandler::cli_error("Could not find 'defs' directory next to executable", hint);
        exit(1);
    }

    // Load from binary cache (generated at build time by gen_schema)
    std::string cache_path = def_dir + "/.cache/definitions.coi.bin";
    auto &def_schema = DefSchema::instance();

    if (def_schema.is_cache_valid(cache_path, def_dir))
    {
        def_schema.load_cache(cache_path);
    }
    else
    {
        // Cache missing or outdated - parse def files
        def_schema.load(def_dir);
        // Save cache for next time (only in the compiler's def directory)
        fs::create_directories(def_dir + "/.cache");
        def_schema.save_cache(cache_path);
    }
}
