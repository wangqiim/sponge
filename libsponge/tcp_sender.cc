#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <algorithm>
#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _retransmission_timeout{retx_timeout} {}

TCPSenderState TCPSender::state() const {
    if (this->next_seqno_absolute() == 0)
        return TCPSenderState::CLOSED;
    if (this->next_seqno_absolute() > 0 && this->next_seqno_absolute() == this->bytes_in_flight())
        return TCPSenderState::SYN_SENT;
    if (this->next_seqno_absolute() > bytes_in_flight() && !this->stream_in().eof())
        return TCPSenderState::SYN_ACKED;
    if (this->stream_in().eof() && this->next_seqno_absolute() < this->stream_in().bytes_written() + 2)
        return TCPSenderState::SYN_ACKED_ALSO;
    if (this->stream_in().eof() && this->next_seqno_absolute() == this->stream_in().bytes_written() + 2 &&
        this->bytes_in_flight() > 0)
        return TCPSenderState::FIN_SENT;
    if (this->stream_in().eof() && this->next_seqno_absolute() == this->stream_in().bytes_written() + 2 &&
        this->bytes_in_flight() == 0)
        return TCPSenderState::FIN_ACKED;
    if (this->stream_in().error())
        return TCPSenderState::ERROR;
    std::cerr << "panic: tcp sender state unknown" << std::endl;
    exit(1);
}

uint64_t TCPSender::bytes_in_flight() const {
    if (!this->_in_flight_TCPSegment.empty())
        return this->next_seqno_absolute() - this->_in_flight_TCPSegment.front().first;
    return 0;
}

void TCPSender::fill_window() {
    if (this->state() == TCPSenderState::SYN_SENT || this->state() == TCPSenderState::FIN_SENT ||
        this->state() == TCPSenderState::FIN_ACKED || this->state() == TCPSenderState::ERROR)
        return;
    // 1. if send zero bytes, return (treat a '0' window size as equal to '1')
    unsigned long long non_zero_window = std::max(1ULL, static_cast<unsigned long long>(this->_window_size));
    if (non_zero_window <= this->bytes_in_flight())
        return;
    size_t len = non_zero_window - static_cast<unsigned long long>(this->bytes_in_flight());
    // 2. if state = closed, send syn
    TCPSegment seg;
    uint64_t index = this->next_seqno_absolute();
    seg.header().seqno = wrap(this->_next_seqno, this->_isn);
    if (this->state() == TCPSenderState::CLOSED) {
        len -= 1;
        seg.header().syn = true;
        this->_next_seqno += 1;
    }
    // 3. construct payload
    std::string data = this->_stream.read(std::min(len, TCPConfig::MAX_PAYLOAD_SIZE));
    size_t payload_len = data.size();
    this->_next_seqno += payload_len;
    seg.payload() = Buffer(std::move(data));
    // 4. if stream is eof (empty and ended), send fin
    if (this->_stream.eof() && seg.length_in_sequence_space() < len) {
        seg.header().fin = true;
        this->_next_seqno += 1;
    }
    // 5. if empty seg, don't send it
    if (seg.length_in_sequence_space() > 0) {
        this->_in_flight_TCPSegment.push_back({index, seg});
        this->_segments_out.push(seg);
    }
    // 6. if payload is not empty, try continue send a segment
    if (seg.payload().size() > 0 && this->_window_size != 0)
        this->fill_window();
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, this->_isn, this->_next_seqno);
    // 1. Impossible ackno (beyond next seqno) is ignored
    if (this->_next_seqno < abs_ackno)
        return;
    this->_window_size = window_size;
    // 2. check and ack _in_flight_TCPSegment
    bool fresh_timer = false;
    while (!this->_in_flight_TCPSegment.empty()) {
        std::pair<uint64_t, TCPSegment> &index_seg_pair = this->_in_flight_TCPSegment.front();
        if (abs_ackno < index_seg_pair.first + index_seg_pair.second.length_in_sequence_space())
            break;
        fresh_timer = true;
        this->_in_flight_TCPSegment.pop_front();
    }
    // 3. whether fresh timer
    if (this->_in_flight_TCPSegment.empty() || fresh_timer) {
        this->_retrans_timer = 0;
        this->_consecutive_retransmissions = 0;
        this->_retransmission_timeout = this->_initial_retransmission_timeout;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 1. if in_flight_segment is empty, fresh timer, return
    if (this->_in_flight_TCPSegment.empty()) {
        this->_retrans_timer = 0;
        this->_consecutive_retransmissions = 0;
        this->_retransmission_timeout = this->_initial_retransmission_timeout;
        return;
    }
    // 2. else inc _retrans_timer
    this->_retrans_timer += ms_since_last_tick;
    // 3. if timeout, retrans first package in flight or send a heartbeat
    if (this->_retrans_timer >= this->_retransmission_timeout) {
        this->_retrans_timer = 0;
        // When filling window, treat a '0' window size as equal to '1' but don't back off RTO
        if (this->_window_size != 0) {
            this->_consecutive_retransmissions++;
            this->_retransmission_timeout *= 2;
        }
        this->_segments_out.push(this->_in_flight_TCPSegment.front().second);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return this->_consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = wrap(this->_next_seqno, this->_isn);
    this->_segments_out.push(segment);
}

// unused function
// I find if window == 0, linux socket will send a package
// [seqno=(next_seqno-1) and empty payload heartbeat] instead of treating window is one(cs144pdf))
void TCPSender::heart_beat() {
    if (this->state() == TCPSenderState::CLOSED || this->state() == TCPSenderState::ERROR)
        return;
    TCPSegment segment;
    segment.header().seqno = wrap(this->_next_seqno - 1, this->_isn);
    this->_segments_out.push(segment);
}
