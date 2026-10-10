#include "../cli/log.h"
#include "pwa_generator.h"
#include "../ast/component/component.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    std::string json_str(const std::string &s)
    {
        std::string out = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
            else if (c == '\n') out += "\\n";
            else if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
            else out += (char)c;
        }
        return out + "\"";
    }

    std::string html_attr(const std::string &s)
    {
        std::string out;
        for (char c : s)
        {
            if (c == '"') out += "&quot;";
            else if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else out += c;
        }
        return out;
    }

    // Path relative to the page, without a leading "./" or "/"
    std::string rel(std::string p)
    {
        while (p.rfind("./", 0) == 0) p = p.substr(2);
        while (!p.empty() && p[0] == '/') p = p.substr(1);
        return p;
    }

    std::string ext_of(const std::string &p)
    {
        std::string e = fs::path(p).extension().string();
        std::transform(e.begin(), e.end(), e.begin(), ::tolower);
        return e;
    }

    std::string icon_mime(const std::string &p)
    {
        std::string e = ext_of(p);
        if (e == ".png") return "image/png";
        if (e == ".svg") return "image/svg+xml";
        if (e == ".webp") return "image/webp";
        if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
        if (e == ".ico") return "image/x-icon";
        return "";
    }

    // Width and height from a PNG's IHDR chunk
    bool png_size(const fs::path &file, uint32_t &w, uint32_t &h)
    {
        std::ifstream in(file, std::ios::binary);
        unsigned char b[24];
        if (!in.read((char *)b, 24)) return false;
        if (b[0] != 0x89 || b[1] != 'P' || b[2] != 'N' || b[3] != 'G') return false;
        w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
        h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
        return true;
    }

    // URL path segment escaping for the precache list
    std::string url_path(const std::string &p)
    {
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : p)
        {
            if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
            else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
        }
        return out;
    }
}

std::string pwa_head_tags(const AppConfig &config)
{
    std::ostringstream o;
    if (!config.icon.empty())
    {
        std::string mime = icon_mime(config.icon);
        o << "    <link rel=\"icon\" href=\"./" << html_attr(rel(config.icon)) << "\"";
        if (!mime.empty()) o << " type=\"" << mime << "\"";
        o << ">\n";
    }
    else
    {
        o << "    <link rel=\"icon\" href=\"data:,\">\n";
    }
    if (!config.theme.empty())
        o << "    <meta name=\"theme-color\" content=\"" << html_attr(config.theme) << "\">\n";
    if (config.pwa)
    {
        o << "    <link rel=\"manifest\" href=\"./manifest.webmanifest\">\n";
        if (!config.icon.empty())
            o << "    <link rel=\"apple-touch-icon\" href=\"./" << html_attr(rel(config.icon)) << "\">\n";
        // skipped under coi dev; update() checks for a new build right away
        o << "    <script>if ('serviceWorker' in navigator) addEventListener('load', () => { if (!window.__coi_dev) navigator.serviceWorker.register('./sw.js').then(r => r.update()).catch(e => console.warn('[Coi] service worker not registered:', e)); });</script>\n";
    }
    return o.str();
}

