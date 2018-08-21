// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __FUZZ_SPAN_H
#define __FUZZ_SPAN_H

#include <unistd.h>

#include <algorithm>

namespace fuzz {

template <typename T>
class span {
 public:
  typedef T value_type;

  constexpr span() : span<T>(nullptr, nullptr) {}
  constexpr span(T* begin, size_t size) : begin_(begin), end_(begin + size) {}
  constexpr span(T* begin, T* end) : begin_(begin), end_(end) {}

  template <class Container>
  constexpr span(Container& container)
      : begin_(container.begin()), end_(container.end()){};

  constexpr T* begin() const { return begin_; }
  constexpr T* end() const { return end_; }

  constexpr T* data() const { return begin_; }

  constexpr bool empty() const { return begin_ == end_; }
  constexpr size_t size() const { return end_ - begin_; }

 private:
  T* begin_;
  T* end_;
};

template <typename Source, typename Destination>
size_t CopyWithPadding(Source source,
                       Destination destination,
                       typename Destination::value_type fill_value) {
  if (source.size() >= destination.size()) {
    std::copy(source.begin(), source.begin() + destination.size(),
              destination.begin());
    return destination.size();
  }
  std::copy(source.begin(), source.end(), destination.begin());
  std::fill(destination.begin() + source.size(), destination.end(), fill_value);
  return source.size();
}

}  // namespace fuzz

#endif  // __FUZZ_SPAN_H
