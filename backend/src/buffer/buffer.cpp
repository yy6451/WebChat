/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/buffer/buffer.cpp
 * 类型: Source
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
#include "buffer.h"
#include <errno.h>

void Buffer::EnsureWriteable(size_t len) {
    if (WritableBytes() < len) {
        MakeSpace(len);
    }
    assert(WritableBytes() >= len);
}

void Buffer::Append(const char* data, size_t len) {
    EnsureWriteable(len);
    // 直接内存拷贝，速度极快
    std::copy(data, data + len, Begin() + writePos_);
    writePos_ += len;
}

void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}

ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    // 申请一块 65536 字节的栈空间。
    // 为什么在栈上？因为栈分配极快（移动指针即可），且出了作用域自动销毁，不产生内存碎片。
    char extrabuf[65536]; 

    struct iovec vec[2];
    const size_t writable = WritableBytes();

    // 第一块：Buffer 内部剩余的可写空间
    vec[0].iov_base = Begin() + writePos_;
    vec[0].iov_len = writable;
    
    // 第二块：栈上的临时空间
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 如果内部空间足够，就不需要读入栈了（避免栈空间的后续拷贝开销）
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    
    // readv 函数：操作系统一次性将数据填入这两块内存，填满第一块再填第二块。
    const ssize_t n = readv(fd, vec, iovcnt);

    if (n < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 第一块都没填满，说明读到的数据不多，直接移动写指针即可
        writePos_ += n;
    } else {
        // 第一块填满了，剩下的写到了 extrabuf 里。
        writePos_ = buffer_.size(); // 内部 buffer 写满了
        // 将栈上的数据追加到 vector 尾部（此时会触发 EnsureWriteable 内部的扩容）
        Append(extrabuf, n - writable);
    }
    return n;
}

ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();
    ssize_t n = write(fd, Peek(), readSize);
    if (n < 0) {
        *saveErrno = errno;
        return n;
    }
    // 成功写出了 n 字节，将读指针向后移动
    Retrieve(n);
    return n;
}

void Buffer::MakeSpace(size_t len) {
    // 如果 (剩余的可写空间 + 前面已读完的废弃空间) < len，说明真的装不下了，必须扩容
    if (WritableBytes() + PrependableBytes() < len + kCheapPrepend) {
        buffer_.resize(writePos_ + len);
    } else {
        // 空间其实够用，只是前面的废弃空间太大了，把未读的数据向左平移（内存整理）
        size_t readable = ReadableBytes();
        std::copy(Begin() + readPos_, 
                  Begin() + writePos_, 
                  Begin() + kCheapPrepend);
        readPos_ = kCheapPrepend;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}

std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}