void generate_pwa_files(const fs::path &out_dir, const AppConfig &config)
{
    std::string name = config.title.empty() ? "Coi App" : config.title;
    std::string base = config.base.empty() ? "/" : config.base;

    // --- manifest ---
    std::ostringstream m;
    m << "{\n";
    m << "  \"name\": " << json_str(name) << ",\n";
    m << "  \"short_name\": " << json_str(name) << ",\n";
    if (!config.description.empty())
        m << "  \"description\": " << json_str(config.description) << ",\n";
    m << "  \"lang\": " << json_str(config.lang.empty() ? "en" : config.lang) << ",\n";
    m << "  \"id\": " << json_str(base) << ",\n";
    m << "  \"start_url\": " << json_str(base) << ",\n";
    m << "  \"scope\": " << json_str(base) << ",\n";
    m << "  \"display\": \"standalone\",\n";
    if (!config.theme.empty())
    {
        m << "  \"theme_color\": " << json_str(config.theme) << ",\n";
        m << "  \"background_color\": " << json_str(config.theme) << ",\n";
    }
    m << "  \"icons\": [";
    if (!config.icon.empty())
    {
        std::string icon = rel(config.icon);
        std::string mime = icon_mime(icon);
        std::string sizes = "any";
        uint32_t w = 0, h = 0;
        if (mime == "image/png")
        {
            fs::path f = out_dir / icon;
            if (!fs::exists(f)) f = fs::current_path() / icon;
            if (png_size(f, w, h))
            {
                sizes = std::to_string(w) + "x" + std::to_string(h);
                if (w < 192 || h < 192)
                    std::cerr << "[Coi] Warning: app icon is " << sizes << "; browsers want at least 192x192 (512x512 for the install screen)" << std::endl;
            }
            else
            {
                std::cerr << "[Coi] Warning: app icon '" << icon << "' not found or not a PNG; the app may not be installable" << std::endl;
            }
        }
        m << "\n    { \"src\": " << json_str(base + icon) << ", \"sizes\": \"" << sizes << "\"";
        if (!mime.empty()) m << ", \"type\": \"" << mime << "\"";
        m << ", \"purpose\": \"any\" }\n  ";
    }
    else
    {
        std::cerr << "[Coi] Warning: app.pwa without app.icon; browsers only offer to install apps with an icon" << std::endl;
    }
    m << "]\n}\n";
    std::ofstream(out_dir / "manifest.webmanifest") << m.str();

    // --- precache list and build hash ---
    std::vector<std::string> files;
    for (const auto &entry : fs::recursive_directory_iterator(out_dir))
    {
        if (!entry.is_regular_file()) continue;
        std::string r = fs::relative(entry.path(), out_dir).generic_string();
        if (r == "sw.js" || r[0] == '.' || r.find("/.") != std::string::npos) continue;
        files.push_back(r);
    }
    std::sort(files.begin(), files.end());

    uint64_t hash = 1469598103934665603ull; // FNV-1a
    auto mix = [&](const char *p, size_t n) { for (size_t i = 0; i < n; i++) { hash ^= (unsigned char)p[i]; hash *= 1099511628211ull; } };
    for (const auto &f : files)
    {
        mix(f.data(), f.size() + 1);
        std::ifstream in(out_dir / f, std::ios::binary);
        std::vector<char> buf(1 << 16);
        while (in.read(buf.data(), buf.size()) || in.gcount() > 0)
            mix(buf.data(), (size_t)in.gcount());
    }
    char version[17];
    snprintf(version, sizeof version, "%016llx", (unsigned long long)hash);

    // --- service worker ---
    std::ostringstream s;
    s << "// Generated by Coi\n";
    s << "const CACHE = 'coi-" << version << "';\n";
    s << "const FILES = ['./'";
    for (const auto &f : files)
        s << ", './" << url_path(f) << "'";
    s << "];\n";
    s << R"JS(self.addEventListener('install', (e) => {
    e.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES.map((f) => new Request(f, { cache: 'reload' })))).then(() => self.skipWaiting()));
});
self.addEventListener('activate', (e) => {
    e.waitUntil(caches.keys().then((keys) => Promise.all(keys.filter((k) => k.startsWith('coi-') && k !== CACHE).map((k) => caches.delete(k)))).then(() => self.clients.claim()));
});
self.addEventListener('fetch', (e) => {
    const req = e.request;
    if (req.method !== 'GET' || new URL(req.url).origin !== location.origin) return;
    // cache first for built files, app for routes, rest to the network
    e.respondWith(caches.open(CACHE).then((c) => c.match(req).then((hit) => hit || (req.mode === 'navigate' ? c.match('./') : undefined))).then((hit) => hit || fetch(req)));
});
)JS";
    std::ofstream(out_dir / "sw.js") << s.str();
    if (g_verbose) std::cout << "[Coi] PWA: manifest.webmanifest, sw.js (" << files.size() << " files cached, version " << version << ")" << std::endl;
}
