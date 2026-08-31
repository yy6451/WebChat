#ifndef REDIS_CLIENT_H
#define REDIS_CLIENT_H

#include <memory>
#include <string>
#include <vector>
#include <sw/redis++/redis++.h>

// RedisClient 封装 redis-plus-plus 的 Cluster 客户端。
// 该类只负责 Redis Cluster 连接、命令执行、基本类型的读写封装，
// 让业务层不必直接依赖 redis-plus-plus 的繁琐模板接口。
class RedisClient {
public:
    RedisClient();
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    // 连接到 Redis Cluster 节点。
    // host: Redis 节点地址，port: Redis 节点端口。
    // password: Redis 认证密码，db: Redis Cluster 不支持 SELECT，因此仅用于兼容参数。
    // err: 返回错误信息。
    bool connect(const std::string& host,
                 int port,
                 const std::string& password,
                 int db,
                 std::string* err);
    bool isConnected() const;
    void disconnect();

    // 基础键值操作
    bool ping(std::string* err);
    bool get(const std::string& key, std::string* value, bool* found, std::string* err);
    bool set(const std::string& key, const std::string& value, std::string* err);
    bool setex(const std::string& key, int ttl_seconds, const std::string& value, std::string* err);
    bool del(const std::string& key, std::string* err);
    bool exists(const std::string& key, bool* exists, std::string* err);

    // set 操作
    bool sadd(const std::string& key, const std::string& member, std::string* err);
    bool srem(const std::string& key, const std::string& member, std::string* err);
    bool sismember(const std::string& key, const std::string& member, bool* is_member, std::string* err);
    bool smembers(const std::string& key, std::vector<std::string>* members, std::string* err);
    bool scard(const std::string& key, long long* count, std::string* err);

    // hash 操作
    bool hset(const std::string& key, const std::string& field, const std::string& value, std::string* err);
    bool hget(const std::string& key, const std::string& field, std::string* value, bool* found, std::string* err);

    // 递增计数器
    bool incr(const std::string& key, long long* value, std::string* err);

    // sorted set 操作
    bool zadd(const std::string& key, const std::string& member, double score, std::string* err);
    bool zscore(const std::string& key, const std::string& member, double* score, bool* found, std::string* err);
    bool zrem(const std::string& key, const std::string& member, std::string* err);
    bool zremrangebyscore(const std::string& key, double min_score, double max_score, std::string* err);
    bool zrangebyscore(const std::string& key, double min_score, double max_score,
                       std::vector<std::string>* members, std::string* err);

    // ========== Stream 操作 ==========

    // XADD key * field value [field value ...] → 返回消息 ID
    bool xadd(const std::string& key, const std::string& id,
              const std::vector<std::pair<std::string, std::string>>& fields,
              std::string* out_id, std::string* err);

    // XGROUP CREATE key group id [MKSTREAM]
    bool xgroup_create(const std::string& key, const std::string& group,
                       const std::string& start_id, bool mkstream, std::string* err);

    // XREADGROUP GROUP group consumer [COUNT count] [BLOCK ms] STREAMS key id
    // 返回: 每条消息的完整字段映射
    bool xreadgroup(const std::string& group, const std::string& consumer,
                    const std::string& key, const std::string& id,
                    int count, int block_ms,
                    std::vector<std::vector<std::pair<std::string, std::string>>>* messages,
                    std::string* err);

    // XACK key group id
    bool xack(const std::string& key, const std::string& group,
              const std::string& id, std::string* err);

    // XTRIM key MAXLEN ~ count
    bool xtrim_maxlen(const std::string& key, long long maxlen, std::string* err);

    // XPENDING key group - + 1 返回待确认数量
    long long xpending_count(const std::string& key, const std::string& group, std::string* err);

    // 通用命令执行（Stream 命令在 Cluster 模式下需特殊处理时使用）
    void command(const std::vector<std::string>& args);

private:
    // RedisCluster 真正执行命令的客户端对象。
    std::unique_ptr<sw::redis::RedisCluster> cluster_;
};

#endif
