#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address), _ip2mac_(), _arp_flood(), _send_datagram_queue() {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway,
// but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    // if (dgram.header().dst == next_hop.ipv4_numeric()) {
    //     std::cerr << "panic: send_datagram to myself" << std::endl;
    //     exit(1);
    // }
    EthernetFrame frame;
    frame.header().src = this->_ethernet_address;
    auto it = this->_ip2mac_.find(next_hop_ip);
    if (it != this->_ip2mac_.end() && this->_timer <= it->second.second + 30000) {
        // 1. if next_hop has been recordedand and not expire, send dgram
        frame.header().dst = it->second.first;
        frame.header().type = EthernetHeader::TYPE_IPv4;
        frame.payload() = dgram.serialize();
    } else {
        // 2. if next_hop hasn't been recored or expire, send arp request
        // 2.1 store dgram in queue
        this->_send_datagram_queue[next_hop_ip].push(dgram);
        // 2.2 avoid arp flood
        if (this->_arp_flood.count(next_hop_ip) == 0 || this->_arp_flood[next_hop_ip] + 5000 <= this->_timer) {
            this->_arp_flood[next_hop_ip] = this->_timer;
        } else {
            return;
        }
        frame.header().dst = ETHERNET_BROADCAST;
        frame.header().type = EthernetHeader::TYPE_ARP;
        // construct arp
        ARPMessage arp;
        arp.opcode = ARPMessage::OPCODE_REQUEST;
        arp.sender_ethernet_address = this->_ethernet_address;
        arp.sender_ip_address = this->_ip_address.ipv4_numeric();
        arp.target_ethernet_address = {};
        arp.target_ip_address = next_hop_ip;
        frame.payload() = arp.serialize();
    }
    this->_frames_out.push(frame);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().dst != ETHERNET_BROADCAST && frame.header().dst != this->_ethernet_address) {
        return {};
    }
    // If the inbound frame is IPv4, parse the payload as an InternetDatagram and,
    // if successful (meaning the parse() method returned ParseResult::NoError),
    // return the resulting InternetDatagram to the caller.
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram datagram;
        ParseResult parseResult = datagram.parse(Buffer(frame.payload()));
        if (parseResult == ParseResult::NoError) {
            return datagram;
        }
        std::cerr << "panic: parse ipv4 error" << std::endl;
        exit(1);
    }
    // If the inbound frame is ARP, parse the payload as an ARPMessage and,
    // if successful, remember the mapping between the sender’s IP address and
    // Ethernet address for 30 seconds. (Learn mappings from both requests and replies.)
    // In addition, if it’s an ARP request asking for our IP address,
    // send an appropriate ARP reply.
    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage recv_arp;
        ParseResult parseResult = recv_arp.parse(Buffer(frame.payload()));
        if (parseResult == ParseResult::NoError) {
            this->_ip2mac_[recv_arp.sender_ip_address] = {recv_arp.sender_ethernet_address, this->_timer};
            if (recv_arp.opcode == ARPMessage::OPCODE_REQUEST) {
                if (recv_arp.target_ip_address == this->_ip_address.ipv4_numeric()) {
                    EthernetFrame send_frame;
                    // header
                    send_frame.header().src = this->_ethernet_address;
                    send_frame.header().dst = frame.header().src;
                    send_frame.header().type = EthernetHeader::TYPE_ARP;
                    // payload
                    ARPMessage send_arp;
                    send_arp.opcode = ARPMessage::OPCODE_REPLY;
                    send_arp.sender_ethernet_address = this->_ethernet_address;
                    send_arp.sender_ip_address = this->_ip_address.ipv4_numeric();
                    send_arp.target_ethernet_address = recv_arp.sender_ethernet_address;
                    send_arp.target_ip_address = recv_arp.sender_ip_address;
                    send_frame.payload() = send_arp.serialize();
                    this->_frames_out.push(send_frame);
                }
            }
            // if recv_arp.opcode == ARPMessage::OPCODE_REPLY,  do nothing
            auto &queue = this->_send_datagram_queue[recv_arp.sender_ip_address];
            while (!queue.empty()) {
                this->send_datagram(queue.front(), Address::from_ipv4_numeric(recv_arp.sender_ip_address));
                queue.pop();
            }
            return {};
        }
        std::cerr << "panic: parse arp error" << std::endl;
        exit(1);
        return {};
    }
    std::cerr << "panic: recv_frame unknown type" << std::endl;
    exit(1);
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { this->_timer += ms_since_last_tick; }
