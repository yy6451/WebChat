/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/buffer/buffer.h
 * 类型: Header
 * 作用: 缓冲区模块：负责网络读写缓存管理与高效字节操作。
 * 关键关注点:
 * 1) 对外接口/类声明（或实现入口）与调用约束。
 * 2) 资源管理（内存、fd、锁、数据库连接）与异常路径回收。
 * 3) 并发与线程安全语义（线程池、锁、回调执行上下文）。
 * 4) 与其他模块的数据流边界（协议层/业务层/基础设施层）。
 * 维护建议:
 * 1) 修改接口时同步检查调用方与单元/集成测试。
 * 2) 修改并发逻辑时优先保证可见性与无竞态。
 * 3) 修改协议字段时保持前后端兼容与错误码稳定。
 */
#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <sys/uio.h> // for iovec
#include <unistd.h>  // for read/write
#include <cassert>

class Buffer {
public:
    // 默认预留 8 字节的头部空间（用于极速添加报文头长度等信息，避免数据挪动）
    static const size_t kCheapPrepend = 8;
    // 初始缓冲区大小 1024 字节。10万并发初始仅占用约 100MB 内存
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
          readPos_(kCheapPrepend),
          writePos_(kCheapPrepend) {}

    ~Buffer() = default;

    // ---------------- 基本状态查询 ----------------

    // 还有多少可读数据
    size_t ReadableBytes() const { return writePos_ - readPos_; }
    
    // 还有多少可写空间
    size_t WritableBytes() const { return buffer_.size() - writePos_; }
    
    // 返回可读数据的起始指针
    const char* Peek() const { return Begin() + readPos_; }
    
    // 头部预留空间大小
    size_t PrependableBytes() const { return readPos_; }

    // 返回可写数据的起始指针
    char* BeginWrite() { return Begin() + writePos_; }

    // 写入后，将写指针向后移动 len 长度
    void HasWritten(size_t len) { writePos_ += len; }

    // ---------------- 移动读写指针 ----------------

    // 读取后，将读指针向后移动 len 长度
    void Retrieve(size_t len) {
        assert(len <= ReadableBytes());
        if (len < ReadableBytes()) {
            readPos_ += len;
        } else {
            RetrieveAll();
        }
    }

    // 读完所有数据，复位指针（这非常关键，尽早复位可以避免内存不断向后滑动扩容）
    void RetrieveAll() {
        readPos_ = kCheapPrepend;
        writePos_ = kCheapPrepend;
    }

    // 将所有可读数据提取为 std::string 并清空 buffer
    std::string RetrieveAllToStr();

    // ---------------- 写入数据 ----------------

    // 确保有足够的空间写入 len 字节数据
    void EnsureWriteable(size_t len);

    // 追加数据到缓冲区
    void Append(const char* data, size_t len);
    void Append(const std::string& str);

    // ---------------- 核心 IO 操作 ----------------

    // 从文件描述符读取数据（核心所在：利用 readv 和栈上空间）
    ssize_t ReadFd(int fd, int* saveErrno);

    // 将缓冲区数据写入文件描述符
    ssize_t WriteFd(int fd, int* saveErrno);

private:
    // 返回底层的原始指针
    char* Begin() { return &*buffer_.begin(); }
    const char* Begin() const { return &*buffer_.begin(); }
    
    // 扩容或整理内存
    void MakeSpace(size_t len);

private:
    std::vector<char> buffer_;
    size_t readPos_;  // 读索引
    size_t writePos_; // 写索引
};