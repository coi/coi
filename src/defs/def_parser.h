// Definition file parser for .d.coi files
// Reads @map, @inline, and @intrinsic annotations to build the schema

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <optional>

// Method mapping types
enum class MappingType
{
    Map,       // @map("ns::func") - calls webcc function
    Inline,    // @inline("${this}.method()") - inline C++ template
    Intrinsic, // @intrinsic("name") - special compiler handling
    Event      // @event("ns::NAME key fields [last]") - registers a callback for a webcc event
};

struct MethodParam
{
    std::string type;
    std::string name;
    bool has_default = false; // `= value` in the def: the argument may be left out
};

struct MethodDef
{
    std::string name;
    std::vector<MethodParam> params;
    std::string return_type;
    bool is_shared = false;   // static method
    bool is_constant = false; // shared constant (no params, accessed as property)

    MappingType mapping_type = MappingType::Map;
    std::string mapping_value; // The string in the annotation

    // Leading params without a default: the least a call must pass
    size_t required_params() const
    {
        size_t n = 0;
        while (n < params.size() && !params[n].has_default)
            n++;
        return n;
    }
};

// @event("ns::NAME key f1,f2 last"): the webcc event a callback method binds to.
// key is the event field holding the handle, fields are the rest in order (passed to
// the callback), last means the handle gets no more events after this one.
struct SchemaEventSpec
{
    std::string ns;
    std::string name;
    std::string key;
    std::vector<std::string> fields;
    bool last = false;
    std::string pod; // "PointerEvent": a handler may take the fields as this one pod
    // The function that turns the event on ("add_pointer_listener") and the value of each of
    // its params after the handle, from the whitelist's +listen
    struct ListenParam
    {
        std::string name, type, value;
    };
    std::string listen;
    std::vector<ListenParam> listen_params;

    static SchemaEventSpec parse(const std::string &value);
    std::string struct_name() const; // webcc's C++ struct: ns::NameEvent
    std::string method_name() const; // Coi's callback method: onName
};

// A view attribute for a webcc event, derived from the defs: every DOMElement event with
// a listener gives on<event> (onpointer, onwheel), and an event whose first field is an
// enum also gives one attribute per value (onpointerdown, onpointerup...)
struct ViewEventAttr
{
    std::string attr;         // "onpointerdown"
    std::string type;         // "pointer": one handler table per event, shared by its attributes
    const struct MethodDef *method = nullptr; // DOMElement.onPointer
    std::string phase;        // "Down" for a per-value attribute, else ""
    std::string phase_enum;   // "PointerPhase"
};

// Callback type "def(T1,T2):ret" as written in a def file
std::vector<std::string> callback_param_types(const std::string &def_type);

struct TypeDef
{
    std::string name;
    bool is_builtin = false; // @builtin types like string, array
    bool is_nocopy = false;  // @nocopy - type cannot be copied, only moved or referenced
    bool is_handle = false;  // @handle - a webcc handle type (webcc::Name in C++)
    std::string extends;     // Parent type (for handle inheritance)
    std::string alias_of;    // @alias("target") - this type is an alias for another
    std::string cleanup;     // @cleanup("ns::func") - called on owned members when the component is destroyed
    // @enum("webcc::ns::Name") type Name { A, B }: a platform enum. The compiler adds it
    // as a Coi enum; the values match the C++ enum class 1:1, so values convert by cast.
    std::string enum_cpp;
    std::vector<std::string> enum_values;
    // @flags("webcc::ns::Name") @alias("uint8"): platform bit flags. Values are shared
    // constants combined with |; the alias makes the type behave as its integer.
    std::string flags_cpp;
    // @pod type PointerEvent { PointerPhase phase; float32 x; }: an event's fields as one
    // value. The compiler adds it as a Coi pod.
    bool is_pod = false;
    std::vector<MethodParam> pod_fields;
    std::vector<MethodDef> methods;
};

struct DefFile
{
    std::string path;
    std::vector<TypeDef> types;
};

class DefParser
{
public:
    // Parse a single .d.coi file
    std::optional<DefFile> parse_file(const std::string &path);

    // Parse all .d.coi files in a directory (recursive)
    std::vector<DefFile> parse_directory(const std::string &dir_path);

private:
    // Minimal token set for def files (no need for full expression parsing)
    struct Token
    {
        enum Type
        {
            Eof,
            Identifier,
            StringLiteral,
            LParen,
            RParen,
            LBrace,
            RBrace,
            LBracket,
            RBracket,
            Colon,
            Comma,
            Dot,
            At,
            Less,
            Greater,
            Equals,
            KwType,
            KwDef,
            KwShared,
            KwExtends
        };
        Type type;
        std::string value;
        int line;
    };

