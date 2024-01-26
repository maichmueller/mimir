#ifndef MIMIR_BUFFER_BYTE_STREAM_SEGMENTED_HPP_
#define MIMIR_BUFFER_BYTE_STREAM_SEGMENTED_HPP_

#include "../common/mixins.hpp"

#include <cassert>
#include <cstddef>
#include <vector>


namespace mimir {

/// @tparam N the amount of bytes per segments.
///         Reasonable numbers are 10000-100000.
template<size_t N>
class ByteStreamSegmented {
private:
    std::vector<std::vector<char>> m_segments;

    size_t cur_segment_id;
    size_t cur_segment_pos;

    size_t size;
    size_t capacity;

    size_t last_written;

    void increase_capacity() {
        m_segments.resize(m_segments.size() + 1);
        m_segments.back().reserve(N);
        ++cur_segment_id;
        cur_segment_pos = 0;
        capacity += N;
    }

public:
    ByteStreamSegmented()
        : cur_segment_id(0)
        , cur_segment_pos(0)
        , size(0)
        , capacity(0)
        , last_written(0) {
        // allocate first block of memory
        increase_capacity();
    }

    /// @brief Write the data starting from the cur_segment_pos
    ///        in the segment with cur_segment_id, if it fits,
    ///        and otherwise, push_back a new segment first.
    /// @param data
    /// @param size
    /// @return
    char* write(const char* data, size_t amount) {
        if (amount > (N - m_segments.back().size())) {
            increase_capacity();
        }
        auto& cur_segment = m_segments.back();
        char* result_data = cur_segment.end().base();
        cur_segment.insert(cur_segment.end(), data, data + amount);
        size += amount;
        last_written += amount;
        return result_data;
    }

    /// @brief Undo the last write operation.
    /// @param amount
    void undo_last_write() {
        m_segments.back().resize(m_segments.back().size() - last_written);
        last_written = 0;
    }

    /// @brief Set the write head to the beginning.
    void clear() {
        cur_segment_id = 0;
        cur_segment_pos = 0;
        size = 0;
        last_written = 0;
    }

    [[nodiscard]] const char* get_data(size_t segment_id) const {
        assert(segment_id <= m_segments.size());
        m_segments[segment_id].data();
    }
    [[nodiscard]] size_t get_size(size_t segment_id) const {
        assert(segment_id <= m_segments.size());
        return m_segments[segment_id].size();
    }

    [[nodiscard]] size_t get_size() const { return size; }
    [[nodiscard]] size_t get_capacity() const { return capacity; }
};

}  // namespace mimir

#endif // MIMIR_BUFFER_BYTE_STREAM_SEGMENTED_HPP_