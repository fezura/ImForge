#include "icon_table.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace imforge {
namespace builder {

namespace {

struct BuiltinIcon {
    const char* name;
    unsigned cp;
};

const BuiltinIcon kBuiltin[] = {
    {"fa-play", 0xf04b}, {"fa-pause", 0xf04c}, {"fa-stop", 0xf04d}, {"fa-forward", 0xf04e},
    {"fa-backward", 0xf04a}, {"fa-forward-step", 0xf051}, {"fa-backward-step", 0xf048},
    {"fa-circle-play", 0xf144}, {"fa-check", 0xf00c}, {"fa-xmark", 0xf00d}, {"fa-plus", 0x2b},
    {"fa-minus", 0xf068}, {"fa-circle-plus", 0xf055}, {"fa-circle-minus", 0xf056},
    {"fa-circle-check", 0xf058}, {"fa-circle-xmark", 0xf057}, {"fa-circle-info", 0xf05a},
    {"fa-circle-question", 0xf059}, {"fa-circle-exclamation", 0xf06a}, {"fa-triangle-exclamation", 0xf071},
    {"fa-ban", 0xf05e}, {"fa-trash", 0xf1f8}, {"fa-trash-can", 0xf2ed}, {"fa-pen", 0xf304},
    {"fa-pencil", 0xf303}, {"fa-pen-to-square", 0xf044}, {"fa-eraser", 0xf12d}, {"fa-copy", 0xf0c5},
    {"fa-paste", 0xf0ea}, {"fa-scissors", 0xf0c4}, {"fa-clone", 0xf24d}, {"fa-floppy-disk", 0xf0c7},
    {"fa-folder", 0xf07b}, {"fa-folder-open", 0xf07c}, {"fa-file", 0xf15b}, {"fa-file-code", 0xf1c9},
    {"fa-file-lines", 0xf15c}, {"fa-file-export", 0xf56e}, {"fa-file-import", 0xf56f},
    {"fa-download", 0xf019}, {"fa-upload", 0xf093}, {"fa-gear", 0xf013}, {"fa-gears", 0xf085},
    {"fa-wrench", 0xf0ad}, {"fa-screwdriver-wrench", 0xf7d9}, {"fa-sliders", 0xf1de},
    {"fa-toggle-on", 0xf205}, {"fa-toggle-off", 0xf204}, {"fa-magnifying-glass", 0xf002},
    {"fa-filter", 0xf0b0}, {"fa-arrow-left", 0xf060}, {"fa-arrow-right", 0xf061}, {"fa-arrow-up", 0xf062},
    {"fa-arrow-down", 0xf063}, {"fa-arrows-rotate", 0xf021}, {"fa-rotate", 0xf2f1},
    {"fa-rotate-left", 0xf2ea}, {"fa-rotate-right", 0xf2f9}, {"fa-chevron-left", 0xf053},
    {"fa-chevron-right", 0xf054}, {"fa-chevron-up", 0xf077}, {"fa-chevron-down", 0xf078},
    {"fa-caret-left", 0xf0d9}, {"fa-caret-right", 0xf0da}, {"fa-caret-up", 0xf0d8}, {"fa-caret-down", 0xf0d7},
    {"fa-angle-left", 0xf104}, {"fa-angle-right", 0xf105}, {"fa-angle-up", 0xf106}, {"fa-angle-down", 0xf107},
    {"fa-angles-left", 0xf100}, {"fa-angles-right", 0xf101}, {"fa-up-down-left-right", 0xf0b2},
    {"fa-arrows-up-down-left-right", 0xf047}, {"fa-arrows-left-right", 0xf337}, {"fa-arrows-up-down", 0xf07d},
    {"fa-expand", 0xf065}, {"fa-compress", 0xf066}, {"fa-maximize", 0xf31e}, {"fa-minimize", 0xf78c},
    {"fa-bars", 0xf0c9}, {"fa-ellipsis", 0xf141}, {"fa-ellipsis-vertical", 0xf142}, {"fa-grip", 0xf58d},
    {"fa-grip-vertical", 0xf58e}, {"fa-grip-lines", 0xf7a4}, {"fa-house", 0xf015}, {"fa-user", 0xf007},
    {"fa-users", 0xf0c0}, {"fa-user-secret", 0xf21b}, {"fa-user-ninja", 0xf504}, {"fa-circle-user", 0xf2bd},
    {"fa-lock", 0xf023}, {"fa-lock-open", 0xf3c1}, {"fa-unlock", 0xf09c}, {"fa-key", 0xf084},
    {"fa-shield", 0xf132}, {"fa-shield-halved", 0xf3ed}, {"fa-eye", 0xf06e}, {"fa-eye-slash", 0xf070},
    {"fa-eye-dropper", 0xf1fb}, {"fa-bell", 0xf0f3}, {"fa-bell-slash", 0xf1f6}, {"fa-star", 0xf005},
    {"fa-heart", 0xf004}, {"fa-bookmark", 0xf02e}, {"fa-flag", 0xf024}, {"fa-tag", 0xf02b}, {"fa-tags", 0xf02c},
    {"fa-thumbtack", 0xf08d}, {"fa-link", 0xf0c1}, {"fa-link-slash", 0xf127}, {"fa-code", 0xf121},
    {"fa-code-branch", 0xf126}, {"fa-terminal", 0xf120}, {"fa-bug", 0xf188}, {"fa-crosshairs", 0xf05b},
    {"fa-bullseye", 0xf140}, {"fa-gamepad", 0xf11b}, {"fa-keyboard", 0xf11c}, {"fa-computer-mouse", 0xf8cc},
    {"fa-desktop", 0xf390}, {"fa-display", 0xe163}, {"fa-laptop", 0xf109}, {"fa-mobile-screen", 0xf3cf},
    {"fa-microchip", 0xf2db}, {"fa-memory", 0xf538}, {"fa-server", 0xf233}, {"fa-database", 0xf1c0},
    {"fa-hard-drive", 0xf0a0}, {"fa-network-wired", 0xf6ff}, {"fa-wifi", 0xf1eb}, {"fa-globe", 0xf0ac},
    {"fa-cloud", 0xf0c2}, {"fa-plug", 0xf1e6}, {"fa-power-off", 0xf011}, {"fa-bolt", 0xf0e7},
    {"fa-fire", 0xf06d}, {"fa-skull", 0xf54c}, {"fa-skull-crossbones", 0xf714}, {"fa-ghost", 0xf6e2},
    {"fa-robot", 0xf544}, {"fa-rocket", 0xf135}, {"fa-bomb", 0xf1e2}, {"fa-gun", 0xe19b},
    {"fa-person-running", 0xf70c}, {"fa-person-walking", 0xf554}, {"fa-car", 0xf1b9}, {"fa-plane", 0xf072},
    {"fa-helicopter", 0xf533}, {"fa-jet-fighter", 0xf0fb}, {"fa-palette", 0xf53f}, {"fa-paintbrush", 0xf1fc},
    {"fa-brush", 0xf55d}, {"fa-fill-drip", 0xf576}, {"fa-droplet", 0xf043}, {"fa-font", 0xf031},
    {"fa-text-height", 0xf034}, {"fa-bold", 0xf032}, {"fa-italic", 0xf033}, {"fa-underline", 0xf0cd},
    {"fa-align-left", 0xf036}, {"fa-align-center", 0xf037}, {"fa-align-right", 0xf038},
    {"fa-align-justify", 0xf039}, {"fa-image", 0xf03e}, {"fa-images", 0xf302}, {"fa-camera", 0xf030},
    {"fa-video", 0xf03d}, {"fa-film", 0xf008}, {"fa-music", 0xf001}, {"fa-volume-high", 0xf028},
    {"fa-volume-xmark", 0xf6a9}, {"fa-microphone", 0xf130}, {"fa-headphones", 0xf025},
    {"fa-layer-group", 0xf5fd}, {"fa-object-group", 0xf247}, {"fa-object-ungroup", 0xf248},
    {"fa-vector-square", 0xf5cb}, {"fa-bezier-curve", 0xf55b}, {"fa-draw-polygon", 0xf5ee},
    {"fa-shapes", 0xf61f}, {"fa-crop", 0xf125}, {"fa-crop-simple", 0xf565}, {"fa-ruler", 0xf545},
    {"fa-ruler-combined", 0xf546}, {"fa-cube", 0xf1b2}, {"fa-cubes", 0xf1b3}, {"fa-box", 0xf466},
    {"fa-box-open", 0xf49e}, {"fa-box-archive", 0xf187}, {"fa-puzzle-piece", 0xf12e}, {"fa-list", 0xf03a},
    {"fa-list-ul", 0xf0ca}, {"fa-list-ol", 0xf0cb}, {"fa-list-check", 0xf0ae}, {"fa-table", 0xf0ce},
    {"fa-table-cells", 0xf00a}, {"fa-table-columns", 0xf0db}, {"fa-table-list", 0xf00b},
    {"fa-border-all", 0xf84c}, {"fa-border-none", 0xf850}, {"fa-window-maximize", 0xf2d0},
    {"fa-window-minimize", 0xf2d1}, {"fa-window-restore", 0xf2d2}, {"fa-square", 0xf0c8},
    {"fa-square-check", 0xf14a}, {"fa-square-plus", 0xf0fe}, {"fa-square-minus", 0xf146},
    {"fa-circle", 0xf111}, {"fa-circle-dot", 0xf192}, {"fa-circle-half-stroke", 0xf042},
    {"fa-circle-notch", 0xf1ce}, {"fa-spinner", 0xf110}, {"fa-hourglass", 0xf254}, {"fa-clock", 0xf017},
    {"fa-stopwatch", 0xf2f2}, {"fa-calendar", 0xf133}, {"fa-chart-line", 0xf201}, {"fa-chart-bar", 0xf080},
    {"fa-chart-pie", 0xf200}, {"fa-chart-area", 0xf1fe}, {"fa-wave-square", 0xf83e}, {"fa-gauge", 0xf624},
    {"fa-gauge-high", 0xf625}, {"fa-signal", 0xf012}, {"fa-battery-full", 0xf240},
    {"fa-battery-half", 0xf242}, {"fa-lightbulb", 0xf0eb}, {"fa-sun", 0xf185}, {"fa-moon", 0xf186},
    {"fa-location-dot", 0xf3c5}, {"fa-map", 0xf279}, {"fa-compass", 0xf14e}, {"fa-route", 0xf4d7},
    {"fa-diagram-project", 0xf542}, {"fa-sitemap", 0xf0e8}, {"fa-circle-nodes", 0xe4e2},
    {"fa-share", 0xf064}, {"fa-share-nodes", 0xf1e0}, {"fa-paper-plane", 0xf1d8}, {"fa-envelope", 0xf0e0},
    {"fa-comment", 0xf075}, {"fa-comments", 0xf086}, {"fa-reply", 0xf3e5}, {"fa-inbox", 0xf01c},
    {"fa-paperclip", 0xf0c6}, {"fa-thumbs-up", 0xf164}, {"fa-thumbs-down", 0xf165},
    {"fa-face-smile", 0xf118}, {"fa-face-frown", 0xf119}, {"fa-hand", 0xf256}, {"fa-hand-pointer", 0xf25a},
    {"fa-arrow-pointer", 0xf245}, {"fa-i-cursor", 0xf246}, {"fa-wand-magic-sparkles", 0xe2ca},
    {"fa-wand-magic", 0xf0d0}, {"fa-hat-wizard", 0xf6e8}, {"fa-dice", 0xf522}, {"fa-dice-d20", 0xf6cf},
    {"fa-trophy", 0xf091}, {"fa-medal", 0xf5a2}, {"fa-crown", 0xf521}, {"fa-award", 0xf559},
    {"fa-gift", 0xf06b}, {"fa-cart-shopping", 0xf07a}, {"fa-coins", 0xf51e}, {"fa-money-bill", 0xf0d6},
    {"fa-dollar-sign", 0x24}, {"fa-wallet", 0xf555}, {"fa-credit-card", 0xf09d}, {"fa-book", 0xf02d},
    {"fa-book-open", 0xf518}, {"fa-graduation-cap", 0xf19d}, {"fa-clipboard", 0xf328},
    {"fa-clipboard-list", 0xf46d}, {"fa-clipboard-check", 0xf46c}, {"fa-flask", 0xf0c3}, {"fa-atom", 0xf5d2},
    {"fa-brain", 0xf5dc}, {"fa-heart-pulse", 0xf21e}, {"fa-syringe", 0xf48e}, {"fa-pills", 0xf484},
    {"fa-kit-medical", 0xf479}, {"fa-radiation", 0xf7b9}, {"fa-biohazard", 0xf780}, {"fa-fan", 0xf863},
    {"fa-temperature-half", 0xf2c9}, {"fa-snowflake", 0xf2dc}, {"fa-leaf", 0xf06c}, {"fa-tree", 0xf1bb},
    {"fa-paw", 0xf1b0}, {"fa-cat", 0xf6be}, {"fa-dog", 0xf6d3}, {"fa-dragon", 0xf6d5}, {"fa-fish", 0xf578},
    {"fa-anchor", 0xf13d}, {"fa-mountain", 0xf6fc}, {"fa-water", 0xf773}, {"fa-wind", 0xf72e},
    {"fa-umbrella", 0xf0e9}, {"fa-cloud-rain", 0xf73d}, {"fa-cloud-bolt", 0xf76c}, {"fa-rainbow", 0xf75b},
    {"fa-hashtag", 0x23}, {"fa-at", 0x40}, {"fa-asterisk", 0x2a}, {"fa-percent", 0x25}, {"fa-equals", 0x3d},
    {"fa-not-equal", 0xf53e}, {"fa-less-than", 0x3c}, {"fa-greater-than", 0x3e}, {"fa-question", 0x3f},
    {"fa-exclamation", 0x21}, {"fa-info", 0xf129}, {"fa-infinity", 0xf534}, {"fa-divide", 0xf529},
    {"fa-sort", 0xf0dc}, {"fa-sort-up", 0xf0de}, {"fa-sort-down", 0xf0dd}, {"fa-shuffle", 0xf074},
    {"fa-repeat", 0xf363}, {"fa-retweet", 0xf079}, {"fa-recycle", 0xf1b8}, {"fa-right-from-bracket", 0xf2f5},
    {"fa-right-to-bracket", 0xf2f6}, {"fa-arrow-up-right-from-square", 0xf08e},
    {"fa-up-right-and-down-left-from-center", 0xf424}, {"fa-id-card", 0xf2c2}, {"fa-fingerprint", 0xf577},
    {"fa-qrcode", 0xf029}, {"fa-barcode", 0xf02a}, {"fa-print", 0xf02f}, {"fa-language", 0xf1ab},
    {"fa-quote-left", 0xf10d}, {"fa-quote-right", 0xf10e}, {"fa-heading", 0xf1dc}, {"fa-paragraph", 0xf1dd},
    {"fa-indent", 0xf03c}, {"fa-outdent", 0xf03b}, {"fa-mask", 0xf6fa}, {"fa-glasses", 0xf530},
    {"fa-hand-fist", 0xf6de}, {"fa-person-rifle", 0xe54e}, {"fa-explosion", 0xe4e9}, {"fa-meteor", 0xf753},
    {"fa-fire-flame-curved", 0xf7e4}, {"fa-bullhorn", 0xf0a1}, {"fa-flag-checkered", 0xf11e},
    {"fa-stamp", 0xf5bf}, {"fa-swatchbook", 0xf5c3}, {"fa-magnet", 0xf076}, {"fa-hammer", 0xf6e3},
    {"fa-toolbox", 0xf552}, {"fa-cash-register", 0xf788}, {"fa-receipt", 0xf543},
    {"fa-scale-balanced", 0xf24e}, {"fa-building", 0xf1ad}, {"fa-city", 0xf64f}, {"fa-store", 0xf54e},
    {"fa-truck", 0xf0d1}, {"fa-ship", 0xf21a}, {"fa-satellite", 0xf7bf}, {"fa-satellite-dish", 0xf7c0},
    {"fa-futbol", 0xf1e3}, {"fa-dumbbell", 0xf44b}, {"fa-ticket", 0xf145}, {"fa-compact-disc", 0xf51f},
    {"fa-vr-cardboard", 0xf729}, {"fa-icons", 0xf86d}, {"fa-certificate", 0xf0a3}, {"fa-diamond", 0xf219},
    {"fa-spray-can", 0xf5bd}, {"fa-highlighter", 0xf591}, {"fa-marker", 0xf5a1}, {"fa-pen-ruler", 0xf5ae},
    {"fa-pen-nib", 0xf5ad}, {"fa-pen-fancy", 0xf5ac}, {"fa-compass-drafting", 0xf568},
    {"fa-arrows-to-dot", 0xe4be}, {"fa-mug-hot", 0xf7b6}, {"fa-pizza-slice", 0xf818}, {"fa-burger", 0xf805},
    {"fa-beer-mug-empty", 0xf0fc}, {"fa-utensils", 0xf2e7}, {"fa-cookie-bite", 0xf564},
    {"fa-apple-whole", 0xf5d1}, {"fa-seedling", 0xf4d8}, {"fa-poo", 0xf2fe},
    {"fa-arrow-up-1-9", 0xf163}, {"fa-bars-progress", 0xf828}, {"fa-calculator", 0xf1ec},
    {"fa-chart-column", 0xe0e3}, {"fa-check-square", 0xf14a}, {"fa-circle-arrow-right", 0xf0a9},
    {"fa-folder-tree", 0xf802}, {"fa-grip-lines-vertical", 0xf7a5}, {"fa-message", 0xf27a},
    {"fa-square-caret-down", 0xf150}, {"fa-turn-down", 0xf3be},
};

struct TableState {
    std::vector<IconEntry> entries;
    std::vector<std::string> search_dirs;
    std::string source;
    unsigned min_cp = 0xe005, max_cp = 0xf8ff;
    bool initialized = false;
};

TableState& State() {
    static TableState s;
    return s;
}

void SortEntries(std::vector<IconEntry>& v) {
    std::sort(v.begin(), v.end(), [](const IconEntry& a, const IconEntry& b) { return a.name < b.name; });
}

void LoadBuiltin(TableState& s) {
    s.entries.clear();
    for (const BuiltinIcon& b : kBuiltin) {
        IconEntry e;
        e.name = b.name;
        e.codepoint = b.cp;
        e.utf8 = Utf8FromCodepoint(b.cp);
        s.entries.push_back(std::move(e));
    }
    SortEntries(s.entries);
    s.source = "builtin";
    s.min_cp = 0xe005;
    s.max_cp = 0xf8ff;
}

std::string MacroToIconName(const std::string& macro) {
    std::string out = "fa-";
    for (char c : macro) {
        if (c == '_') out.push_back('-');
        else if (c >= 'A' && c <= 'Z') out.push_back((char)(c - 'A' + 'a'));
        else out.push_back(c);
    }
    return out;
}

bool DecodeStringLiteral(const std::string& line, size_t start, std::string& out) {
    size_t q = line.find('"', start);
    if (q == std::string::npos) return false;
    out.clear();
    for (size_t i = q + 1; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') return !out.empty();
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i + 1 >= line.size()) return false;
        char e = line[++i];
        if (e == 'x') {
            unsigned v = 0;
            int digits = 0;
            while (i + 1 < line.size() && digits < 2) {
                char h = line[i + 1];
                int n = (h >= '0' && h <= '9') ? h - '0'
                        : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                        : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                if (n < 0) break;
                v = v * 16 + (unsigned)n;
                ++i;
                ++digits;
            }
            if (digits == 0) return false;
            out.push_back((char)(unsigned char)v);
        } else if (e >= '0' && e <= '7') {
            unsigned v = (unsigned)(e - '0');
            int digits = 1;
            while (i + 1 < line.size() && digits < 3 && line[i + 1] >= '0' && line[i + 1] <= '7') {
                v = v * 8 + (unsigned)(line[++i] - '0');
                ++digits;
            }
            out.push_back((char)(unsigned char)v);
        } else if (e == 'n' || e == 't' || e == 'r') {
            return false;
        } else {
            out.push_back(e);
        }
    }
    return false;
}

bool ParseHeaderText(const std::string& text, TableState& s) {
    std::vector<IconEntry> entries;
    unsigned min_cp = 0, max_cp = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        size_t d = line.find("#define");
        if (d == std::string::npos) continue;
        size_t p = d + 7;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
        size_t name_end = p;
        while (name_end < line.size() && (std::isalnum((unsigned char)line[name_end]) || line[name_end] == '_'))
            ++name_end;
        std::string macro = line.substr(p, name_end - p);
        if (macro.compare(0, 9, "ICON_MIN_") == 0 || macro.compare(0, 9, "ICON_MAX_") == 0) {
            unsigned v = (unsigned)std::strtoul(line.c_str() + name_end, nullptr, 0);
            if (macro.compare(0, 9, "ICON_MIN_") == 0) min_cp = v;
            else if (max_cp == 0 || macro.find("16") != std::string::npos) max_cp = v;
            continue;
        }
        if (macro.compare(0, 8, "ICON_FA_") != 0) continue;
        IconEntry e;
        if (!DecodeStringLiteral(line, name_end, e.utf8)) continue;
        e.name = MacroToIconName(macro.substr(8));
        e.codepoint = CodepointFromUtf8(e.utf8.c_str());
        entries.push_back(std::move(e));
    }
    if (entries.size() < 16) return false;
    SortEntries(entries);
    s.entries = std::move(entries);
    if (min_cp) s.min_cp = min_cp;
    if (max_cp) s.max_cp = max_cp;
    return true;
}

