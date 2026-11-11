// Testes sem biblioteca externa: cada CHECK que falha imprime a linha e o programa sai com código 1.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "../src/adm.hpp"
#include "../src/report.hpp"
#include "../src/rpt.hpp"

using namespace dayzlog;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << "  falhou: " #cond "\n"; \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                                      \
    do {                                                                                                    \
        ++checks;                                                                                           \
        auto va = (a);                                                                                      \
        auto vb = (b);                                                                                      \
        if (!(va == vb)) {                                                                                  \
            ++failures;                                                                                     \
            std::cerr << __FILE__ << ":" << __LINE__ << "  " #a " == " #b "  (" << va << " != " << vb << ")\n"; \
        }                                                                                                   \
    } while (0)

static AdmReport adm(const std::string& text) {
    std::istringstream in(text);
    return parse_adm(in);
}

static RptReport rpt(const std::string& text, std::size_t top = 10) {
    std::istringstream in(text);
    return parse_rpt(in, top);
}

// procura pelo nome mostrado (o mapa de jogadores é por id)
static PlayerStats player(const AdmReport& r, const std::string& name) {
    auto names = display_names(r);
    for (const auto& [key, stats] : r.players)
        if (names[key] == name) return stats;
    std::cerr << "jogador não encontrado: " << name << "\n";
    ++failures;
    return {};
}

static std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

static void test_pvp_kill() {
    auto r = adm(
        "10:00:00 | Player \"Vitima\" (DEAD) (id=v1= pos=<1, 2, 3>) killed by Player \"Atirador\" (id=a1= pos=<4, 5, 6>) "
        "with M4-A1 from 245.3 meters \n");
    CHECK_EQ(r.kills.size(), 1u);
    CHECK_EQ(r.kills[0].killer, std::string("a1="));
    CHECK_EQ(r.kills[0].victim, std::string("v1="));
    CHECK_EQ(r.kills[0].weapon, std::string("M4-A1"));
    CHECK(!r.kills[0].by_ai);
    CHECK(r.kills[0].distance > 245.2 && r.kills[0].distance < 245.4);
    CHECK_EQ(player(r, "Atirador").kills, 1);
    CHECK_EQ(player(r, "Vitima").deaths, 1);
    CHECK_EQ(r.deaths[0].cause, std::string("jogador"));
}

static void test_ai_kill() {
    auto r = adm(
        "12:56:43 | Player \"P\" (DEAD) (id=p= pos=<1, 2, 3>) killed by AI \"Manua\" (group=1 faction=\"West\" "
        "pos=<4, 5, 6>) with AUR A1 from 41.0918 meters \n");
    CHECK_EQ(r.kills.size(), 1u);
    CHECK(r.kills[0].by_ai);
    CHECK(r.kills[0].killer.empty());
    CHECK_EQ(r.kills[0].weapon, std::string("AUR A1"));
    CHECK_EQ(r.deaths[0].cause, std::string("IA"));
    CHECK_EQ(r.players.size(), 1u);  // a IA não vira jogador
}

static void test_kill_without_distance() {
    auto r = adm("10:00:00 | Player \"A\" (DEAD) (id=a=) killed by Player \"B\" (id=b=) with Fists\n");
    CHECK_EQ(r.kills[0].weapon, std::string("Fists"));
    CHECK(r.kills[0].distance < 0);
}

static void test_one_death_many_lines() {
    // o jogo escreve a mesma morte em até 3 linhas, em qualquer ordem
    auto r = adm(
        "10:00:00 | Player \"A\" (DEAD) (id=a=)[HP: 0] hit by Urso-pardo into Head(0) for 30 damage (MeleeBearShock)\n"
        "10:00:00 | Player \"A\" (DEAD) (id=a=) killed by Animal_UrsusArctos\n"
        "10:00:01 | Player \"A\" (DEAD) (id=a=) died. Stats> Water: 1 Energy: 1 Bleed sources: 0\n"
        "10:00:09 | Player \"A\" (DEAD) (id=a=) is choosing to respawn\n");
    CHECK_EQ(r.deaths.size(), 1u);
    CHECK_EQ(player(r, "A").deaths, 1);
    CHECK_EQ(r.deaths[0].cause, std::string("animais"));  // o killed by vence o nome traduzido do dano
}

static void test_died_line_first() {
    auto r = adm(
        "10:00:00 | Player \"A\" (DEAD) (id=a=) died. Stats> Water: 1\n"
        "10:00:01 | Player \"A\" (DEAD) (id=a=) killed by ZmbF_CitizenANormal\n");
    CHECK_EQ(r.deaths.size(), 1u);
    CHECK_EQ(r.deaths[0].cause, std::string("infectados"));
}

