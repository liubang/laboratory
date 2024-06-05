// Copyright (c) 2024 The Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: liubang (it.liubang@gmail.com)

#include "cpp/misc/sst/table_iterator.h"

namespace pl {

void TableIterator::seek(const Binary& target) {
    index_iter_->seek(target);
    initDataBlock();
    if (data_iter_ != nullptr) {
        data_iter_->seek(target);
    }
    skipEmptyDataBlock();
}

bool TableIterator::valid() const { return data_iter_ != nullptr && data_iter_->valid(); }

Binary TableIterator::key() const {
    assert(valid());
    return data_iter_->key();
}

Binary TableIterator::val() const {
    assert(valid());
    return data_iter_->val();
}

Status TableIterator::status() const {
    if (!index_iter_->status().isOk()) {
        return index_iter_->status();
    }
    if (data_iter_ != nullptr && !data_iter_->status().isOk()) {
        return data_iter_->status();
    }
    return status_;
}

void TableIterator::initDataBlock() {
    if (!index_iter_->valid()) {
        data_iter_ = nullptr;
        return;
    }
    Binary handle = index_iter_->val();
    if (data_iter_ != nullptr && handle.compare(data_block_handle_) == 0) {
        // do nothing
    } else {
        data_iter_ = block_func_(handle);
        data_block_handle_.assign(handle.data(), handle.size());
    }
}

void TableIterator::skipEmptyDataBlock() {
    while (data_iter_ == nullptr || !data_iter_->valid()) {
        if (!index_iter_->valid()) {
            data_iter_ = nullptr;
            return;
        }
        index_iter_->next();
        initDataBlock();
        if (data_iter_ != nullptr) {
            data_iter_->first();
        }
    }
}

} // namespace pl
