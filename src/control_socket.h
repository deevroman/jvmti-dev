#ifndef DUMPER_SOCKET_H
#define DUMPER_SOCKET_H

#include <cstdint>
#include <string>

struct RuntimeConfig
{
    std::string target_class;
    std::string target_method;
    std::string target_method_signature;
    std::string dump_path;
    std::string llm_dump_path;
    long external_string_limit = 0;
};

bool parse_runtime_config_payload(const std::string& payload,
                                  RuntimeConfig& config,
                                  std::string& error_message);

bool load_runtime_config_from_file(const std::string& file_path,
                                   RuntimeConfig& config,
                                   std::string& error_message);

bool wait_for_runtime_config(uint16_t port,
                             RuntimeConfig& config,
                             std::string& error_message);

#endif // DUMPER_SOCKET_H