static void test_dead_until_reconnect() {
    auto r = adm(
        "10:00:00 | Player \"A\" (DEAD) (id=a=) died. Stats> Water: 1\n"
        "10:30:00 | Player \"A\" (DEAD) (id=a=)[HP: 0] hit by Infectado into Torso(1) for 5 damage (MeleeInfected)\n"
        "10:31:00 | Player \"A\" (id=a=) is connected\n"
        "10:40:00 | Player \"A\" (id=a=) committed suicide\n");
    // o acerto no corpo às 10:30 não é morte nova; depois de conectar de novo, é
    CHECK_EQ(r.deaths.size(), 2u);
    CHECK_EQ(r.deaths[0].cause, std::string("infectados"));
    CHECK_EQ(r.deaths[1].cause, std::string("suicídio"));
}

static void test_fall_and_bleed() {
    auto r = adm(
        "16:05:07 | Player \"A\" (DEAD) (id=a= pos=<1, 2, 3>)[HP: 0] hit by FallDamageHealth\n"
        "16:05:07 | Player \"A\" (DEAD) (id=a= pos=<1, 2, 3>) died. Stats> Water: 7\n"
        "16:10:00 | Player \"B\" (DEAD) (id=b=) bled out\n");
    CHECK_EQ(r.deaths[0].cause, std::string("queda"));
    CHECK_EQ(r.deaths[1].cause, std::string("sangramento"));
}

static void test_hits() {
    auto r = adm(
        "10:00:00 | Player \"V\" (id=v=)[HP: 100] hit by Player \"S\" (id=s=) into Torso(12) for 30 damage (Bullet) "
        "with AKM from 50 meters\n"
        "10:00:01 | Player \"V\" (id=v=)[HP: 70] hit by Player \"S\" (id=s=) into Head(0) for 10.5 damage (Bullet) "
        "with AKM from 50 meters\n"
        "10:00:02 | Player \"V\" (id=v=)[HP: 60] hit by Infectado into LeftArm(18) for 5.85 damage (MeleeInfected)\n"
        "10:00:03 | Player \"V\" (id=v=)[HP: 59] hit by OffroadHatchback with TransportHit\n"
        "10:00:04 | Player \"V\" (id=v=)[HP: 58] hit by AI \"X\" (group=1 faction=\"West\") into Torso(1) for 2 damage "
        "(MeleeFist_Heavy)\n");
    CHECK_EQ(player(r, "S").hits, 2);
    CHECK_EQ(player(r, "S").headshots, 1);
    CHECK_EQ(r.damage_taken["jogadores"].hits, 2);
    CHECK(r.damage_taken["jogadores"].damage > 40.4 && r.damage_taken["jogadores"].damage < 40.6);
    CHECK_EQ(r.damage_taken["infectados"].hits, 1);
    CHECK_EQ(r.damage_taken["veículos"].hits, 1);
    CHECK_EQ(r.damage_taken["IA"].hits, 1);
    CHECK_EQ(r.deaths.size(), 0u);
}

static void test_same_name_different_ids() {
    auto r = adm(
        "10:00:00 | Player \"Survivor\" (id=AAAAxxxx= pos=<1, 2, 3>) is connected\n"
        "10:00:00 | Player \"Survivor\" (id=BBBBxxxx= pos=<1, 2, 3>) is connected\n");
    CHECK_EQ(r.players.size(), 2u);
    CHECK_EQ(r.peak_online, 2);
    auto names = display_names(r);
    CHECK_EQ(names["AAAAxxxx="], std::string("Survivor (AAAA)"));
    CHECK_EQ(names["BBBBxxxx="], std::string("Survivor (BBBB)"));
}

static void test_renamed_same_id() {
    auto r = adm(
        "10:00:00 | Player \"Lobo\" (id=L= pos=<1, 2, 3>) is connected\n"
        "10:30:00 | Player \"Lobo (2)\" (id=L= pos=<1, 2, 3>) has been disconnected\n");
    CHECK_EQ(r.players.size(), 1u);
    CHECK_EQ(player(r, "Lobo").seconds_online, 30LL * 60);
}

