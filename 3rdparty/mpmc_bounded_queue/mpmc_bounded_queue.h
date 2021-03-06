#pragma once

// Downloaded from http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue on 2018-09-28
// All modifications made are marked by MODIFIED: comment.

/*  Multi-producer/multi-consumer bounded queue.
 *  Copyright (c) 2010-2011, Dmitry Vyukov. All rights reserved.
 *  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *     1. Redistributions of source code must retain the above copyright notice, this list of
 *        conditions and the following disclaimer.
 *     2. Redistributions in binary form must reproduce the above copyright notice, this list
 *        of conditions and the following disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *  THIS SOFTWARE IS PROVIDED BY DMITRY VYUKOV "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 *  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 *  DMITRY VYUKOV OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *  The views and conclusions contained in the software and documentation are those of the authors and should not be interpreted
 *  as representing official policies, either expressed or implied, of Dmitry Vyukov.
 */

#include <atomic>
#include <cassert>
// MODIFIED: Added <utility> for std::forward.
#include <utility>

// MODIFIED: Added shuffle_pos.
template<typename T, bool shuffle_pos = false>
class mpmc_bounded_queue
{
public:
  // MODIFIED: Added typedef for BlockingQueue.
  using ElementType = T;

  mpmc_bounded_queue(size_t buffer_size)
    : buffer_(new cell_t [buffer_size])
    , buffer_mask_(buffer_size - 1)
  {
    assert((buffer_size >= 2) &&
      ((buffer_size & (buffer_size - 1)) == 0));
    if constexpr (shuffle_pos && sizeof(T) < cacheline_size) {
      size_t per_line = (cacheline_size + sizeof(T) - 1) / sizeof(T);
      shuffle_bits_ = 1;
      while ((1 << shuffle_bits_) < per_line) {
        shuffle_bits_++;
      }
      if (buffer_size < (1 << (shuffle_bits_ * 2))) {
        shuffle_bits_ = 0;
      }
      low_mask_ = (1 << shuffle_bits_) - 1;
      mid_mask_ = low_mask_ << shuffle_bits_;
      up_mask_ = buffer_mask_ & ~(low_mask_ | mid_mask_);
    }
    for (size_t i = 0; i != buffer_size; i += 1)
      buffer_[get_index(i)].sequence_.store(i, std::memory_order_relaxed);
    enqueue_pos_.store(0, std::memory_order_relaxed);
    dequeue_pos_.store(0, std::memory_order_relaxed);
  }

  ~mpmc_bounded_queue()
  {
    delete [] buffer_;
  }

  // MODIFIED: Switched to universal references to support move-only data.
  template<typename U>
  bool enqueue(U&& data)
  {
    cell_t* cell;
    size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    for (;;)
    {
      // MODIFIED: Added shuffle_pos.
      cell = &buffer_[get_index(pos)];
      size_t seq =
        cell->sequence_.load(std::memory_order_acquire);
      intptr_t dif = (intptr_t)seq - (intptr_t)pos;
      if (dif == 0)
      {
        if (enqueue_pos_.compare_exchange_weak
            (pos, pos + 1, std::memory_order_relaxed))
          break;
      }
      else if (dif < 0)
        return false;
      else
        pos = enqueue_pos_.load(std::memory_order_relaxed);
    }
    // MODIFIED: Switched to forwarding to support move-only data.
    cell->data_ = std::forward<U>(data);
    // MODIFIED: Use seq_cst instead of release as a way to prevent reordering of memory accesses
    // before it (see blockingtaskqueue.h for more details).
    cell->sequence_.store(pos + 1, std::memory_order_seq_cst);
    return true;
  }

  bool dequeue(T& data)
  {
    cell_t* cell;
    size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    for (;;)
    {
      // MODIFIED: Added shuffle_pos.
      cell = &buffer_[get_index(pos)];
      // MODIFIED: Use seq_cst instead of acquire as a way to prevent reordering of the memory
      // accesses around it (see blockingtaskqueue.h for more details).
      size_t seq =
        cell->sequence_.load(std::memory_order_seq_cst);
      intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
      if (dif == 0)
      {
        if (dequeue_pos_.compare_exchange_weak
            (pos, pos + 1, std::memory_order_relaxed))
          break;
      }
      else if (dif < 0)
        return false;
      else
        pos = dequeue_pos_.load(std::memory_order_relaxed);
    }
    // MODIFIED: Use std::move because the data is dequeued.
    data = std::move(cell->data_);
    cell->sequence_.store(pos + buffer_mask_ + 1, std::memory_order_release);
    return true;
  }

private:
  struct cell_t
  {
    std::atomic<size_t>   sequence_;
    T                     data_;
  };

  static size_t const     cacheline_size = 64;
  typedef char            cacheline_pad_t [cacheline_size];

  cacheline_pad_t         pad0_;
  cell_t* const           buffer_;
  size_t const            buffer_mask_;
  // MODIFIED: Added shuffle_pos.
  size_t                  shuffle_bits_;
  size_t                  low_mask_;
  size_t                  mid_mask_;
  size_t                  up_mask_;
  cacheline_pad_t pad1_;
  std::atomic<size_t>     enqueue_pos_;
  cacheline_pad_t         pad2_;
  std::atomic<size_t>     dequeue_pos_;
  cacheline_pad_t         pad3_;

  mpmc_bounded_queue(mpmc_bounded_queue const&);
  void operator = (mpmc_bounded_queue const&);

  // MODIFIED: Added shuffle_pos.
  size_t get_index(size_t pos)
  {
    if constexpr (shuffle_pos && sizeof(T) < cacheline_size) {
      size_t pos_up = pos & up_mask_;
      size_t pos_mid = (pos & mid_mask_) >> shuffle_bits_;
      size_t pos_low = (pos & low_mask_) << shuffle_bits_;
      return pos_up | pos_mid | pos_low;
    } else {
      return pos & buffer_mask_;
    }
  }
};
