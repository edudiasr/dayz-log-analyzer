#include "adm.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace dayzlog {

namespace {

constexpr long long kDay = 24 * 3600;
constexpr auto npos = std::string::npos;

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool starts_with(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

bool contains(std::string_view s, std::string_view key) { return s.find(key) != std::string_view::npos; }

// "HH:MM:SS" no começo do texto -> segundos.
bool parse_clock(std::string_view s, long long& out) {
    if (s.size() < 8 || s[2] != ':' || s[5] != ':') return false;
    for (int i : {0, 1, 3, 4, 6, 7})
        if (!is_digit(s[i])) return false;
    auto two = [&](int i) { return (s[i] - '0') * 10 + (s[i + 1] - '0'); };
    out = two(0) * 3600LL + two(3) * 60LL + two(6);
    return true;
}

// Dias desde 1970-01-01 (algoritmo de Howard Hinnant).
long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// Lê `"Nome" (id=XXXX ...` a partir das aspas de abertura.
bool ref_at(const std::string& s, std::size_t open_quote, PlayerRef& out) {
    std::size_t close = s.find('"', open_quote + 1);
    if (close == npos) return false;
    out.name = s.substr(open_quote + 1, close - open_quote - 1);
    out.end = close + 1;

    // o id tem que vir antes das próximas aspas, senão é de outra pessoa na mesma linha
    std::size_t id = s.find("(id=", out.end);
    std::size_t next_quote = s.find('"', out.end);
    if (id != npos && (next_quote == npos || id < next_quote)) {
        std::size_t start = id + 4;
        std::size_t stop = s.find_first_of(" )", start);
        if (stop == npos) stop = s.size();
        out.key = s.substr(start, stop - start);
        out.end = stop;
    }
    if (out.key.empty()) out.key = "nome:" + out.name;
    return true;
}

bool read_player(const std::string& s, std::size_t from, PlayerRef& out) {
    std::size_t at = s.find("Player \"", from);
    return at != npos && ref_at(s, at + 7, out);
}

std::string trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.remove_suffix(1);
    return std::string(s);
}

double number_after(const std::string& s, std::size_t at) {
    char* end = nullptr;
    double value = std::strtod(s.c_str() + at, &end);
    return end == s.c_str() + at ? -1 : value;
}

// "killed by ZmbM_..." / "killed by Animal_..." -> causa legível.
std::string cause_from_killer(std::string_view killer) {
    if (starts_with(killer, "Zmb")) return "infectados";
    if (starts_with(killer, "Animal_")) return "animais";
    return std::string(killer);
}

// De onde veio um acerto: "hit by Infectado", "hit by OffroadHatchback with TransportHit", ...
std::string hit_source(std::string_view rest) {
    if (starts_with(rest, "Player \"")) return "jogadores";
    if (starts_with(rest, "AI \"")) return "IA";
    std::string_view token = rest.substr(0, rest.find_first_of(" ("));
    if (contains(rest, "TransportHit")) return "veículos";
    if (starts_with(token, "FallDamage")) return "queda";
    if (token == "Infectado" || starts_with(token, "Zmb")) return "infectados";
    if (starts_with(token, "Animal_")) return "animais";
    return token.empty() ? "desconhecido" : std::string(token);
}

std::string cause_from_source(const std::string& source) {
    if (source == "jogadores") return "jogador";
    if (source == "veículos") return "veículo";
    return source;
}

}  // namespace

void AdmParser::feed(std::istream& in) {
    report_.files++;
    std::string line;
    while (std::getline(in, line)) {
        ++report_.lines;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        handle(line);
    }
}

AdmReport AdmParser::finish() {
    close_sessions(last_time_);
    return std::move(report_);
}

