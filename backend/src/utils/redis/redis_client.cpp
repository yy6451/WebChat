// Redis 客户端实现。
// 封装 redis-plus-plus 的 RedisCluster，提供一组便捷的键值、集合、哈希、排序集合操作。
// 业务层调用者无需直接依赖 redis-plus-plus 模板接口与异常类型。
#include "redis_client.h"

#include <chrono>
#include <iterator>

namespace {
// 内部统一错误返回 helper，当 RedisCluster 尚未连接时使用。
bool fillNotConnected(std::string* err) {
    if (err) {
        *err = "redis cluster not connected";
    }
    return false;
}
}

RedisClient::RedisClient() : cluster_(nullptr) {}

RedisClient::~RedisClient() {
    disconnect();
}

bool RedisClient::connect(const std::string& host,
                          int port,
                          const std::string& password,
                          int db,
                          std::string* err) {
    // 先退出旧连接，确保每次 connect 都是干净状态。
    disconnect();

    sw::redis::ConnectionOptions options;
    options.host = host;
    options.port = port;
    options.password = password;
    options.connect_timeout = std::chrono::milliseconds(2000);  // TCP 握手超时
    options.socket_timeout = std::chrono::milliseconds(2000);   // 读写超时

    // Redis Cluster 不支持 SELECT 命令；这里只是兼容传参接口。
    if (db != 0 && err) {
        *err = "redis cluster does not support SELECT; db ignored";
    }

    try {
        cluster_ = std::make_unique<sw::redis::RedisCluster>(options);
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        cluster_.reset();
        return false;
    }

    return true;
}

bool RedisClient::isConnected() const {
    return static_cast<bool>(cluster_);
}

void RedisClient::disconnect() {
    // 释放集群客户端资源。
    cluster_.reset();
}

