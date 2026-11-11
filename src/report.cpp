#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace dayzlog {

namespace {

template <typename T>
using Ranking = std::vector<std::pair<std::string, T>>;

// Largura na tela de um texto UTF-8 (conta caracteres, não bytes), pra alinhar nomes com acento.
std::size_t display_width(const std::string& s) {
    std::size_t width = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++width;
    return width;
}

std::string pad(const std::string& s, std::size_t width) {
    std::size_t w = display_width(s);
    return w >= width ? s : s + std::string(width - w, ' ');
}

template <typename T>
std::size_t name_width(const Ranking<T>& rows) {
    std::size_t width = 0;
    for (const auto& row : rows) width = std::max(width, display_width(row.first));
    return width;
}

template <typename T, typename Key>
Ranking<T> top_by(Ranking<T> rows, std::size_t top, Key key) {
    std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) { return key(a.second) > key(b.second); });
    if (rows.size() > top) rows.resize(top);
    return rows;
}

Ranking<int> count_ranking(const std::map<std::string, int>& counts, std::size_t top) {
    return top_by(Ranking<int>(counts.begin(), counts.end()), top, [](int n) { return n; });
}

std::string number(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    return buffer;
}

struct AdmView {
    std::map<std::string, std::string> names;
    int pvp_kills = 0;
    int ai_kills = 0;
    long long seconds_played = 0;
    std::map<std::string, int> weapons;
    std::map<std::string, int> causes;
    const Kill* longest_pvp = nullptr;
    Ranking<PlayerStats> killers;
    Ranking<PlayerStats> deaths;
    Ranking<long long> online;
    Ranking<DamageSource> damage;
};

AdmView view_of(const AdmReport& r, std::size_t top) {
    AdmView v;
    v.names = display_names(r);
    for (const auto& kill : r.kills) {
        (kill.by_ai ? v.ai_kills : v.pvp_kills)++;
        v.weapons[kill.weapon]++;
        if (!kill.by_ai && kill.distance >= 0 && (!v.longest_pvp || kill.distance > v.longest_pvp->distance))
            v.longest_pvp = &kill;
    }
    for (const auto& death : r.deaths) v.causes[death.cause.empty() ? "desconhecida" : death.cause]++;

    Ranking<PlayerStats> killers, deaths;
    Ranking<long long> online;
    for (const auto& [key, s] : r.players) {
        const std::string& name = v.names[key];
        v.seconds_played += s.seconds_online;
        if (s.kills > 0) killers.emplace_back(name, s);
        if (s.deaths > 0) deaths.emplace_back(name, s);
        if (s.seconds_online > 0) online.emplace_back(name, s.seconds_online);
    }
    v.killers = top_by(killers, top, [](const PlayerStats& s) { return s.kills * 1000 - s.deaths; });
    v.deaths = top_by(deaths, top, [](const PlayerStats& s) { return s.deaths; });
    v.online = top_by(online, top, [](long long seconds) { return seconds; });
    v.damage = top_by(Ranking<DamageSource>(r.damage_taken.begin(), r.damage_taken.end()), top,
                      [](const DamageSource& d) { return d.hits; });
    return v;
}

void write_counts(std::ostringstream& out, const char* title, const Ranking<int>& rows) {
    if (rows.empty()) return;
    std::size_t width = name_width(rows);
    out << "\n  " << title << "\n";
    for (const auto& [name, count] : rows) out << "    " << pad(name, width) << "  " << std::setw(5) << count << "\n";
}

void json_counts(std::ostringstream& out, const Ranking<int>& rows) {
    out << "{";
    for (std::size_t i = 0; i < rows.size(); ++i)
        out << (i ? ", " : "") << "\"" << json_escape(rows[i].first) << "\": " << rows[i].second;
    out << "}";
}

double kd(const PlayerStats& s) { return static_cast<double>(s.kills) / std::max(s.deaths, 1); }

}  // namespace

std::map<std::string, std::string> display_names(const AdmReport& r) {
    std::map<std::string, int> uses;
    for (const auto& entry : r.players) uses[entry.second.name]++;
    std::map<std::string, std::string> names;
    for (const auto& [key, s] : r.players) {
        bool shared = uses[s.name] > 1 && key.rfind("nome:", 0) != 0;
        names[key] = shared ? s.name + " (" + key.substr(0, 4) + ")" : s.name;
    }
    return names;
}

