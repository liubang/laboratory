//=====================================================================
//
// block_builder.cpp -
//
// Created by liubang on 2023/05/29 19:44
// Last Modified: 2023/05/29 19:44
//
//=====================================================================

#include "cpp/misc/sst/block_builder.h"

#include <cassert>
#include <iostream>

#include "cpp/misc/sst/encoding.h"

namespace pl {

BlockBuilder::BlockBuilder(const Options* options)
    : comparator_(options->comparator), block_restart_interval_(options->block_restart_interval) {
    restarts_.push_back(0);
}

void BlockBuilder::add(const Binary& key, const Binary& value) {
    assert(!finished_);
    auto last_key_pice = Binary(last_key_);
    // 必须保证key按照指定的comparator的递增的顺序
    assert(buffer_.empty() || comparator_->compare(key, last_key_pice) > 0);
    uint32_t shared = 0;
    if (counter_ < block_restart_interval_) {
        // 计算当前key和前一个key的最大公共前缀
        const uint32_t min_length = std::min(last_key_pice.size(), key.size());
        while (shared < min_length && (last_key_pice[shared] == key[shared])) {
            shared++;
        }
    } else {
        restarts_.push_back(buffer_.size());
        counter_ = 0;
    }

    const uint32_t non_shared = key.size() - shared;

    /*
     * +----------------+--------------------+---------------+----------------+-------+
     * | shared size 4B | non shared size 4B | value size 4B | non shared key |
     * value |
     * +----------------+--------------------+---------------+----------------+-------+
     */
    encodeInt<uint32_t>(&buffer_, shared);
    encodeInt<uint32_t>(&buffer_, non_shared);
    encodeInt<uint32_t>(&buffer_, value.size());

    buffer_.append(key.data() + shared, non_shared);
    buffer_.append(value.data(), value.size());

    last_key_.resize(shared);
    last_key_.append(key.data() + shared, non_shared);
    assert(Binary(last_key_).compare(key) == 0);
    counter_++;
}

Binary BlockBuilder::finish() {
    /*
     * restarts indexes
     *
     * +---------------+---------------+--------+---------------+------------------+
     * | restart[0] 4B | restart[1] 4B | ...... | restart[n] 4B | restart count 4B
     * |
     * +---------------+---------------+--------+---------------+------------------+
     */
    for (uint32_t restart : restarts_) {
        encodeInt<uint32_t>(&buffer_, restart);
    }

    encodeInt(&buffer_, static_cast<uint32_t>(restarts_.size()));
    finished_ = true;
    return {buffer_};
}

std::size_t BlockBuilder::sizeEstimate() const {
    return (buffer_.size() + restarts_.size() * sizeof(uint32_t) + sizeof(uint32_t));
}

void BlockBuilder::reset() {
    buffer_.clear();
    last_key_.clear();
    counter_ = 0;
    finished_ = false;
    restarts_.clear();
    restarts_.push_back(0);
}

} // namespace pl