bool RedisClient::ping(std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }

    try {
        const auto pong = cluster_->command<std::string>("PING", "ping");
        return pong == "PONG";
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// GET 命令封装
// 如果 key 存在，则返回 value；否则 found=false。
bool RedisClient::get(const std::string& key, std::string* value, bool* found, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        auto result = cluster_->get(key);
        if (found) {
            *found = static_cast<bool>(result);
        }
        if (result && value) {
            *value = *result;
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// SET 命令封装，写入字符串值。
bool RedisClient::set(const std::string& key, const std::string& value, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        cluster_->set(key, value);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// SETEX 命令封装，带 TTL 的写入。
bool RedisClient::setex(const std::string& key, int ttl_seconds, const std::string& value, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        cluster_->setex(key, ttl_seconds, value);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// DEL 命令封装，删除指定 key。
bool RedisClient::del(const std::string& key, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        cluster_->del(key);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// EXISTS 命令封装，判断 key 是否存在。
bool RedisClient::exists(const std::string& key, bool* exists, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        auto n = cluster_->exists(key);
        if (exists) {
            *exists = (n > 0);
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// SADD 命令封装，向 Set 添加成员。
bool RedisClient::sadd(const std::string& key, const std::string& member, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        cluster_->sadd(key, member);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

bool RedisClient::srem(const std::string& key, const std::string& member, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        cluster_->srem(key, member);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// SISMEMBER 命令封装，判断成员是否属于集合。
bool RedisClient::sismember(const std::string& key, const std::string& member, bool* is_member, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        auto ok = cluster_->sismember(key, member);
        if (is_member) {
            *is_member = ok;
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// SMEMBERS 命令封装，返回集合所有成员。
bool RedisClient::smembers(const std::string& key, std::vector<std::string>* members, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    if (members) {
        members->clear();
    }
    try {
        if (members) {
            cluster_->smembers(key, std::back_inserter(*members));
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

bool RedisClient::scard(const std::string& key, long long* count, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        auto n = cluster_->scard(key);
        if (count) {
            *count = n;
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// HSET 命令封装，将 field=value 写入哈希表。
bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        cluster_->hset(key, field, value);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// HGET 命令封装，读取哈希表中指定 field 的值。
bool RedisClient::hget(const std::string& key, const std::string& field, std::string* value, bool* found, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        auto result = cluster_->hget(key, field);
        if (found) {
            *found = static_cast<bool>(result);
        }
        if (result && value) {
            *value = *result;
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// INCR 命令封装，递增整数值并返回新值。
bool RedisClient::incr(const std::string& key, long long* value, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        auto n = cluster_->incr(key);
        if (value) {
            *value = n;
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// ZADD 命令封装，向 Sorted Set 添加成员和分数。
bool RedisClient::zadd(const std::string& key, const std::string& member, double score, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        cluster_->zadd(key, member, score);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

bool RedisClient::zscore(const std::string& key, const std::string& member, double* score, bool* found, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        auto result = cluster_->zscore(key, member);
        if (found) {
            *found = static_cast<bool>(result);
        }
        if (result && score) {
            *score = *result;
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

bool RedisClient::zrem(const std::string& key, const std::string& member, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        cluster_->zrem(key, member);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// ZREMRANGEBYSCORE 命令封装，删除指定 score 区间内的元素。
bool RedisClient::zremrangebyscore(const std::string& key, double min_score, double max_score, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    try {
        sw::redis::BoundedInterval<double> interval(min_score, max_score, sw::redis::BoundType::CLOSED);
        cluster_->zremrangebyscore(key, interval);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}

// ZRANGEBYSCORE 命令封装，返回指定 score 区间的成员列表。
bool RedisClient::zrangebyscore(const std::string& key, double min_score, double max_score,
                                std::vector<std::string>* members, std::string* err) {
    if (!cluster_) {
        return fillNotConnected(err);
    }
    if (members) {
        members->clear();
    }
    try {
        if (members) {
            sw::redis::BoundedInterval<double> interval(min_score, max_score, sw::redis::BoundType::CLOSED);
            cluster_->zrangebyscore(key, interval, std::back_inserter(*members));
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) {
            *err = ex.what();
        }
        return false;
    }
}


// ========== Stream 操作 ==========

bool RedisClient::xadd(const std::string& key, const std::string& id,
                       const std::vector<std::pair<std::string, std::string>>& fields,
                       std::string* out_id, std::string* err) {
    if (!cluster_) return fillNotConnected(err);
    try {
        auto result = cluster_->xadd(key, id, fields.begin(), fields.end());
        if (out_id) *out_id = result;
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) *err = ex.what();
        return false;
    }
}

bool RedisClient::xgroup_create(const std::string& key, const std::string& group,
                                const std::string& start_id, bool mkstream, std::string* err) {
    if (!cluster_) return fillNotConnected(err);
    try {
        // 使用 cluster_->command() 直接执行原始命令，Redis Cluster 自动路由
        std::vector<std::string> args = {"XGROUP", "CREATE", key, group, start_id};
        if (mkstream) args.push_back("MKSTREAM");
        command(args);
        return true;
    } catch (const sw::redis::Error& ex) {
        std::string what = ex.what();
        // BUSYGROUP 表示 Consumer Group 已存在，幂等成功
        if (what.find("BUSYGROUP") != std::string::npos) return true;
        if (err) *err = what;
        return false;
    } catch (const std::exception& ex) {
        // 额外兜底：command() 可能抛出 std::exception 而非 sw::redis::Error
        std::string what = ex.what();
        if (what.find("BUSYGROUP") != std::string::npos) return true;
        if (err) *err = what;
        return false;
    }
}

bool RedisClient::xreadgroup(const std::string& group, const std::string& consumer,
                             const std::string& key, const std::string& id,
                             int count, int block_ms,
                             std::vector<std::vector<std::pair<std::string, std::string>>>* messages,
                             std::string* err) {
    (void)block_ms; // sw::redis++ cluster xreadgroup doesn't support BLOCK directly
    if (!cluster_) return fillNotConnected(err);
    messages->clear();
    try {
        using Attr = std::pair<std::string, sw::redis::OptionalString>;
        using Item = std::pair<std::string, std::vector<Attr>>;
        using ItemStream = std::pair<std::string, std::vector<Item>>;
        std::vector<ItemStream> result;
        cluster_->xreadgroup(group, consumer, key, id, static_cast<long long>(count), std::back_inserter(result));
        for (auto& strm : result) {
            for (auto& item : strm.second) {
                std::vector<std::pair<std::string, std::string>> fields;
                for (auto& attr : item.second)
                    fields.emplace_back(attr.first, attr.second.value_or(""));
                fields.emplace_back("__msg_id", item.first);
                messages->emplace_back(std::move(fields));
                break; // only process first item per stream
            }
        }
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) *err = ex.what();
        return false;
    }
}

bool RedisClient::xack(const std::string& key, const std::string& group,
                       const std::string& id, std::string* err) {
    if (!cluster_) return fillNotConnected(err);
    try {
        cluster_->xack(key, group, id);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) *err = ex.what();
        return false;
    }
}

bool RedisClient::xtrim_maxlen(const std::string& key, long long maxlen, std::string* err) {
    if (!cluster_) return fillNotConnected(err);
    try {
        cluster_->xtrim(key, static_cast<size_t>(maxlen), true);
        return true;
    } catch (const sw::redis::Error& ex) {
        if (err) *err = ex.what();
        return false;
    }
}

long long RedisClient::xpending_count(const std::string& key, const std::string& group, std::string* err) {
    if (!cluster_) { fillNotConnected(err); return 0; }
    try {
        std::vector<std::pair<std::string, std::string>> dummy;
        auto summary = cluster_->xpending(key, group, std::back_inserter(dummy));
        return std::get<0>(summary);
    } catch (const sw::redis::Error& ex) {
        if (err) *err = ex.what();
        return 0;
    }
}

void RedisClient::command(const std::vector<std::string>& args) {
    if (!cluster_) return;
    cluster_->command(args.begin(), args.end());
}
