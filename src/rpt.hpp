#pragma once

#include <cstddef>
#include <istream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace dayzlog {

struct RptIssue {
    std::string message;  // números trocados por # para agrupar mensagens parecidas
    int count = 0;
    std::string first_file;
    int first_line = 0;
};

struct RptReport {
    int files = 0;
    int lines = 0;
    int errors = 0;
    int warnings = 0;
    std::map<std::string, int> errors_by_system;  // SCRIPT, PHYSICS, RESOURCES...
    std::vector<RptIssue> top_errors;
    std::vector<RptIssue> top_warnings;
};

// Lê um ou mais .RPT (log de script e engine) e agrupa erros e avisos repetidos.
class RptParser {
public:
    void feed(std::istream& in, const std::string& file);
    RptReport finish(std::size_t top);

private:
    struct Group {
        int count = 0;
        std::string first_file;
        int first_line = 0;
    };

    // "SCRIPT (E): Virtual Machine Exception" ocupa várias linhas: o motivo e o script vêm depois.
    struct PendingException {
        bool active = false;
        bool want_frame = false;
        std::string reason;
        std::string frame;
        int line = 0;
    };

    RptReport report_;
    std::unordered_map<std::string, Group> errors_;
    std::unordered_map<std::string, Group> warnings_;
    PendingException pending_;
    std::string file_;
    int line_ = 0;

    void handle(const std::string& line);
    void flush_exception();
    void add(bool error, const std::string& message, const std::string& system, int line);
};

RptReport parse_rpt(std::istream& in, std::size_t top);

// Expostos para os testes.
std::string normalize_message(const std::string& line);
std::string error_system(const std::string& line);

}  // namespace dayzlog
