#include "stream_reassembler.hh"

#include <iostream>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _unassembled_map() {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    std::string adjusted_data = data;
    size_t adjusted_index = index;
    bool adjusted_eof = eof;
    // 1. adjust data, index and eof
    this->adjust_substring(adjusted_data, adjusted_index, adjusted_eof);
    // 2 try to colase with sibling
    this->coalse_node(adjusted_data, adjusted_index);
    // 3. if begin of unressembler data index = _next_assembled_index, write it to _output
    if (!this->_unassembled_map.empty() && this->_unassembled_map.begin()->first == this->_next_assembled_index) {
        auto it = this->_unassembled_map.begin();
        this->_output.write(it->second);
        this->_unassembled_bytes -= it->second.size();
        this->_next_assembled_index += it->second.size();
        this->_unassembled_map.erase(it);
    }
    // 4. if all data are assembled, end it
    if (this->unassembled_bytes() == 0 && this->_eof) {
        this->_output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return this->_unassembled_bytes; }

bool StreamReassembler::empty() const { return this->_unassembled_map.empty(); }

void StreamReassembler::print_details() const {
    std::cout << "_next_assembled_index: " << this->_next_assembled_index << std::endl;
    if (!this->_unassembled_map.empty()) {
        std::cout << "unassembled_bytes:" << std::endl;
    }
    for (const auto &item : this->_unassembled_map) {
        std::cout << "index: " << item.first << ", data: " << item.second << std::endl;
    }
}

// private
void StreamReassembler::adjust_substring(string &data, size_t &index, bool &eof) {
    // 1. if index < _next_assembled_index, cut off begin
    if (index < this->_next_assembled_index) {
        size_t offset = this->_next_assembled_index - index;
        if (offset >= data.size()) {
            index += data.size();
            data = "";
        } else {
            data = data.substr(offset);
            index += offset;
        }
    }
    // 2. if adjusted_data > right_bound, cut off end
    size_t right_bound = this->_output.remaining_capacity() + this->_next_assembled_index;
    if (index + data.size() > right_bound) {
        data = data.substr(0, right_bound - index);
        eof = false;
    } else if (eof) {
        this->_eof = true;
    }
}

void StreamReassembler::coalse_node(string &data, size_t &index) {
    if (data.size() == 0) {
        return;
    }
    // 1.try to coalse with right node
    for (;;) {
        auto it_right = this->_unassembled_map.lower_bound(index);
        if (it_right != this->_unassembled_map.end()) {
            if (index + data.size() >= it_right->first) {
                if (index + data.size() < it_right->first + it_right->second.size()) {
                    //               <------------>
                    // <-----sub---->[][][][][][][][index i][][]
                    // [][][][][][][][][][][][][][]
                    // <---------data.size()------>
                    for (size_t i = (data.size() - (it_right->first - index)); i < it_right->second.size(); i++) {
                        data.push_back(it_right->second[i]);
                    }
                }
                this->_unassembled_bytes -= it_right->second.size();
                this->_unassembled_map.erase(it_right);
                continue;
            }
        }
        break;
    }
    // 2.try to coalse with left node
    auto it_left = this->_unassembled_map.lower_bound(index);
    if (it_left != this->_unassembled_map.begin()) {
        it_left--;
        if (it_left->first + it_left->second.size() >= index) {
            size_t left_index = it_left->first;
            string left_data = it_left->second;
            if (left_index + left_data.size() < index + data.size()) {
                for (size_t i = (left_data.size() - (index - left_index)); i < data.size(); i++) {
                    left_data.push_back(data[i]);
                }
            }
            std::swap(left_data, data);
            std::swap(left_index, index);
            this->_unassembled_bytes -= it_left->second.size();
            this->_unassembled_map.erase(it_left);
        }
    }
    this->_unassembled_map[index] = data;
    this->_unassembled_bytes += data.size();
}
