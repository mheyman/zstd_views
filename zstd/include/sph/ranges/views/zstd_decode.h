#pragma once
#include <format>
#include <ranges>
#include <stdexcept>
#include <sph/ranges/views/detail/zstd_decompress.h>

namespace sph::ranges::views
{
    namespace detail
    {
        /**
         * Provides view of an underlying sequence after applying zstd decompression to each element.
         * @tparam R The type of the range that holds a zstd compressed stream.
         * @tparam T The type to decompress into.
         */
        template<std::ranges::viewable_range R, typename T>
            requires std::ranges::input_range<R> && std::is_standard_layout_v<T>&& std::is_standard_layout_v<std::remove_cvref_t<std::ranges::range_value_t<R>>>
        class zstd_decode_view : public std::ranges::view_interface<zstd_decode_view<R, T>> {
            R input_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
            int window_log_max_;
        public:
            /**
             * Initialize a new instance of the zstd_decode_view class.
             *
             * Provides a begin() iterator and end() sentinel over the
             * decompressed view of the given input range.
             *
             * The given input range must comprise a valid zstd compressed
             * stream. Failure to provide a valid stream will result in a
             * std::invalid_argument exception.
             * 
             * @param window_log_max Size limit (in powers of 2) beyond which
             * the decompressor will refuse to allocate a memory buffer in
             * order to protect the host; zero for default. Valid values
             * (typically): 11 through 30 (32-bit), 11 through 31 (64-bit).
             * Out of range values will be clamped.
             * @param input the range to decompress.
             */
            zstd_decode_view(int window_log_max, R&& input)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
                : input_(std::forward<R>(input)), window_log_max_{window_log_max} {}

            zstd_decode_view(zstd_decode_view const&) = default;
            zstd_decode_view(zstd_decode_view&&) = default;
            ~zstd_decode_view() noexcept = default;
            auto operator=(zstd_decode_view const&) -> zstd_decode_view& = default;
            auto operator=(zstd_decode_view&&o) noexcept -> zstd_decode_view&
            {
                // not sure why "= default" doesn't work here...
                input_ = std::move(std::forward<zstd_decode_view>(o).input_);
                return *this;
            }

            /**
             * Forward declaration of the zstd_decode_view end-of-sequence
             * sentinel.
             */
            struct sentinel;

            /**
             * The iterator for the zstd_decode_view providing a view of the
             * decompressed stream.
             *
             * This uses the zstd_decompressor class to do the work.
             */
            class iterator
            {
            public:
                using iterator_concept = std::input_iterator_tag;
                using iterator_category = std::input_iterator_tag;
                using value_type = T;
                using difference_type = std::ptrdiff_t;
                using pointer = const T*;
                using reference = const T&;
				using input_type = std::remove_cvref_t<std::ranges::range_value_t<R>>;
            private:
                zstd_decompressor decompress_;
                std::ranges::const_iterator_t<R> current_;
                size_t current_pos_{ 0 };
                std::ranges::const_sentinel_t<R> end_;
                value_type value_;
                bool maybe_done_{ false };
                bool at_end_{ false };
            public:
                /**
                 * Initialize a new instance of the zstd_decode_view::iterator
                 * class.
                 * @param window_log_max Size limit (in powers of 2) beyond
                 * which the decompressor will refuse to allocate a memory
                 * buffer in order to protect the host; zero for default. Valid
                 * values (typically): 11 through 30 (32-bit), 11 through 31
                 * (64-bit). Out of range values will be clamped.
                 * @param begin The start of the input range to decompress.
                 * @param end The end of the input range.
                 */
                iterator(int window_log_max, std::ranges::const_iterator_t<R> begin, std::ranges::const_sentinel_t<R> end)
                    : decompress_{window_log_max}, current_(std::move(begin)), end_(std::move(end))
                {
                    load_next_value();
                }

