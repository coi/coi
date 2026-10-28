#pragma once

// lang/ next to src/: an app's text in more than one language. lang/en.json is read at build
// time and built into the program; every other lang/<code>.json is written to the output as
// lazy/lang/<code>.json, every text in en.json's order (English where it has none), fetched at
// start when that's the visitor's language. Lang.t("key") is the text, Lang.t("key", n) a
// plural's form for n (key.one, key.other, ...), Lang.use(code) keeps a choice and reloads.
//
// A text may hold tags, "agree to the <terms>Terms</terms>" or "<logo/>", which a view fills
// with elements: {Lang.t("agree", terms = <a href="/terms" />)}. Each language's text is cut at
// its tags here, at build time, into hidden texts of their own: key#0, key#1, ... the runs
// between the tags, key#name the words inside one, key#order which of the orders the tags come
// in (en.json's is "0").

#include <filesystem>
#include <map>
#include <ostream>
#include <string>
#include <vector>

// the tags of one text with them, as en.json has it
struct LangMarkup
{
    std::vector<std::string> names;                // in en.json's order
    std::map<std::string, bool> paired;            // <name>words</name> rather than <name/>
    std::vector<std::vector<std::string>> orders;  // every order a language puts them in, en.json's first
    std::vector<bool> gap_used;                    // key#i is text in some language
};

struct LangTable
{
    bool present = false;            // lang/en.json is there
    std::vector<std::string> keys;   // flattened ("settings.title"), in en.json's order
    std::vector<std::string> en;     // the English text of each
    std::map<std::string, int> index;
    std::vector<std::string> codes;  // "en" first, then every other lang/<code>.json, sorted
    std::map<std::string, LangMarkup> markup;  // the texts with tags, by key
    // every other language's texts, in keys' order (English where it has none)
    std::map<std::string, std::vector<std::string>> others;

    static LangTable &instance();
    // reads lang/*.json (if en.json is there); a broken file is a compiler error
    void load(const std::filesystem::path &project_root);
    int find(const std::string &key) const;
    // the slot of a plural's first category (zero, one, two, few, many, other follow it); -1
    // when en.json has no key.other
    int plural(const std::string &key) const;
};

// the table and Lang's functions, in the generated program (base: the app's deploy base)
void emit_lang_runtime(std::ostream &out, const std::string &base, bool test_program);
// writes lazy/lang/<code>.json for every language but English
void write_lang_files(const std::filesystem::path &out_dir);
