#pragma once

#include <istream>
#include <map>
#include <string>
#include <vector>

namespace dayzlog {

// Jogadores são identificados pelo id do log, não pelo nome: vários podem se chamar "Survivor",
// e o mesmo jogador às vezes aparece como "Nome (2)".

struct Kill {
    std::string killer;  // id do jogador; vazio quando quem matou foi a IA
    std::string victim;  // id do jogador
    std::string weapon;
    double distance = -1;  // metros; -1 quando o log não informa
    long long time = 0;
    bool by_ai = false;
};

struct Death {
    std::string player;  // id
    std::string cause;   // vazio = o log não disse a causa
    long long time = 0;
    bool cause_from_hit = false;  // deduzida da linha de dano; uma linha "killed by" substitui
};

struct DamageSource {
    int hits = 0;
    double damage = 0;
};

struct PlayerStats {
    std::string name;
    int sessions = 0;
    long long seconds_online = 0;
    int kills = 0;
    int deaths = 0;
    int hits = 0;       // acertos em outros jogadores
    int headshots = 0;  // desses, quantos na cabeça
    int chat_messages = 0;
};

struct AdmReport {
    std::vector<Kill> kills;
    std::vector<Death> deaths;
    std::map<std::string, PlayerStats> players;  // chave: id
    std::map<std::string, DamageSource> damage_taken;  // o que acertou os jogadores
    int files = 0;
    int lines = 0;
    int ai_lines = 0;
    int mod_lines = 0;
    int unrecognized = 0;
    int peak_online = 0;
    std::string first_date;
    std::string last_date;
};

// Lê um ou mais .ADM em sequência (em ordem de data) e junta tudo num relatório.
struct PlayerRef {
    std::string key;   // id do log (ou "nome:<nome>" se a linha não tiver id)
    std::string name;
    std::size_t end = 0;  // posição na linha logo depois da referência
};

class AdmParser {
public:
    void feed(std::istream& in);
    AdmReport finish();

private:
    AdmReport report_;
    std::map<std::string, long long> online_;   // id -> horário em que conectou
    std::map<std::string, std::size_t> dead_;   // id -> índice da morte, até o jogador renascer
    long long base_time_ = 0;
    long long day_offset_ = 0;
    long long previous_clock_ = -1;
    long long last_time_ = 0;

    void handle(const std::string& line);
    void on_header(const std::string& line);
    void on_player_event(const std::string& text, const PlayerRef& subject, long long t);
    void on_chat(const std::string& text);
    void on_kill(const std::string& text, std::size_t at, const PlayerRef& victim, long long t, bool by_ai);
    void on_hit(const std::string& text, std::size_t at, const PlayerRef& victim, long long t);
    void on_connect(const PlayerRef& p, long long t);
    void on_disconnect(const PlayerRef& p, long long t);
    void close_sessions(long long t);
    void add_death(const std::string& key, const std::string& cause, long long t, bool from_hit = false);
    PlayerStats& stats(const PlayerRef& p);
};

AdmReport parse_adm(std::istream& in);

}  // namespace dayzlog