static void test_other_line_kinds() {
    auto r = adm(
        "10:00:00 | Player \"A\" (id=a=) is connecting\n"
        "10:00:01 | AI \"Kaito\" (group=5:\"Berezhki\" faction=\"Guards\" pos=<1, 2, 3>) is unconscious\n"
        "10:00:02 | AI \"Eva\" (group=73) turned hostile to attacker Player \"A\" (id=a= pos=<1, 2, 3>)\n"
        "10:00:03 | [AlgumMod] configuração carregada\n"
        "10:00:04 | Chat(\"A\"(id=a=)): salve\n"
        "10:00:05 | Player \"A\" (id=a=) is unconscious\n"
        "10:00:06 | Player \"A\" (id=a=) regained consciousness\n"
        "10:00:07 | ##### PlayerList log: 1 players\n"
        "linha sem horário\n");
    CHECK_EQ(r.ai_lines, 2);
    CHECK_EQ(r.mod_lines, 1);
    CHECK_EQ(player(r, "A").chat_messages, 1);
    CHECK_EQ(r.unrecognized, 2);
    CHECK_EQ(r.deaths.size(), 0u);
}

static void test_sessions_midnight_and_restart() {
    auto r = adm(
        "AdminLog started on 2026-11-08 at 23:40:00\n"
        "23:50:00 | Player \"A\" (id=a=) is connected\n"
        "00:20:00 | Player \"B\" (id=b=) is connected\n"
        "AdminLog started on 2026-11-09 at 00:30:00\n"
        "00:40:00 | Player \"A\" (id=a=) is connected\n"
        "00:50:00 | Player \"A\" (id=a=) has been disconnected\n");
    // restart fecha as sessões no último horário visto (00:20)
    CHECK_EQ(player(r, "A").seconds_online, 40LL * 60);
    CHECK_EQ(player(r, "B").seconds_online, 0LL);
    CHECK_EQ(player(r, "A").sessions, 2);
    CHECK_EQ(r.first_date, std::string("2026-11-08"));
    CHECK_EQ(r.last_date, std::string("2026-11-09"));
}

static void test_many_files() {
    AdmParser parser;
    std::istringstream a(
        "AdminLog started on 2026-09-03 at 15:30:16\n"
        "16:35:10 | Player \"P\" (id=p=) is connected\n"
        "18:01:03 | Player \"P\" (id=p=) has been disconnected\n");
    std::istringstream b(
        "AdminLog started on 2026-09-03 at 18:05:46\n"
        "18:11:36 | Player \"P\" (id=p=) is connected\n"
        "18:29:41 | Player \"P\" (id=p=) has been disconnected\n");
    parser.feed(a);
    parser.feed(b);
    AdmReport r = parser.finish();
    CHECK_EQ(r.files, 2);
    CHECK_EQ(player(r, "P").sessions, 2);
    CHECK_EQ(player(r, "P").seconds_online, (85LL * 60 + 53) + (18LL * 60 + 5));
}

static void test_rpt_grouping() {
    auto r = rpt(
        "22:58:12.007 SCRIPT    (E): NULL pointer\n"
        "23:01:12.330 SCRIPT (E): NULL pointer\n"
        " 6:05:45.002 PHYSICS    (E): LODShape 'wall_4.p3d'\n"
        "23:05:45.002 ERROR: Cannot open object wreck_old_7.p3d\n"
        "22:58:06.001 Warning Message: item 'Land_Wreck_Old_1'\n"
        "22:58:06.002 Warning Message: item 'Land_Wreck_Old_2'\n"
        "22:58:02.114 Mission read.\n");
    CHECK_EQ(r.errors, 4);
    CHECK_EQ(r.warnings, 2);
    CHECK_EQ(r.top_errors[0].count, 2);
    CHECK_EQ(r.top_errors[0].first_line, 1);
    CHECK_EQ(r.top_errors[0].message, std::string("SCRIPT (E): NULL pointer"));
    CHECK_EQ(r.errors_by_system["SCRIPT"], 2);
    CHECK_EQ(r.errors_by_system["PHYSICS"], 1);
    CHECK_EQ(r.errors_by_system["outros"], 1);
    CHECK_EQ(r.top_warnings[0].count, 2);
}

