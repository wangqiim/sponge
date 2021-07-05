#include "tcp_receiver.hh"

#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

TCPReceiverState TCPReceiver::state() const {
    if (!this->ackno().has_value())
        return TCPReceiverState::LISTEN;
    if (this->ackno().has_value() && !this->stream_out().input_ended())
        return TCPReceiverState::SYN_RECV;
    if (this->ackno().has_value() && this->stream_out().input_ended())
        return TCPReceiverState::FIN_RECV;
    if (this->stream_out().error())
        return TCPReceiverState::ERROR;
    std::cerr << "panic: tcp receiver state unknown" << std::endl;
    exit(1);
}

void TCPReceiver::segment_received(const TCPSegment &seg) {
    bool syn = seg.header().syn;
    bool fin = seg.header().fin;
    uint64_t index = 0;
    switch (this->state()) {
        case TCPReceiverState::LISTEN:
            if (!syn)
                return;
            this->_isn = WrappingInt32(seg.header().seqno.raw_value());
            this->_reassembler.push_substring(seg.payload().copy(), index, fin);
            break;
        case TCPReceiverState::SYN_RECV:
            if (!syn)
                index = unwrap(seg.header().seqno, this->_isn.value(), this->stream_out().bytes_written()) - 1;
            this->_reassembler.push_substring(seg.payload().copy(), index, fin);
            break;
        case TCPReceiverState::FIN_RECV:
            // do nothing
            break;
        case TCPReceiverState::ERROR:
            // do nothing
            break;
        default:
            break;
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (this->_isn.has_value()) {
        uint64_t abs_seqno = static_cast<uint64_t>(stream_out().bytes_written()) + 1;
        // SYN_RECV
        if (!this->stream_out().input_ended())
            return wrap(abs_seqno, WrappingInt32(this->_isn.value()));
        // FIN_RECV
        return wrap(abs_seqno + 1, WrappingInt32(this->_isn.value()));
    }
    // LISTEN/ERROR
    return std::nullopt;
}

size_t TCPReceiver::window_size() const { return this->_capacity - this->stream_out().buffer_size(); }
