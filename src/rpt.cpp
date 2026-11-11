#include "rpt.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace dayzlog {

namespace {

constexpr std::size_t kMaxMessage = 160;

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool contains(std::string_view s, std::string_view key) { return s.find(key) != std::string_view::npos; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// Tira o horário do começo: "18:05:46.436", e também " 6:03:12.123" (hora com espaço na frente).
std::string_view strip_clock(std::string_view s) {
    s = trim(s);
    std::size_t i = 0;
    while (i < s.size() && (is_digit(s[i]) || s[i] == ':' || s[i] == '.')) ++i;
    if (i >= 5 && s.substr(0, i).find(':') != std::string_view::npos) s.remove_prefix(i);
    return trim(s);
}

// Linha nova do log (começa com horário), e não continuação de um erro de várias linhas.
bool starts_with_clock(std::string_view s) {
    s = trim(s);
    return s.size() >= 5 && is_digit(s[0]) && (s[1] == ':' || (is_digit(s[1]) && s[2] == ':'));
}

enum class Kind { None, Error, Warning };

Kind classify(std::string_view line) {
    if (contains(line, "(E)") || contains(line, "ERROR") || contains(line, "Error:")) return Kind::Error;
    if (contains(line, "(W)") || contains(line, "WARNING") || contains(line, "Warning")) return Kind::Warning;
    return Kind::None;
}

}  // namespace

std::string normalize_message(const std::string& line) {
    std::string_view s = strip_clock(line);

    std::string out;
    out.reserve(s.size());
    bool keep_number = false;  // número grudado em letra faz parte do nome ("p3d", "x64")
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (is_digit(s[k])) {
            if (k == 0 || !is_digit(s[k - 1])) keep_number = k > 0 && std::isalpha(static_cast<unsigned char>(s[k - 1]));
            if (keep_number) {
                out += s[k];
            } else if (out.empty() || out.back() != '#') {
                out += '#';
            }
        } else if (s[k] == ' ' && !out.empty() && out.back() == ' ') {
            continue;  // "SCRIPT    (E)" e "SCRIPT (E)" viram a mesma coisa
        } else {
            out += s[k];
        }
    }
    if (out.size() > kMaxMessage) out = out.substr(0, kMaxMessage - 3) + "...";
    return out;
}

// "PHYSICS    (E): ..." -> "PHYSICS". Erros sem esse prefixo caem em "outros".
std::string error_system(const std::string& line) {
    std::string_view s = strip_clock(line);
    std::size_t e = s.find("(E)");
    if (e == std::string_view::npos) return "outros";
    std::string_view word = trim(s.substr(0, e));
    std::size_t space = word.find_last_of(' ');
    if (space != std::string_view::npos) word = word.substr(space + 1);
    return word.empty() ? "outros" : std::string(word);
}

void RptParser::feed(std::istream& in, const std::string& file) {
    report_.files++;
    file_ = file;
    line_ = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++line_;
        ++report_.lines;
        handle(line);
    }
    flush_exception();
}

void RptParser::handle(const std::string& line) {
    if (pending_.active) {
        std::string_view t = trim(line);
        if (!starts_with_clock(line)) {
            if (t.substr(0, 7) == "Reason:") {
                if (pending_.reason.empty()) pending_.reason = std::string(trim(t.substr(7)));
            } else if (t == "Stack trace:") {
                pending_.want_frame = true;
            } else if (pending_.want_frame && !t.empty()) {
                std::size_t fn = t.find(" Function");
                pending_.frame = std::string(t.substr(0, fn));
                flush_exception();
            }
            return;  // linhas de dentro do bloco não contam como erro separado
        }
        flush_exception();
    }

    Kind kind = classify(line);
    if (kind == Kind::None) return;

    if (kind == Kind::Error && contains(line, "Virtual Machine Exception")) {
        pending_ = PendingException{};
        pending_.active = true;
        pending_.line = line_;
        return;
    }
    add(kind == Kind::Error, normalize_message(line), error_system(line), line_);
}

void RptParser::flush_exception() {
    if (!pending_.active) return;
    std::string message = "SCRIPT (E): ";
    message += pending_.reason.empty() ? "Virtual Machine Exception" : normalize_message(pending_.reason);
    if (!pending_.frame.empty()) message += "  [" + pending_.frame + "]";
    add(true, message, "SCRIPT", pending_.line);
    pending_ = PendingException{};
}

void RptParser::add(bool error, const std::string& message, const std::string& system, int line) {
    auto& group = (error ? errors_ : warnings_)[message];
    if (group.count++ == 0) {
        group.first_file = file_;
        group.first_line = line;
    }
    if (error) {
        report_.errors++;
        report_.errors_by_system[system]++;
    } else {
        report_.warnings++;
    }
}

RptReport RptParser::finish(std::size_t top) {
    flush_exception();
    auto top_of = [top](const std::unordered_map<std::string, Group>& groups) {
        std::vector<RptIssue> issues;
        issues.reserve(groups.size());
        for (const auto& [message, g] : groups) issues.push_back({message, g.count, g.first_file, g.first_line});
        std::sort(issues.begin(), issues.end(), [](const RptIssue& a, const RptIssue& b) {
            if (a.count != b.count) return a.count > b.count;
            if (a.first_file != b.first_file) return a.first_file < b.first_file;
            return a.first_line < b.first_line;
        });
        if (issues.size() > top) issues.resize(top);
        return issues;
    };
    report_.top_errors = top_of(errors_);
    report_.top_warnings = top_of(warnings_);
    return std::move(report_);
}

RptReport parse_rpt(std::istream& in, std::size_t top) {
    RptParser parser;
    parser.feed(in, "");
    return parser.finish(top);
}

}  // namespace dayzlog