static void test_rpt_script_exception() {
    auto r = rpt(
        "18:06:38.896  SCRIPT    (E): Virtual Machine Exception\n"
        "\n"
        "Reason: NULL pointer to instance. Variable 'BubakLocations'\n"
        "\n"
        "Class:      'SpawnerBubaku'\n"
        "Function: 'SpawnerBubaku'\n"
        "Stack trace:\n"
        "SpawnerBubaku/Scripts/4_World/spawnerbubaku.c:73 Function SpawnerBubaku\n"
        "scripts/4_World/plugins\\pluginmanager.c:125 Function PluginsInit\n"
        "18:06:39.305  SCRIPT    (E): Virtual Machine Exception\n"
        "\n"
        "Reason: [::JsonLoadFile] :: [ERROR] :: Cannot load data\n"
        "JSON ERROR:\n"
        "18:06:40.000 Mission read.\n");
    // o [ERROR] e o JSON ERROR de dentro do bloco não contam como erros separados
    CHECK_EQ(r.errors, 2);
    CHECK_EQ(r.errors_by_system["SCRIPT"], 2);
    bool found = false;
    for (const auto& issue : r.top_errors)
        if (issue.message ==
            "SCRIPT (E): NULL pointer to instance. Variable 'BubakLocations'  "
            "[SpawnerBubaku/Scripts/4_World/spawnerbubaku.c:73]")
            found = true;
    CHECK(found);
}

static void test_rpt_top_limit() {
    auto r = rpt("ERROR: a\nERROR: b\nERROR: c\n", 2);
    CHECK_EQ(r.errors, 3);
    CHECK_EQ(r.top_errors.size(), 2u);
}

static void test_normalize() {
    CHECK_EQ(normalize_message("12:00:00.123 SCRIPT  (W): item 42 at 1802"), std::string("SCRIPT (W): item # at #"));
    CHECK_EQ(normalize_message(" 6:03:12.123 RESOURCES (E): x"), std::string("RESOURCES (E): x"));
    CHECK_EQ(normalize_message("ERROR: wreck_old_7.p3d"), std::string("ERROR: wreck_old_#.p3d"));
    CHECK_EQ(error_system("18:05:46.436  PHYSICS    (E): LODShape"), std::string("PHYSICS"));
    CHECK_EQ(error_system("ERROR: Mod key"), std::string("outros"));
}

static void test_helpers() {
    CHECK_EQ(format_duration(59), std::string("0min"));
    CHECK_EQ(format_duration(3 * 3600 + 5 * 60), std::string("3h 05min"));
    CHECK_EQ(json_escape("a\"b\\c\n"), std::string("a\\\"b\\\\c\\n"));
}

static void test_example_files(const std::string& dir) {
    std::istringstream adm_in(read_file(dir + "/DayZServer_x64_exemplo.ADM"));
    AdmReport a = parse_adm(adm_in);
    CHECK_EQ(a.players.size(), 5u);
    CHECK_EQ(a.kills.size(), 3u);
    CHECK_EQ(a.deaths.size(), 7u);
    CHECK_EQ(a.peak_online, 3);
    CHECK_EQ(a.ai_lines, 2);
    CHECK_EQ(a.mod_lines, 1);
    CHECK_EQ(a.unrecognized, 0);
    CHECK_EQ(player(a, "Lobo").kills, 2);
    CHECK_EQ(player(a, "Lobo").hits, 4);
    CHECK_EQ(player(a, "Lobo").headshots, 2);
    CHECK_EQ(player(a, "Lobo").seconds_online, 5585LL);
    CHECK_EQ(player(a, "Corvo").deaths, 3);
    CHECK_EQ(player(a, "Survivor (aX9k)").seconds_online, 1875LL);
    CHECK_EQ(player(a, "Survivor (Zr4t)").deaths, 1);
    CHECK_EQ(player(a, "Raposa").chat_messages, 1);
    CHECK_EQ(a.damage_taken["IA"].hits, 2);

    std::istringstream rpt_in(read_file(dir + "/DayZServer_x64_exemplo.RPT"));
    RptReport r = parse_rpt(rpt_in, 10);
    CHECK_EQ(r.errors, 7);
    CHECK_EQ(r.warnings, 3);
    CHECK_EQ(r.errors_by_system["SCRIPT"], 3);
    CHECK_EQ(r.top_errors[0].count, 2);
    CHECK_EQ(r.top_errors[0].first_line, 15);
}

int main(int argc, char** argv) {
    std::string examples = argc > 1 ? argv[1] : "examples";

    test_pvp_kill();
    test_ai_kill();
    test_kill_without_distance();
    test_one_death_many_lines();
    test_died_line_first();
    test_dead_until_reconnect();
    test_fall_and_bleed();
    test_hits();
    test_same_name_different_ids();
    test_renamed_same_id();
    test_other_line_kinds();
    test_sessions_midnight_and_restart();
    test_many_files();
    test_rpt_grouping();
    test_rpt_script_exception();
    test_rpt_top_limit();
    test_normalize();
    test_helpers();
    test_example_files(examples);

    std::cout << checks - failures << "/" << checks << " checagens passaram\n";
    return failures == 0 ? 0 : 1;
}