std::string format_duration(long long seconds) {
    long long hours = seconds / 3600;
    long long minutes = (seconds % 3600) / 60;
    if (hours == 0) return std::to_string(minutes) + "min";
    return std::to_string(hours) + "h " + (minutes < 10 ? "0" : "") + std::to_string(minutes) + "min";
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string adm_text(const AdmReport& r, std::size_t top) {
    AdmView v = view_of(r, top);
    std::ostringstream out;

    out << "Logs de administração (.ADM): " << r.files << (r.files == 1 ? " arquivo" : " arquivos");
    if (!r.first_date.empty())
        out << (r.first_date == r.last_date ? ", " + r.first_date : ", de " + r.first_date + " a " + r.last_date);
    out << "\n";
    out << "  " << r.lines << " linhas · " << r.players.size() << " jogadores · pico de " << r.peak_online
        << " online · " << format_duration(v.seconds_played) << " jogadas no total\n";
    int chat = 0;
    for (const auto& entry : r.players) chat += entry.second.chat_messages;
    out << "  " << r.deaths.size() << " mortes de jogadores (" << v.pvp_kills << " PvP, " << v.ai_kills
        << " pela IA) · " << chat << (chat == 1 ? " mensagem" : " mensagens") << " no chat\n";

    write_counts(out, "Causas de morte", count_ranking(v.causes, top));

    if (!v.deaths.empty()) {
        Ranking<int> rows;
        for (const auto& [name, s] : v.deaths) rows.emplace_back(name, s.deaths);
        write_counts(out, "Quem mais morreu", rows);
    }

    write_counts(out, "Armas que mais mataram jogadores (PvP e IA)", count_ranking(v.weapons, top));

    if (!v.damage.empty()) {
        std::size_t width = name_width(v.damage);
        out << "\n  O que mais acertou os jogadores\n";
        for (const auto& [source, d] : v.damage)
            out << "    " << pad(source, width) << "  " << std::setw(5) << d.hits << " acertos  " << std::setw(7)
                << std::lround(d.damage) << " de dano\n";
    }

    if (!v.killers.empty()) {
        std::size_t width = name_width(v.killers);
        out << "\n  Quem mais matou (PvP)\n    " << pad("", width) << "  kills  mortes   K/D  acertos  na cabeça\n";
        for (const auto& [name, s] : v.killers) {
            out << "    " << pad(name, width) << "  " << std::setw(5) << s.kills << "  " << std::setw(6) << s.deaths
                << "  " << std::setw(4) << number(kd(s)) << "  " << std::setw(7) << s.hits << "  " << std::setw(9)
                << s.headshots << "\n";
        }
    }
    if (const Kill* best = v.longest_pvp) {
        out << "\n  Tiro mais longo (PvP): " << v.names[best->killer] << " matou " << v.names[best->victim] << " com "
            << best->weapon << " a " << number(best->distance) << " m\n";
    }

    if (!v.online.empty()) {
        std::size_t width = name_width(v.online);
        out << "\n  Mais tempo online\n";
        for (const auto& [name, seconds] : v.online)
            out << "    " << pad(name, width) << "  " << std::setw(10) << format_duration(seconds) << "\n";
    }

    out << "\n  Também no log: " << r.ai_lines << " linhas de IA, " << r.mod_lines << " de mods, " << r.unrecognized
        << " não reconhecidas\n";
    return out.str();
}

std::string rpt_text(const RptReport& r) {
    std::ostringstream out;
    out << "Logs de script e engine (.RPT): " << r.files << (r.files == 1 ? " arquivo" : " arquivos") << "\n";
    out << "  " << r.lines << " linhas · " << r.errors << " erros · " << r.warnings << " avisos\n";

    if (!r.errors_by_system.empty()) {
        out << "  Erros por sistema:";
        bool first = true;
        for (const auto& [system, count] : count_ranking(r.errors_by_system, r.errors_by_system.size())) {
            out << (first ? " " : " · ") << system << " " << count;
            first = false;
        }
        out << "\n";
    }

    auto section = [&](const char* title, const std::vector<RptIssue>& issues) {
        if (issues.empty()) return;
        out << "\n  " << title << "\n";
        for (const auto& issue : issues) {
            out << "    " << std::setw(6) << issue.count << "x  " << issue.message << "\n";
            out << "             primeira vez: " << (issue.first_file.empty() ? "" : issue.first_file + ", ")
                << "linha " << issue.first_line << "\n";
        }
    };
    section("Erros que mais se repetem", r.top_errors);
    section("Avisos que mais se repetem", r.top_warnings);
    if (r.errors == 0 && r.warnings == 0) out << "  nenhum erro ou aviso\n";
    return out.str();
}

std::string adm_json(const AdmReport& r, std::size_t top) {
    AdmView v = view_of(r, top);
    std::ostringstream out;
    out << "{\"type\": \"adm\", \"files\": " << r.files << ", \"first_date\": \"" << r.first_date
        << "\", \"last_date\": \"" << r.last_date << "\", \"lines\": " << r.lines << ", \"players\": " << r.players.size()
        << ", \"peak_online\": " << r.peak_online << ", \"seconds_played\": " << v.seconds_played
        << ", \"deaths\": " << r.deaths.size() << ", \"pvp_kills\": " << v.pvp_kills << ", \"ai_kills\": " << v.ai_kills;

    out << ", \"death_causes\": ";
    json_counts(out, count_ranking(v.causes, top));
    out << ", \"weapons\": ";
    json_counts(out, count_ranking(v.weapons, top));

    out << ", \"damage_taken\": {";
    for (std::size_t i = 0; i < v.damage.size(); ++i)
        out << (i ? ", " : "") << "\"" << json_escape(v.damage[i].first) << "\": {\"hits\": " << v.damage[i].second.hits
            << ", \"damage\": " << number(v.damage[i].second.damage) << "}";
    out << "}";

    auto player_list = [&](const char* field, const Ranking<PlayerStats>& rows) {
        out << ", \"" << field << "\": [";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& [name, s] = rows[i];
            out << (i ? ", " : "") << "{\"player\": \"" << json_escape(name) << "\", \"kills\": " << s.kills
                << ", \"deaths\": " << s.deaths << ", \"hits\": " << s.hits << ", \"headshots\": " << s.headshots
                << ", \"seconds_online\": " << s.seconds_online << "}";
        }
        out << "]";
    };
    player_list("top_killers", v.killers);
    player_list("most_deaths", v.deaths);

    out << ", \"longest_pvp_kill\": ";
    if (const Kill* best = v.longest_pvp) {
        out << "{\"killer\": \"" << json_escape(v.names[best->killer]) << "\", \"victim\": \""
            << json_escape(v.names[best->victim]) << "\", \"weapon\": \"" << json_escape(best->weapon)
            << "\", \"meters\": " << number(best->distance) << "}";
    } else {
        out << "null";
    }
    out << ", \"ai_lines\": " << r.ai_lines << ", \"mod_lines\": " << r.mod_lines
        << ", \"unrecognized\": " << r.unrecognized << "}";
    return out.str();
}

std::string rpt_json(const RptReport& r) {
    std::ostringstream out;
    auto issues = [&](const std::vector<RptIssue>& list) {
        out << "[";
        for (std::size_t i = 0; i < list.size(); ++i)
            out << (i ? ", " : "") << "{\"message\": \"" << json_escape(list[i].message)
                << "\", \"count\": " << list[i].count << ", \"first_file\": \"" << json_escape(list[i].first_file)
                << "\", \"first_line\": " << list[i].first_line << "}";
        out << "]";
    };
    out << "{\"type\": \"rpt\", \"files\": " << r.files << ", \"lines\": " << r.lines << ", \"errors\": " << r.errors
        << ", \"warnings\": " << r.warnings << ", \"errors_by_system\": ";
    json_counts(out, count_ranking(r.errors_by_system, r.errors_by_system.size()));
    out << ", \"top_errors\": ";
    issues(r.top_errors);
    out << ", \"top_warnings\": ";
    issues(r.top_warnings);
    out << "}";
    return out.str();
}

}  // namespace dayzlog