void AdmParser::handle(const std::string& line) {
    if (line.empty() || line[0] == '*') return;
    if (starts_with(line, "AdminLog started")) {
        on_header(line);
        return;
    }

    long long clock;
    std::size_t bar = line.find(" | ");
    if (!parse_clock(line, clock) || bar == npos) {
        ++report_.unrecognized;
        return;
    }
    // o log passa da meia-noite sem avisar: 23:59 -> 00:01
    if (previous_clock_ >= 0 && clock + 3600 < previous_clock_) day_offset_ += kDay;
    previous_clock_ = clock;
    long long t = base_time_ + day_offset_ + clock;
    last_time_ = t;

    std::string text = line.substr(bar + 3);
    if (starts_with(text, "AI \"")) {
        ++report_.ai_lines;  // mods de IA: "is unconscious", "turned hostile to ..."
    } else if (starts_with(text, "[")) {
        ++report_.mod_lines;  // mensagens que os mods escrevem no ADM
    } else if (starts_with(text, "Chat(")) {
        on_chat(text);
    } else if (starts_with(text, "Player \"")) {
        PlayerRef subject;
        if (ref_at(text, 7, subject))
            on_player_event(text, subject, t);
        else
            ++report_.unrecognized;
    } else {
        ++report_.unrecognized;
    }
}

