//=====================================================================
//
// block.cpp -
//
// Created by liubang on 2023/06/01 19:45
// Last Modified: 2023/06/01 19:45
//
//=====================================================================
#include "cpp/misc/sst/block.h"
#include "cpp/misc/sst/encoding.h"

#include <iostream>

namespace pl {

Block::Block(const BlockContents &content)
    : data_(content.data.data()), size_(content.data.size()), owned_(content.heap_allocated) {
    num_restarts_ = decodeInt<uint32_t>(data_ + size_ - 4);
    std::size_t max_num_restarts = (size_ - 4) / 4;
    if (num_restarts_ > max_num_restarts) {
        // invalid block
        size_ = 0;
    } else {
        // 计算restart的起始地址偏移
        restart_offset_ = size_ - (1 + num_restarts_) * 4;
    }
}

Block::~Block() {
    if (owned_) {
        delete[] data_;
    }
}

class Block::BlockIterator : public Iterator {
public:
    BlockIterator(const Comparator *comparator,
                  const char *data,
                  uint32_t restarts,
                  uint32_t num_restarts)
        : comparator_(comparator), data_(data), restarts_(restarts), num_restarts_(num_restarts) {}

    [[nodiscard]] bool valid() const override { return current_ < restarts_; }
    [[nodiscard]] Binary key() const override { return key_; }
    [[nodiscard]] Binary val() const override { return val_; }
    [[nodiscard]] Status status() const override { return status_; }

    void seek(const Binary &target) override {
        uint32_t left = 0;
        uint32_t right = num_restarts_ - 1;
        int current_key_compare = 0;
        if (valid()) {
            current_key_compare = compare(key_, target);
            if (current_key_compare < 0) {
                left = current_restart_;
            } else if (current_key_compare > 0) {
                right = current_restart_;
            } else {
                return;
            }
        }

        // binary search
        while (left < right) {
            uint32_t mid = (left + right + 1) / 2;
            uint32_t offset = getRestartOffset(mid);
            uint32_t shared, non_shared, value_size;
            const char *key_ptr =
                decodeEntry(data_ + offset, data_ + restarts_, &shared, &non_shared, &value_size);
            if (nullptr == key_ptr || (shared != 0)) {
                status_ = Status::NewCorruption("invalid entry in block");
                current_ = restarts_;
                current_restart_ = num_restarts_;
                key_.clear();
                val_.clear();
                return;
            }
            Binary mid_key(key_ptr, non_shared);
            if (compare(mid_key, target) < 0) {
                left = mid;
            } else {
                right = mid - 1;
            }
        }

        assert(current_key_compare == 0 || valid());
        bool skip_seek = left == current_restart_ && current_key_compare < 0;
        if (!skip_seek) {
            seekToRestartPoint(left);
        }
        while (true) {
            if (!parseNextKeyVal()) {
                return;
            }
            if (compare(key_, target) >= 0) {
                return;
            }
        }
    }

    void first() override {
        seekToRestartPoint(0);
        parseNextKeyVal();
    }

    void last() override {
        seekToRestartPoint(num_restarts_ - 1);
        while (parseNextKeyVal() && nextEntryOffset() < restarts_) {
        }
    }

    void prev() override {
        assert(valid());
        const uint32_t old = current_;
        while (getRestartOffset(current_restart_) >= old) {
            if (current_restart_ == 0) {
                current_ = restarts_;
                current_restart_ = num_restarts_;
                return;
            }
            --current_restart_;
        }
        seekToRestartPoint(current_restart_);
        do {
        } while (parseNextKeyVal() && nextEntryOffset() < old);
    }

    void next() override {
        assert(valid());
        parseNextKeyVal();
    }

    ~BlockIterator() override = default;

private:
    uint32_t getRestartOffset(uint32_t idx) {
        assert(idx < num_restarts_);
        return decodeInt<uint32_t>(data_ + idx * sizeof(uint32_t));
    }

    void seekToRestartPoint(uint32_t idx) {
        key_.clear();
        current_restart_ = idx;
        uint32_t offset = getRestartOffset(idx);
        val_ = Binary(data_ + offset, 0);
    }

    [[nodiscard]] inline uint32_t nextEntryOffset() const {
        return (val_.data() + val_.size()) - data_;
    }

    bool parseNextKeyVal() {
        current_ = nextEntryOffset();
        const char *p = data_ + current_;
        const char *limit = data_ + restarts_;
        if (p >= limit) {
            current_ = restarts_;
            current_restart_ = num_restarts_;
            return false;
        }
        uint32_t shared, non_shared, value_size;
        p = decodeEntry(p, limit, &shared, &non_shared, &value_size);
        // TODO(liubang): error handle
        if (p == nullptr || key_.size() < shared)
            return false;
        key_.resize(shared);
        key_.append(p, non_shared);
        val_ = Binary(p + non_shared, value_size);
        while (current_restart_ + 1 < num_restarts_ &&
               getRestartOffset(current_restart_) < current_) {
            ++current_restart_;
        }

        return true;
    }

    const char *decodeEntry(const char *p,
                            const char *limit,
                            uint32_t *shared,
                            uint32_t *non_shared,
                            uint32_t *value_size) {
        constexpr std::size_t s = sizeof(uint32_t);
        if (p + 3 > limit)
            return nullptr;
        *shared = pl::decodeInt<uint32_t>(p);
        *non_shared = pl::decodeInt<uint32_t>(p + s);
        *value_size = pl::decodeInt<uint32_t>(p + s * 2);
        p += s * 3;
        return p;
    }

    [[nodiscard]] inline int compare(const Binary &a, const Binary &b) const {
        return comparator_->compare(a, b);
    }

private:
    const Comparator *comparator_; // 主要是seek的时候做二分查找的
    const char *data_;             // data block content
    uint32_t const restarts_;      // restart的起始位置
    uint32_t const num_restarts_;  // restart的个数
    uint32_t current_{0};          // 当前游标的偏移
    uint32_t current_restart_{0};  // 当前是第几个restart
    std::string key_;              // 当前游标处的key
    Binary val_;                   // 当前游标处的value
    Status status_;
};

// TODO(liubang): use unique_ptr
Iterator *Block::iterator(const Comparator *comparator) {
    return new BlockIterator(comparator, data_, restart_offset_, num_restarts_);
}

} // namespace pl