                /**
                 * Increment the iterator.
                 * @return The pre-incremented iterator value. This iterator is
                 * a copy and cannot be used for additional compressing.
                 */
                auto operator++(int) -> iterator&
                {
                    auto ret{ *this };
                    load_next_value();
                    return ret;
                }

                /**
                 * Increment the iterator.
                 * @return The incremented iterator value.
                 */
                auto operator++() -> iterator&
                {
                    load_next_value();
                    return *this;
                }

                /**
                 * Compare the provided iterator for equality.
                 * @param i The iterator to compare against.
                 * @return True if the provided iterator is the same as this one.
                 */
                auto equals(const iterator& i) const noexcept -> bool
                {
                    return current_ == i.current_ && decompress_.in().pos == i.decompress_.in().pos && decompress_.out().pos == i.decompress_.out().pos;
                }

                /**
                 * Compare the provided sentinel for equality.
                 * @return True if at the end of the decompressed view.
                 */
                auto equals(const sentinel&) const noexcept -> bool
                {
                    return at_end_;
                }

                /**
                 * Gets the current decompressed value.
                 * @return The current decompressed value.
                 */
                auto operator*() const -> value_type
                {
	                return value_;
                }

                auto operator==(const iterator& other) const noexcept -> bool { return equals(other); }
                auto operator==(const sentinel&s) const noexcept -> bool { return equals(s); }
                auto operator!=(const iterator& other) const noexcept -> bool { return !equals(other); }
                auto operator!=(const sentinel&s) const noexcept -> bool { return !equals(s); }

            private:
                /**
                 * Sets _value to the next decompressed value.
                 *
                 * Will throw std::invalid_argument for a truncated or otherwise invalid input range.
                 */
                void load_next_value()
                {
                    if constexpr (sizeof(value_type) > 1)
                    {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
                        for (std::span<uint8_t, sizeof(value_type)> vs{ reinterpret_cast<uint8_t*>(&value_), sizeof(value_type) }; auto t : std::views::enumerate(vs))
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                        {
                            auto [v_count, v] {t};
                            if (decompress_.out().pos >= decompress_.out().size)
                            {
                                if (load_next_out() == false)
                                {
                                    if (v_count > 0)
                                    {
                                        throw std::invalid_argument(std::format("zstd_decode: Partial type at end of data. Required {} bytes, received {}.", sizeof(value_type), v_count));
                                    }

                                    at_end_ = true;
                                    if (!maybe_done_)
                                    {
                                        throw std::invalid_argument("zstd_decode: Truncated input. Failed decompression at end of input.");
                                    }

                                    return;
                                }
                            }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                            v = static_cast<uint8_t const*>(decompress_.out().dst)[decompress_.out().pos];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                            ++decompress_.out().pos;
                        }
                    }
                    else
                    {
                        // value_type is a single byte
                        if (decompress_.out().pos >= decompress_.out().size)
                        {
                            if (load_next_out() == false)
                            {
                                at_end_ = true;
                                if (!maybe_done_)
                                {
                                    throw std::invalid_argument("zstd_decode: Truncated input. Failed decompression at end of input.");
                                }

                                return;
                            }
                        }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                        value_ = static_cast<value_type*>(decompress_.out().dst)[decompress_.out().pos];
#ifdef __clang__
#pragma clang diagnostic pop
#endif

                        ++decompress_.out().pos;
                    }
                }

                /**
                 * Performs decompression on the next chunk, loading the next chunk as needed.
                 * @return True if more input; false otherwise.
                 */
                auto load_next_out() -> bool
                {
                    if (decompress_.in().pos >= decompress_.in().size)
                    {
                        if (load_next_in() == false)
                        {
                            return false;
                        }
                    }

                    maybe_done_ = decompress_();
                    return !maybe_done_ || decompress_.out().size > 0;
                }

