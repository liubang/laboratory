//=====================================================================
//
// comparator.cpp -
//
// Created by liubang on 2023/05/31 11:31
// Last Modified: 2023/05/31 11:31
//
//=====================================================================

#include "cpp/misc/sst/comparator.h"

namespace pl {

Comparator::~Comparator() = default;

class BytewiseComparator : public Comparator {
public:
    BytewiseComparator() = default;

    [[nodiscard]] const char *name() const override { return "BytewiseComparator"; };

    [[nodiscard]] int compare(const Binary &a, const Binary &b) const override {
        return a.compare(b);
    }
};

Comparator *bytewiseComparator() {
    return new BytewiseComparator();
}

} // namespace pl