// "AdminLog started on 2026-09-03 at 18:05:46": servidor (re)iniciou.
void AdmParser::on_header(const std::string& line) {
    close_sessions(last_time_);  // quem estava online caiu junto

    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(line.c_str(), "AdminLog started on %d-%d-%d at %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return;
    base_time_ = days_from_civil(y, mo, d) * kDay;
    day_offset_ = 0;
    previous_clock_ = h * 3600LL + mi * 60LL + s;

    char date[16];
    std::snprintf(date, sizeof date, "%04d-%02d-%02d", y, mo, d);
    if (report_.first_date.empty() || date < report_.first_date) report_.first_date = date;
    if (date > report_.last_date) report_.last_date = date;
}

void AdmParser::on_player_event(const std::string& text, const PlayerRef& subject, long long t) {
    stats(subject);
    std::size_t at;
    if ((at = text.find("killed by Player \"", subject.end)) != npos) {
        on_kill(text, at, subject, t, false);
    } else if ((at = text.find("killed by AI \"", subject.end)) != npos) {
        on_kill(text, at, subject, t, true);
    } else if ((at = text.find("hit by ", subject.end)) != npos) {
        on_hit(text, at, subject, t);
    } else if (contains(text, " is connected")) {
        on_connect(subject, t);
    } else if (contains(text, "has been disconnected")) {
        on_disconnect(subject, t);
    } else if (contains(text, "committed suicide")) {
        add_death(subject.key, "suicídio", t);
    } else if (contains(text, "bled out")) {
        add_death(subject.key, "sangramento", t);
    } else if ((at = text.find("killed by ", subject.end)) != npos) {
        std::string_view killer(text);
        killer = killer.substr(at + 10);
        add_death(subject.key, cause_from_killer(killer.substr(0, killer.find_first_of(" ("))), t);
    } else if (contains(text, " died.")) {
        add_death(subject.key, "", t);
    } else if (!contains(text, " is connecting") && !contains(text, "is unconscious") &&
               !contains(text, "regained consciousness") && !contains(text, "is choosing to respawn")) {
        ++report_.unrecognized;
    }
}

void AdmParser::on_chat(const std::string& text) {
    PlayerRef p;
    std::size_t quote = text.find('"');
    if (quote == npos || !ref_at(text, quote, p)) {
        ++report_.unrecognized;
        return;
    }
    stats(p).chat_messages++;
}

void AdmParser::on_kill(const std::string& text, std::size_t at, const PlayerRef& victim, long long t, bool by_ai) {
    Kill kill;
    kill.victim = victim.key;
    kill.time = t;
    kill.by_ai = by_ai;

    std::size_t from_here = at;
    if (!by_ai) {
        PlayerRef killer;
        if (!read_player(text, at, killer)) {
            ++report_.unrecognized;
            return;
        }
        kill.killer = killer.key;
        stats(killer).kills++;
        from_here = killer.end;
    }

    std::size_t with = text.find(" with ", from_here);
    std::size_t from = text.find(" from ", from_here);
    if (with != npos) {
        std::size_t end = (from != npos && from > with) ? from : text.size();
        kill.weapon = trim(std::string_view(text).substr(with + 6, end - with - 6));
    }
    if (from != npos) kill.distance = number_after(text, from + 6);
    if (kill.weapon.empty()) kill.weapon = "desconhecida";

    report_.kills.push_back(kill);
    add_death(victim.key, by_ai ? "IA" : "jogador", t);
}

void AdmParser::on_hit(const std::string& text, std::size_t at, const PlayerRef& victim, long long t) {
    std::string_view rest = std::string_view(text).substr(at + 7);
    std::string source = hit_source(rest);

    if (source == "jogadores") {
        PlayerRef attacker;
        if (read_player(text, at, attacker)) {
            auto& s = stats(attacker);
            s.hits++;
            if (text.find(" into Head", attacker.end) != npos) s.headshots++;
        }
    }

    std::size_t for_at = text.find(" for ", at);
    double damage = for_at == npos ? 0 : number_after(text, for_at + 5);
    auto& taken = report_.damage_taken[source];
    taken.hits++;
    if (damage > 0) taken.damage += damage;

    // "[HP: 0]" ou "(DEAD)" antes do "hit by": esse acerto matou
    std::string_view before = std::string_view(text).substr(0, at);
    if (contains(before, "(DEAD)") || contains(before, "[HP: 0]"))
        add_death(victim.key, cause_from_source(source), t, true);
}

void AdmParser::on_connect(const PlayerRef& p, long long t) {
    auto& s = stats(p);
    dead_.erase(p.key);  // conectou de novo = personagem novo depois de morrer
    if (online_.count(p.key)) return;
    online_[p.key] = t;
    s.sessions++;
    report_.peak_online = std::max(report_.peak_online, static_cast<int>(online_.size()));
}

void AdmParser::on_disconnect(const PlayerRef& p, long long t) {
    auto it = online_.find(p.key);
    if (it == online_.end()) return;
    report_.players[p.key].seconds_online += t - it->second;
    online_.erase(it);
}

void AdmParser::close_sessions(long long t) {
    for (const auto& [key, since] : online_) report_.players[key].seconds_online += t - since;
    online_.clear();
}

// A mesma morte costuma aparecer em 2 ou 3 linhas ("hit by ... [HP: 0]", "killed by ...", "died. Stats>"),
// em ordem variada. Depois da primeira, o jogador fica marcado como morto até conectar de novo;
// as linhas seguintes só completam a causa. A causa de um "killed by" vale mais que a deduzida do dano
// (o dano vem com nome traduzido, tipo "Urso-pardo"; o killed by traz "Animal_UrsusArctos").
void AdmParser::add_death(const std::string& key, const std::string& cause, long long t, bool from_hit) {
    auto it = dead_.find(key);
    if (it != dead_.end()) {
        auto& death = report_.deaths[it->second];
        bool upgrade = death.cause_from_hit && !from_hit && !cause.empty();
        if (death.cause.empty() || upgrade) {
            death.cause = cause;
            death.cause_from_hit = from_hit;
        }
        return;
    }
    dead_[key] = report_.deaths.size();
    report_.deaths.push_back({key, cause, t, from_hit});
    report_.players[key].deaths++;
}

PlayerStats& AdmParser::stats(const PlayerRef& p) {
    auto& s = report_.players[p.key];
    if (s.name.empty()) s.name = p.name;
    return s;
}

AdmReport parse_adm(std::istream& in) {
    AdmParser parser;
    parser.feed(in);
    return parser.finish();
}

}  // namespace dayzlog