    // Lexer state
    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;

    // Lexer
    Token next_token();
    Token peek_token();
    void skip_whitespace_and_comments();
    std::string read_string();
    std::string read_identifier();

    // Parser
    std::optional<TypeDef> parse_type();
    std::optional<MethodDef> parse_method(const std::vector<std::pair<std::string, std::string>> &annotations);
    std::vector<MethodParam> parse_params();
    std::pair<std::string, std::string> parse_annotation(); // returns (name, value)

    Token current_;
    void advance();
    bool match(Token::Type type);
    bool expect(Token::Type type, const std::string &msg);
};

// Schema built from def files
class DefSchema
{
public:
    static DefSchema &instance();

    // Load all def files and build schema
    // Returns false if loading failed
    bool load(const std::string &def_dir);

    // Check if cache is valid (all def files older than cache)
    bool is_cache_valid(const std::string &cache_path, const std::string &def_dir);

    // Load from binary cache
    bool load_cache(const std::string &cache_path);

    // Save to binary cache
    bool save_cache(const std::string &cache_path);

    // Lookup methods
    const MethodDef *lookup_method(const std::string &type_name, const std::string &method_name) const;
    // Event attributes for elements, and listener options (attr -> Coi type, e.g.
    // "pointerflags" -> "PointerFlags"); both derived from DOMElement's events
    const std::vector<ViewEventAttr> &view_event_attrs() const;
    const ViewEventAttr *find_view_event_attr(const std::string &attr) const;
    const std::map<std::string, std::string> &view_option_attrs() const;

    // C++ type a value of this Coi type must be cast to when passed to webcc (platform
    // enums and flags), or "" for anything else
    std::string webcc_cast_type(const std::string &type_name) const;

    // With arg_count: an exact match first, then a method whose trailing params have defaults
    const MethodDef *lookup_method(const std::string &type_name, const std::string &method_name, size_t arg_count) const;
    const TypeDef *lookup_type(const std::string &type_name) const;

    // Get all types
    const std::unordered_map<std::string, TypeDef> &types() const { return types_; }

    // Check if type inherits from another
    bool inherits_from(const std::string &derived, const std::string &base) const;

    // Check if a type is a handle (has methods defined in def files from webcc)
    bool is_handle(const std::string &type_name) const;

    // Check if a type is nocopy (can only be moved or referenced, not copied)
    // Returns true if the type or any of its parent types has @nocopy annotation
    bool is_nocopy(const std::string &type_name) const;

    // Resolve type alias (e.g., "int" -> "int32", "float" -> "float64")
    // Returns the canonical type name, or the input if not an alias
    std::string resolve_alias(const std::string &type_name) const;

    // Get namespace for a type (extracted from @map annotations)
    // e.g., "Canvas" -> "canvas", "DOMElement" -> "dom"
    std::string get_namespace_for_type(const std::string &type_name) const;

    // Lookup by @map value (for webcc function calls)
    // Returns the method that maps to "ns::func_name"
    const MethodDef *lookup_by_map(const std::string &ns, const std::string &func_name) const;

    // Convert camelCase to snake_case (e.g., "fillRect" -> "fill_rect")
    static std::string to_snake_case(const std::string &camel);

    // Lookup by snake_case function name (for compatibility with old SchemaLoader)
    // Returns method + namespace info, or nullptr if not found
    struct FuncLookupResult
    {
        std::string ns;        // namespace (e.g., "dom", "canvas")
        std::string type_name; // type that owns this method
        const MethodDef *method;
    };
    const FuncLookupResult *lookup_func(const std::string &snake_func_name) const;

private:
    std::unordered_map<std::string, TypeDef> types_;
    // Index for fast @map lookups: "ns::func" -> (type_name, method_def*)
    mutable std::unordered_map<std::string, std::pair<std::string, const MethodDef *>> map_index_;
    mutable bool map_index_built_ = false;
    void build_map_index() const;

    // Index for fast func name lookups: "snake_func" -> FuncLookupResult
    mutable std::unordered_map<std::string, FuncLookupResult> func_index_;
    mutable std::vector<ViewEventAttr> view_attrs_;
    mutable std::map<std::string, std::string> view_options_;
    mutable bool view_attrs_built_ = false;
    void build_view_attrs() const;
    mutable bool func_index_built_ = false;
    void build_func_index() const;

    bool loaded_ = false;
};
