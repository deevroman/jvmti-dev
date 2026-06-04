#ifndef DUMPER_LLM_DUMP_FORMATTER_H
#define DUMPER_LLM_DUMP_FORMATTER_H

#include <string>

#include <nlohmann/json.hpp>

std::string DefaultLlmDumpPath(const std::string& json_dump_path);
std::string BuildLlmReadableDump(const nlohmann::json& dump_events);

#endif  // DUMPER_LLM_DUMP_FORMATTER_H
