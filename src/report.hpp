#pragma once

#include <cstddef>
#include <map>
#include <string>

#include "adm.hpp"
#include "rpt.hpp"

namespace dayzlog {

std::string adm_text(const AdmReport& report, std::size_t top);
std::string rpt_text(const RptReport& report);

std::string adm_json(const AdmReport& report, std::size_t top);
std::string rpt_json(const RptReport& report);

// id -> nome pra mostrar. Quando dois ids têm o mesmo nome ("Survivor"), entra o começo do id.
std::map<std::string, std::string> display_names(const AdmReport& report);

std::string format_duration(long long seconds);
std::string json_escape(const std::string& s);

}  // namespace dayzlog
