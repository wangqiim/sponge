#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return this->_sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return this->_receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return this->_time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    // any ACK (in CONNECTSTATE::LISTEN) should result in a RST
    if (this->_receiver.state() == TCPReceiverState::LISTEN && this->_sender.state() == TCPSenderState::CLOSED &&
        seg.header().ack) {
        return;
    }
    this->_time_since_last_segment_received = 0;
    // 1. if receive rst, shutdown
    if (seg.header().rst) {
        this->shutdown();
        return;
    }
    // ESTABLISHED -> CLOSE_WAIT
    if (seg.header().fin && this->_receiver.state() == TCPReceiverState::SYN_RECV &&
        (this->_sender.state() == TCPSenderState::SYN_ACKED ||
         this->_sender.state() == TCPSenderState::SYN_ACKED_ALSO)) {
        this->_linger_after_streams_finish = false;
    }
    // 2. receive segment
    this->_receiver.segment_received(seg);
    // 3. tell sender [ackno, window]
    this->_sender.ack_received(seg.header().ackno, seg.header().win);
    // 4. send segment (don't reply ACK)
    this->_sender.fill_window();
    // 5. may send a empty ACK
    if (this->_sender.segments_out().empty() && seg.length_in_sequence_space() != 0)
        this->_sender.send_empty_segment();
    this->send_segment_pkg();
    // LAST_ACK -> CLOSED
    if (this->_receiver.state() == TCPReceiverState::FIN_RECV && this->_sender.state() == TCPSenderState::FIN_ACKED &&
        this->_linger_after_streams_finish == false) {
        this->_active = false;
    }
}

bool TCPConnection::active() const { return this->_active; }

size_t TCPConnection::write(const string &data) {
    size_t len = this->_sender.stream_in().write(data);
    this->_sender.fill_window();
    this->send_segment_pkg();
    return len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    this->_time_since_last_segment_received += ms_since_last_tick;
    this->_sender.tick(ms_since_last_tick);
    // 1. if retrans exceed MAX_RETX_ATTEMPS, shutdown and rst
    if (this->_sender.consecutive_retransmissions() > this->_cfg.MAX_RETX_ATTEMPTS) {
        this->send_rst_pkg();
        this->shutdown();
        return;
    }
    this->send_segment_pkg();
    // 2. else check if in STATE: TIME_WAIT, check close connect
    if (this->_receiver.state() == TCPReceiverState::FIN_RECV && this->_sender.state() == TCPSenderState::FIN_ACKED &&
        this->_time_since_last_segment_received >= 10 * this->_cfg.rt_timeout) {
        this->_active = false;
        this->_linger_after_streams_finish = false;
        return;
    }
}

void TCPConnection::end_input_stream() {
    this->_sender.stream_in().end_input();
    this->_sender.fill_window();
    this->send_segment_pkg();
}

void TCPConnection::connect() {
    this->_sender.fill_window();
    this->send_segment_pkg();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_segment_pkg() {
    while (!this->_sender.segments_out().empty()) {
        this->_sender.segments_out().front().header().ack = this->_receiver.ackno().has_value();
        this->_sender.segments_out().front().header().ackno = this->_receiver.ackno().value_or(WrappingInt32(0));
        this->_sender.segments_out().front().header().win = static_cast<uint16_t>(this->_receiver.window_size());
        this->_segments_out.push(this->_sender.segments_out().front());
        this->_sender.segments_out().pop();
    }
}

void TCPConnection::send_rst_pkg() {
    this->_sender.send_empty_segment();
    TCPSegment seg = std::move(this->_sender.segments_out().front());
    seg.header().rst = true;
    this->_segments_out.push(seg);
    this->_sender.segments_out().pop();
}

void TCPConnection::shutdown() {
    this->_receiver.stream_out().set_error();
    this->_sender.stream_in().set_error();
    this->_active = false;
    this->_linger_after_streams_finish = false;
}