bool ReadWholeFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

void AutoLoad(TableState& s) {
    s.initialized = true;
    std::vector<std::string> dirs = s.search_dirs;
    if (const char* env = std::getenv("IMFORGE_FONTS_DIR")) dirs.push_back(env);
    const char* rel[] = {"fonts", "../fonts", "../../fonts", "../share/imforge/fonts", "third_party/fonts",
                         "_deps/fonts-src", "build/_deps/fonts-src"};
    for (const char* r : rel) dirs.push_back(r);
    for (const std::string& dir : dirs) {
        std::error_code ec;
        std::filesystem::path p = std::filesystem::u8path(dir) / "IconsFontAwesome6.h";
        if (std::filesystem::exists(p, ec) && LoadIconTableFromHeader(p.u8string())) return;
    }
    LoadBuiltin(s);
}

TableState& Ready() {
    TableState& s = State();
    if (!s.initialized) AutoLoad(s);
    return s;
}

}  // namespace

std::string Utf8FromCodepoint(unsigned cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

unsigned CodepointFromUtf8(const char* s) {
    if (!s || !*s) return 0;
    const unsigned char* p = (const unsigned char*)s;
    if (p[0] < 0x80) return p[0];
    if ((p[0] & 0xE0) == 0xC0 && p[1]) return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2])
        return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3])
        return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    return 0;
}

