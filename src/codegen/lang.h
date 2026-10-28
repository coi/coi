#pragma once

// lang/ next to src/: an app's text in more than one language. lang/en.json is read at build
// time and built into the program; every other lang/<code>.json is written to the output as
// lazy/lang/<code>.json, a list of its texts in en.json's order ("" where it has none), fetched
// at start when that's the visitor's language. Lang.t("key") is the text, Lang.t("key", n) a
// plural's form for n (key.one, key.other, ...), Lang.use(code) keeps a choice and reloads.

#include <filesystem>
#include <map>
#include <ostream>
#include <string>
#include <vector>

struct LangTable
{
    bool present = false;            // lang/en.json is there
    std::vector<std::string> keys;   // flattened ("settings.title"), in en.json's order
    std::vector<std::string> en;     // the English text of each
    std::map<std::string, int> index;
    std::vector<std::string> codes;  // "en" first, then every other lang/<code>.json, sorted

    static LangTable &instance();
    // reads lang/en.json (if any) and lists the others; a broken file is a compiler error
    void load(const std::filesystem::path &project_root);
    int find(const std::string &key) const;
    // the slot of a plural's first category (zero, one, two, few, many, other follow it); -1
    // when en.json has no key.other
    int plural(const std::string &key) const;
};

// the table and Lang's functions, in the generated program (base: the app's deploy base)
void emit_lang_runtime(std::ostream &out, const std::string &base, bool test_program);
// writes lazy/lang/<code>.json for every language but English
void write_lang_files(const std::filesystem::path &project_root, const std::filesystem::path &out_dir);
