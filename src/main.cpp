#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "adm.hpp"
#include "report.hpp"
#include "rpt.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace dayzlog;

namespace {

const char* kUsage =
    "uso: dayzlog [opções] <arquivo ou pasta>...\n"
    "\n"
    "Lê os logs .ADM e .RPT de um servidor de DayZ e mostra um resumo.\n"
    "Passando uma pasta (ex.: a pasta profiles), junta todos os .ADM e .RPT dela num relatório só.\n"
    "\n"
    "opções:\n"
    "  --json      saída em JSON\n"
    "  --top N     quantos itens em cada ranking (padrão 10)\n"
    "  -h, --help  mostra esta ajuda\n";

struct Inputs {
    std::vector<fs::path> adm;
    std::vector<fs::path> rpt;
    int crash_dumps = 0;
};

std::string extension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// devolve false se o arquivo não for log nenhum
bool add_file(const fs::path& path, Inputs& inputs) {
    std::string ext = extension(path);
    if (ext == ".adm") {
        inputs.adm.push_back(path);
    } else if (ext == ".rpt") {
        inputs.rpt.push_back(path);
    } else if (ext == ".mdmp") {
        inputs.crash_dumps++;
    } else {
        return false;
    }
    return true;
}

bool collect(const fs::path& input, Inputs& inputs) {
    std::error_code ec;
    if (fs::is_directory(input, ec)) {
        for (const auto& entry : fs::directory_iterator(input, ec))
            if (entry.is_regular_file()) add_file(entry.path(), inputs);
        return true;
    }
    if (!fs::exists(input, ec)) {
        std::cerr << "não encontrado: " << input.string() << "\n";
        return false;
    }
    if (!add_file(input, inputs)) std::cerr << "ignorado (não é .ADM nem .RPT): " << input.string() << "\n";
    return true;
}

bool open(const fs::path& path, std::ifstream& in) {
    in.open(path, std::ios::binary);
    if (!in) std::cerr << "não foi possível abrir " << path.string() << "\n";
    return static_cast<bool>(in);
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    bool json = false;
    std::size_t top = 10;
    std::vector<fs::path> paths;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            return 0;
        } else if (arg == "--json") {
            json = true;
        } else if (arg == "--top" && i + 1 < argc) {
            try {
                int n = std::stoi(argv[++i]);
                if (n < 1) throw std::out_of_range("top");
                top = static_cast<std::size_t>(n);
            } catch (const std::exception&) {
                std::cerr << "--top precisa de um número maior que zero\n";
                return 2;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "opção desconhecida: " << arg << "\n\n" << kUsage;
            return 2;
        } else {
            paths.emplace_back(arg);
        }
    }

    if (paths.empty()) {
        std::cerr << kUsage;
        return 2;
    }

    Inputs inputs;
    bool ok = true;
    for (const auto& path : paths) ok = collect(path, inputs) && ok;
    if (inputs.adm.empty() && inputs.rpt.empty()) {
        std::cerr << "nenhum .ADM ou .RPT encontrado\n";
        return 2;
    }

    // o nome dos arquivos tem data e hora, então ordem alfabética = ordem cronológica
    auto by_name = [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); };
    std::sort(inputs.adm.begin(), inputs.adm.end(), by_name);
    std::sort(inputs.rpt.begin(), inputs.rpt.end(), by_name);

    AdmParser adm;
    for (const auto& path : inputs.adm) {
        std::ifstream in;
        if (open(path, in)) adm.feed(in);
        else ok = false;
    }
    RptParser rpt;
    for (const auto& path : inputs.rpt) {
        std::ifstream in;
        if (open(path, in)) rpt.feed(in, path.filename().string());
        else ok = false;
    }
    AdmReport adm_report = adm.finish();
    RptReport rpt_report = rpt.finish(top);

    if (json) {
        std::cout << "{\"adm\": " << (inputs.adm.empty() ? "null" : adm_json(adm_report, top))
                  << ",\n \"rpt\": " << (inputs.rpt.empty() ? "null" : rpt_json(rpt_report))
                  << ",\n \"crash_dumps\": " << inputs.crash_dumps << "}\n";
    } else {
        if (!inputs.adm.empty()) std::cout << adm_text(adm_report, top);
        if (!inputs.adm.empty() && !inputs.rpt.empty()) std::cout << "\n";
        if (!inputs.rpt.empty()) std::cout << rpt_text(rpt_report);
        if (inputs.crash_dumps > 0)
            std::cout << "\n" << inputs.crash_dumps << " dumps de crash (.mdmp) na pasta. O DayZ gera um quando o "
                      << "servidor trava ou cai.\n";
    }
    return ok ? 0 : 2;
}
