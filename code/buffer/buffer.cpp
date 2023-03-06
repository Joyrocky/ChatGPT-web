/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */ 
#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

//返回可读字节数
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

//返回可写字节数
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

//返回头部预留字节数
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

//返回数据可读处
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

//如果Buffer里可读字节数大于要读的字节，那么直接将readPos_指针向后移动len个位置
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
}

//从可读处读至Buffer的end指针处
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end );
    Retrieve(end - Peek());
}

//清空Buffer
void Buffer::RetrieveAll() {
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

//将从Buffer中读取的字节转换为string
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

//返回数据可写处
const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

//Buffer中追加数据后更新写指针的位置
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
} 

void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}

void Buffer::Append(const void* data, size_t len) {
    assert(data);
    Append(static_cast<const char*>(data), len);
}

//向Buffer中追加数据
void Buffer::Append(const char* str, size_t len) {
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

//如果可写字节数小于我们要写的字节数，那么我们就得增加空间。
void Buffer::EnsureWriteable(size_t len) {
    if(WritableBytes() < len) {
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}

// struct iovec {
//     ptr_t iov_base; /* Starting address */
//     size_t iov_len; /* Length in bytes */
// };

//从fd中读取数据到Buffer中
ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    char buff[65535];
    struct iovec iov[2];
    const size_t writable = WritableBytes();
    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = BeginPtr_() + writePos_;
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    // when there is enough space in this buffer, don't read into buf[].
    // when buf[] is used, we read 128k-1 bytes at most.

    
    //ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
    //从文件描述符fd所关联的文件中读取数据到iov指定的iovcnt个缓冲区中
    const ssize_t len = readv(fd, iov, 2);
    if(len < 0) {
        *saveErrno = errno;
    }
    //读取的数据没有超过Buffer的可写长度
    else if(static_cast<size_t>(len) <= writable) {
        writePos_ += len;
    }
    //读取数据超过Buffer长度时将超出部分放入buff中
    else {
        writePos_ = buffer_.size();
        Append(buff, len - writable);
    }
    return len;
}

//从Buffer中写出数据到fd
ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();
    
    //ssize_t write(int fd, const void *buf, size_t count);
    //从buf开始的缓冲区向文件描述符fd所引用的文件写入count字节数。
    ssize_t len = write(fd, Peek(), readSize);
    if(len < 0) {
        *saveErrno = errno;
        return len;
    } 
    readPos_ += len;
    return len;
}

//返回Buffer中第一个元素的地址
char* Buffer::BeginPtr_() {
    return &*buffer_.begin();
}

const char* Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}

//当空间不够用时，对Buffer进行扩充数据和数据复制
//如果头部剩下的空间和可写空间即 writableBytes() + prependableBytes()  小于 新加的数据长度 必须重新分配空间，如果大于的话，那么发生内部腾挪就行。
void Buffer::MakeSpace_(size_t len) {
    if(WritableBytes() + PrependableBytes() < len) {
        //当buffer_.resize(writerIndex_+len)的时候，会把以前的Buffer自动复制到新的Buffer处。
        buffer_.resize(writePos_ + len + 1);
    } 
    else {
        // move readable data to the front, make space inside buffer
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}