const IconEntry* IconTable(size_t& count) {
    TableState& s = Ready();
    count = s.entries.size();
    return s.entries.data();
}

const IconEntry* FindIcon(const char* name) {
    if (!name || !*name) return nullptr;
    std::string key = name;
    if (key.compare(0, 6, "{icon:") == 0 && key.back() == '}') key = key.substr(6, key.size() - 7);
    if (key.compare(0, 3, "fa-") != 0) key = "fa-" + key;
    TableState& s = Ready();
    auto it = std::lower_bound(s.entries.begin(), s.entries.end(), key,
                               [](const IconEntry& e, const std::string& k) { return e.name < k; });
    if (it != s.entries.end() && it->name == key) return &*it;
    return nullptr;
}

const char* IconUtf8(const char* name) {
    const IconEntry* e = FindIcon(name);
    return e ? e->utf8.c_str() : nullptr;
}

bool LoadIconTableFromHeader(const std::string& header_path) {
    TableState& s = State();
    std::string text;
    if (!ReadWholeFile(header_path, text)) return false;
    if (!ParseHeaderText(text, s)) return false;
    s.source = header_path;
    s.initialized = true;
    return true;
}

void AddIconHeaderSearchDir(const std::string& dir) {
    TableState& s = State();
    s.search_dirs.push_back(dir);
    if (s.initialized && s.source == "builtin") s.initialized = false;
}

const std::string& IconTableSource() { return Ready().source; }

void IconCodepointRange(unsigned& min_out, unsigned& max_out) {
    TableState& s = Ready();
    min_out = s.min_cp;
    max_out = s.max_cp;
}

}  // namespace builder
}  // namespace imforge
