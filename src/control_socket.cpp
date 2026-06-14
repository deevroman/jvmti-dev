#include "control_socket.h"

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <map>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
constexpr size_t kMaxPayloadSize = 64 * 1024;

std::map<std::string, std::string> parse_key_value_payload(const std::string& payload)
{
    std::map<std::string, std::string> result;
    std::istringstream ss(payload);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        const auto separator = token.find(':');
        if (separator == std::string::npos)
            continue;

        result[token.substr(0, separator)] = token.substr(separator + 1);
    }
    return result;
}

bool read_payload(int fd, std::string& payload)
{
    payload.clear();
    char buffer[1024];
    while (payload.size() < kMaxPayloadSize)
    {
        const ssize_t read_count = ::read(fd, buffer, sizeof(buffer));
        if (read_count < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (read_count == 0)
            break;

        payload.append(buffer, static_cast<size_t>(read_count));
        if (payload.find('\n') != std::string::npos)
            break;
    }

    if (payload.size() >= kMaxPayloadSize)
        return false;

    const auto newline_pos = payload.find('\n');
    if (newline_pos != std::string::npos)
        payload.resize(newline_pos);

    return !payload.empty();
}

bool send_all(int fd, const std::string& response)
{
    size_t offset = 0;
    while (offset < response.size())
    {
        const ssize_t written = ::write(fd, response.data() + offset, response.size() - offset);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (written == 0)
            return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

std::string required_value(const json& payload,
                           const char* key,
                           std::string* error_message)
{
    if (!payload.contains(key) || !payload[key].is_string())
    {
        if (error_message)
            *error_message = std::string("missing or invalid field: ") + key;
        return "";
    }
    return payload[key].get<std::string>();
}

long optional_long_value(const json& payload,
                         const char* key,
                         long default_value)
{
    if (!payload.contains(key))
        return default_value;
    if (payload[key].is_number_integer())
        return payload[key].get<long>();
    if (payload[key].is_number_unsigned())
        return static_cast<long>(payload[key].get<unsigned long>());
    if (payload[key].is_string())
    {
        char* end = nullptr;
        const std::string raw = payload[key].get<std::string>();
        const long parsed = strtol(raw.c_str(), &end, 10);
        if (end != raw.c_str() && *end == '\0')
            return parsed;
    }
    return default_value;
}

bool parse_runtime_payload_impl(const std::string& payload,
                                RuntimeConfig& config,
                                std::string& error_message)
{
    const json parsed = json::parse(payload, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object())
    {
        config.target_class = required_value(parsed, "target_class", &error_message);
        if (!error_message.empty())
            return false;

        config.target_method = required_value(parsed, "target_method", &error_message);
        if (!error_message.empty())
            return false;

        config.target_method_signature =
            required_value(parsed, "target_method_signature", &error_message);
        if (!error_message.empty())
            return false;

        if (parsed.contains("dump") && parsed["dump"].is_string())
            config.dump_path = parsed["dump"].get<std::string>();
        else if (parsed.contains("dump_path") && parsed["dump_path"].is_string())
            config.dump_path = parsed["dump_path"].get<std::string>();
        if (parsed.contains("llm_dump") && parsed["llm_dump"].is_string())
            config.llm_dump_path = parsed["llm_dump"].get<std::string>();
        else if (parsed.contains("llm_dump_path") && parsed["llm_dump_path"].is_string())
            config.llm_dump_path = parsed["llm_dump_path"].get<std::string>();
        else if (parsed.contains("dump_llm") && parsed["dump_llm"].is_string())
            config.llm_dump_path = parsed["dump_llm"].get<std::string>();
        config.external_string_limit =
            optional_long_value(parsed, "external_string_limit", config.external_string_limit);

        return true;
    }

    const auto parsed_kv = parse_key_value_payload(payload);
    if (!parsed_kv.count("target_class") ||
        !parsed_kv.count("target_method") ||
        !parsed_kv.count("target_method_signature"))
    {
        error_message =
            "payload must be JSON or key:value pairs with target_class,target_method,target_method_signature";
        return false;
    }

    config.target_class = parsed_kv.at("target_class");
    config.target_method = parsed_kv.at("target_method");
    config.target_method_signature = parsed_kv.at("target_method_signature");
    if (parsed_kv.count("dump"))
        config.dump_path = parsed_kv.at("dump");
    else if (parsed_kv.count("dump_path"))
        config.dump_path = parsed_kv.at("dump_path");
    if (parsed_kv.count("llm_dump"))
        config.llm_dump_path = parsed_kv.at("llm_dump");
    else if (parsed_kv.count("llm_dump_path"))
        config.llm_dump_path = parsed_kv.at("llm_dump_path");
    else if (parsed_kv.count("dump_llm"))
        config.llm_dump_path = parsed_kv.at("dump_llm");
    if (parsed_kv.count("external_string_limit"))
    {
        char* end = nullptr;
        const std::string raw = parsed_kv.at("external_string_limit");
        const long parsed_limit = strtol(raw.c_str(), &end, 10);
        if (end != raw.c_str() && *end == '\0')
            config.external_string_limit = parsed_limit;
    }

    return true;
}
} // namespace

bool parse_runtime_config_payload(const std::string& payload,
                                  RuntimeConfig& config,
                                  std::string& error_message)
{
    config = RuntimeConfig{};
    error_message.clear();
    return parse_runtime_payload_impl(payload, config, error_message);
}

bool load_runtime_config_from_file(const std::string& file_path,
                                   RuntimeConfig& config,
                                   std::string& error_message)
{
    config = RuntimeConfig{};
    error_message.clear();

    if (file_path.empty())
    {
        error_message = "config_file path is empty";
        return false;
    }

    std::ifstream in(file_path);
    if (!in)
    {
        error_message = "cannot open config file: " + file_path;
        return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof())
    {
        error_message = "failed to read config file: " + file_path;
        return false;
    }

    const std::string payload = buffer.str();
    if (payload.empty())
    {
        error_message = "config file is empty: " + file_path;
        return false;
    }

    return parse_runtime_payload_impl(payload, config, error_message);
}

bool wait_for_runtime_config(uint16_t port,
                             RuntimeConfig& config,
                             std::string& error_message)
{
    config = RuntimeConfig{};
    error_message.clear();

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        error_message = std::string("socket failed: ") + strerror(errno);
        return false;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
    {
        error_message = std::string("bind failed on 127.0.0.1:") +
                        std::to_string(port) + ": " + strerror(errno);
        close(server_fd);
        return false;
    }

    if (::listen(server_fd, 1) < 0)
    {
        error_message = std::string("listen failed: ") + strerror(errno);
        close(server_fd);
        return false;
    }

    int client_fd = -1;
    while (client_fd < 0)
    {
        client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0 && errno != EINTR)
        {
            error_message = std::string("accept failed: ") + strerror(errno);
            close(server_fd);
            return false;
        }
    }

    std::string payload;
    if (!read_payload(client_fd, payload))
    {
        error_message = "failed to read runtime config payload";
        send_all(client_fd, "ERROR " + error_message + "\n");
        close(client_fd);
        close(server_fd);
        return false;
    }

    const bool parsed = parse_runtime_config_payload(payload, config, error_message);
    if (!parsed)
        send_all(client_fd, "ERROR " + error_message + "\n");
    else
        send_all(client_fd, "OK\n");

    close(client_fd);
    close(server_fd);
    return parsed;
}
