/**
 * @file TinyVector.h
 * @brief A small vector for Arduino. Its memory comes from PSRAM when the board
 * has it, otherwise from the normal heap.
 *
 * On the ESP32 Arduino core, BOARD_HAS_PSRAM is defined for boards with PSRAM
 * enabled. Define TINY_EMAIL_USE_PSRAM to use PSRAM in any case. If PSRAM
 * cannot give the memory at run time, the normal heap is used.
 */
#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <new>  // placement new

#include "TinyLogger.h"

#if defined(ESP32) && (defined(BOARD_HAS_PSRAM) || defined(TINY_EMAIL_USE_PSRAM))
#include <esp32-hal-psram.h>  // ps_malloc
#define TINY_VECTOR_USE_PSRAM 1
#else
#define TINY_VECTOR_USE_PSRAM 0
#endif

/**
 * @brief Dynamic array of T with copy semantics. T needs a default constructor
 * and a copy constructor.
 *
 * @code
 * TinyVector<int> v;
 * v.push_back(1);
 * for (size_t i = 0; i < v.size(); i++) { ... v[i] ... }
 * @endcode
 */
template <typename T>
class TinyVector {
 public:
  TinyVector() = default;

  /// Creates a vector with count default constructed elements
  explicit TinyVector(size_t count) {
    if (!reserve(count)) {
      return;  // empty: no memory
    }
    while (size_ < count) {
      new (&data_[size_]) T();
      size_++;
    }
  }

  TinyVector(const TinyVector& other) { copyFrom(other); }

  TinyVector& operator=(const TinyVector& other) {
    if (this != &other) {
      clear();
      copyFrom(other);
    }
    return *this;
  }

  /// Takes the elements of other (other becomes empty)
  TinyVector(TinyVector&& other) noexcept { steal(other); }

  TinyVector& operator=(TinyVector&& other) noexcept {
    if (this != &other) {
      clear();
      free(data_);
      steal(other);
    }
    return *this;
  }

  ~TinyVector() {
    clear();
    free(data_);
  }

  /// Appends a copy of value. Returns false if there is no memory.
  bool push_back(const T& value) {
    if (size_ == capacity_ && !reserve(capacity_ == 0 ? 4 : capacity_ * 2)) {
      return false;
    }
    new (&data_[size_]) T(value);
    size_++;
    return true;
  }

  /// Makes room for at least count elements. Returns false if there is no memory.
  bool reserve(size_t count) {
    if (count <= capacity_) {
      return true;
    }
    T* grown = static_cast<T*>(allocate(count * sizeof(T)));
    if (grown == nullptr) {
      TinyLogger::instance().error(String("TinyVector: out of memory for ") + String(static_cast<unsigned long>(count)) +
                   " elements");
      return false;
    }
    for (size_t i = 0; i < size_; i++) {
      new (&grown[i]) T(data_[i]);
      data_[i].~T();
    }
    free(data_);
    data_ = grown;
    capacity_ = count;
    return true;
  }

  /// Removes all elements (the memory stays)
  void clear() {
    for (size_t i = 0; i < size_; i++) {
      data_[i].~T();
    }
    size_ = 0;
  }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return size_ == 0; }

  /// Element at index. index must be smaller than size() (checked in debug builds).
  T& operator[](size_t index) {
    assert(index < size_);
    return data_[index];
  }
  const T& operator[](size_t index) const {
    assert(index < size_);
    return data_[index];
  }

  /// Element at index, or nullptr if index is out of range
  T* at(size_t index) { return index < size_ ? &data_[index] : nullptr; }
  const T* at(size_t index) const { return index < size_ ? &data_[index] : nullptr; }

  T* begin() { return data_; }
  T* end() { return data_ + size_; }
  const T* begin() const { return data_; }
  const T* end() const { return data_ + size_; }

  /// true if the current memory of the elements was taken from PSRAM
  bool inPsram() const { return inPsram_; }

 private:
  T* data_ = nullptr;
  size_t size_ = 0;
  size_t capacity_ = 0;
  bool inPsram_ = false;

  void steal(TinyVector& other) {
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    inPsram_ = other.inPsram_;
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    other.inPsram_ = false;
  }

  void copyFrom(const TinyVector& other) {
    if (!reserve(other.size_)) {
      return;
    }
    for (size_t i = 0; i < other.size_; i++) {
      new (&data_[size_]) T(other.data_[i]);
      size_++;
    }
  }

  /// Memory for bytes: PSRAM first if it is compiled in, then the heap
  void* allocate(size_t bytes) {
    void* memory = nullptr;
#if TINY_VECTOR_USE_PSRAM
    memory = ps_malloc(bytes);
#endif
    inPsram_ = memory != nullptr;
    if (memory == nullptr) {
      memory = malloc(bytes);
    }
    return memory;
  }
};
