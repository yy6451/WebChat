#include "message_codec.h"
#include <sstream>
#include <atomic>

namespace {

std::string extractJsonStringField(const std::string& json, const std::string& key) {
    std::string token = "\"" + key + "\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

int64_t extractJsonIntField(const std::string& json, const std::string& key, int64_t def = 0) {
    std::string token = "\"" + key + "\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) return def;
    std::string num;
    for (size_t i = pos + 1; i < json.size() && (isdigit(json[i]) || json[i] == '-'); ++i)
        num += json[i];
    if (num.empty()) return def;
    return std::atoll(num.c_str());
}

std::string escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::atomic<uint64_t> g_counter{0};

} // namespace

uint64_t MessageCodec::nextCounter() {
    return ++g_counter;
}

std::string MessageCodec::generateMsgId(const std::string& serverId) {
    // 格式: timestamp-server_hash-counter
    auto now = static_cast<int64_t>(time(nullptr));
    size_t hash = std::hash<std::string>{}(serverId);
    std::ostringstream oss;
    oss << now << "-" << (hash % 10000) << "-" << nextCounter();
    return oss.str();
}

MessageCodec::ChatMessage MessageCodec::decode(const std::string& json) {
    ChatMessage msg;
    msg.msg_id   = extractJsonStringField(json, "msg_id");
    msg.server_id = extractJsonStringField(json, "server_id");
    msg.type     = extractJsonStringField(json, "type");
    msg.from     = extractJsonStringField(json, "from");
    msg.to       = extractJsonStringField(json, "to");
    msg.content  = extractJsonStringField(json, "content");
    msg.timestamp = extractJsonIntField(json, "timestamp", 0);
    msg.seq      = static_cast<uint64_t>(extractJsonIntField(json, "seq", 0));
    msg.target_server = extractJsonStringField(json, "target_server");
    msg.target_role   = extractJsonStringField(json, "target_role");
    return msg;
}

std::string MessageCodec::encode(const ChatMessage& msg) {
    std::ostringstream oss;
    oss << "{"
        << "\"msg_id\":\"" << escape(msg.msg_id) << "\","
        << "\"server_id\":\"" << escape(msg.server_id) << "\","
        << "\"type\":\"" << escape(msg.type) << "\","
        << "\"from\":\"" << escape(msg.from) << "\","
        << "\"to\":\"" << escape(msg.to) << "\","
        << "\"content\":\"" << escape(msg.content) << "\","
        << "\"timestamp\":" << msg.timestamp << ","
        << "\"seq\":" << msg.seq;
    if (!msg.target_server.empty())
        oss << ",\"target_server\":\"" << escape(msg.target_server) << "\"";
    if (!msg.target_role.empty())
        oss << ",\"target_role\":\"" << escape(msg.target_role) << "\"";
    oss << "}";
    return oss.str();
}

MessageCodec::ChatMessage MessageCodec::fromLegacyJson(
        const std::string& json, const std::string& serverId, uint64_t seq) {
    ChatMessage msg = decode(json);
    msg.server_id = serverId;
    msg.seq = seq;
    if (msg.msg_id.empty())
        msg.msg_id = generateMsgId(serverId);
    if (msg.timestamp == 0)
        msg.timestamp = static_cast<int64_t>(time(nullptr));
    return msg;
}
