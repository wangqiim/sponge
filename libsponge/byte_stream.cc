#include "byte_stream.hh"

#include <cstring>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) { this->_buf.resize(capacity + 1); }

size_t ByteStream::write(const string &data) {
    size_t remain_cap = this->remaining_capacity();
    size_t write_len = remain_cap > data.size() ? data.size() : remain_cap;
    size_t len = this->_buf.size() - this->_tail;
    if (len >= write_len) {
        memmove(&this->_buf[this->_tail], data.data(), write_len);
    } else {
        memmove(&this->_buf[this->_tail], data.data(), len);
        memmove(&this->_buf[0], &data.data()[len], write_len - len);
    }
    this->_tail += write_len;
    this->_tail %= this->_buf.size();
    this->_bytes_written += write_len;
    return write_len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t n = len;
    if (n > this->buffer_size()) {
        n = this->buffer_size();
    }
    size_t buf_cap = this->_buf.size();
    if (this->_head + n <= buf_cap)
        return this->_buf.substr(this->_head, n);
    else
        return this->_buf.substr(this->_head) + this->_buf.substr(0, n - (buf_cap - this->_head));
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t n = len;
    if (n > this->buffer_size()) {
        n = this->buffer_size();
    }
    this->_bytes_read += n;
    int buf_cap = this->_buf.size();
    this->_head = (this->_head + n) % buf_cap;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string str = this->peek_output(len);
    this->pop_output(len);
    return str;
}

void ByteStream::end_input() { this->_end = true; }

bool ByteStream::input_ended() const { return this->_end; }

size_t ByteStream::buffer_size() const {
    size_t buf_cap = this->_buf.size();
    return (this->_tail - this->_head + buf_cap) % buf_cap;
}

bool ByteStream::buffer_empty() const { return this->_head == this->_tail; }

bool ByteStream::buffer_full() const { return (this->_tail + 1) % this->_buf.size() == this->_head; }

bool ByteStream::eof() const { return this->_end && this->buffer_empty(); }

size_t ByteStream::bytes_written() const { return this->_bytes_written; }

size_t ByteStream::bytes_read() const { return this->_bytes_read; }

size_t ByteStream::remaining_capacity() const {
    size_t buf_cap = this->_buf.size();
    size_t used_capacity = (this->_tail - this->_head + buf_cap) % buf_cap;
    // std::cout << "----" << (buf_cap - 1) - used_capacity << "---" << std::endl;
    return (buf_cap - 1) - used_capacity;
}