                /**
                 * Load the next chunk into the buffer the decompressor works on.
                 * @return True if not at end; false otherwise.
                 */
                auto load_next_in() -> bool
                {
                    if (current_ == end_)
                    {
                        return false;
                    }

                    size_t i{ 0 };
                    if constexpr (sizeof(input_type) == 1)
					{
                        while (true)
                        {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                            decompress_.in_src()[i] = static_cast<uint8_t>(*current_);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                            ++i;
                            ++current_;
                            if (i == decompress_.in_max_size())
                            {
                                decompress_.in().size = i;
                                decompress_.in().pos = 0;
                                return true;
                            }

                            if (current_ == end_)
                            {
                                decompress_.in().size = i;
                                decompress_.in().pos = 0;
                                if (i == 0)
                                {
                                    return false;
                                }

                                return true;
                            }
                        }
                    }
					else
					{
	                    input_type current{ *current_ };
	                    while(true)
	                    {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
	                        decompress_.in_src()[i] = reinterpret_cast<uint8_t const*>(&current)[current_pos_];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                            ++i;
	                        ++current_pos_;
	                        if (i == decompress_.in_max_size())
	                        {
	                            if (current_pos_ == sizeof(input_type))
	                            {
	                                ++current_;
	                                current_pos_ = 0;
	                            }

	                            decompress_.in().size = i;
	                            decompress_.in().pos = 0;
	                            return true;
	                        }

	                        if (current_pos_ == sizeof(input_type))
	                        {
	                            ++current_;
	                            if (current_ == end_)
	                            {
	                                decompress_.in().size = i;
	                                decompress_.in().pos = 0;
	                                if (i == 0)
	                                {
	                                    return false;
	                                }

	                                return true;
	                            }

	                            current = *current_;
	                            current_pos_ = 0;
	                        }
	                    }
                    }
                }
            };

            struct sentinel
        	{
                auto operator==(const sentinel& /*other*/) const -> bool { return true; }
                auto operator==(const iterator& i) const -> bool { return i.equals(*this); }
                auto operator!=(const sentinel& /*other*/) const -> bool { return false; }
                auto operator!=(const iterator& i) const -> bool { return !i.equals(*this); }
            };

            iterator begin() const { return iterator(window_log_max_, std::ranges::begin(input_), std::ranges::end(input_)); }

            sentinel end() const { return sentinel{}; }
        };

        template<std::ranges::viewable_range R, typename T = uint8_t>
        zstd_decode_view(R&&) -> zstd_decode_view<R, T>;

        /**
         * Functor that, given a zstd compressed range, provides a decompressed view of that range.
         * @tparam T The type to decompress into.
         */
        template <typename T>
        class zstd_decode_fn : public std::ranges::range_adaptor_closure<zstd_decode_fn<T>>
        {
            int window_log_max_;
        public:
            explicit zstd_decode_fn(int window_log_max = 0) : window_log_max_{ window_log_max } {}
            template <std::ranges::viewable_range R>
            [[nodiscard]] constexpr auto operator()(R&& range) const -> zstd_decode_view<std::views::all_t<R>, T>
            {
                return zstd_decode_view<std::views::all_t<R>, T>(window_log_max_, std::views::all(std::forward<R>(range)));
            }
        };
    }
}

namespace sph::views
{
	/**
     * A range adaptor that represents view of an underlying sequence after applying zstd decompression to each element.
     *
     * Will fail to decompress and throw a std::invalid_argument if the provided range does not represent a valid zstd compressed stream.
     * 
     * @tparam T The type to decompress into. Should probably match, but
     * doesn't have to match, the type that was compressed from.
     * @param window_log_max Size limit (in powers of 2) beyond which the
     * decompressor will refuse to allocate a memory buffer in order to protect
     * the host; zero for default. Valid values (typically): 11 through 30
     * (32-bit), 11 through 31 (64-bit). Out of range values will be clamped.
     * @return A functor that takes a zstd compressed range and returns a view of the decompressed information.
	 */
	template<typename T = uint8_t>
    auto zstd_decode(int window_log_max = 0) -> sph::ranges::views::detail::zstd_decode_fn<T>
    {
        return sph::ranges::views::detail::zstd_decode_fn<T>{window_log_max};
    }
}
