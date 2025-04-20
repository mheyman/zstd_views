#pragma once
#include <cassert>
#include <format>
#include <ranges>
#include <stdexcept>
#include <sph/ranges/views/detail/zstd_compress.h>

namespace sph::ranges::views
{
    namespace detail
    {
        /**
         * @brief A view that encodes binary data into zstd-compressed data.
         * @tparam R The input range type.
         * @tparam T The output range value type.
         */
        template<std::ranges::viewable_range R, typename T>
            requires std::ranges::input_range<R> && std::is_standard_layout_v<T>
        class zstd_encode_view : public std::ranges::view_interface<zstd_encode_view<R, T>> {
            int compression_level_;
            R input_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        public:
            explicit zstd_encode_view(int compression_level, R&& input)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
                : compression_level_{ compression_level }, input_(std::forward<R>(input)) {}

            zstd_encode_view(zstd_encode_view const&) = default;
            zstd_encode_view(zstd_encode_view&&) = default;
            ~zstd_encode_view() noexcept = default;
            auto operator=(zstd_encode_view const&)->zstd_encode_view & = default;
            auto operator=(zstd_encode_view&& o) noexcept -> zstd_encode_view&
            {
                // not sure why "= default" doesn't work here...
				compression_level_ = std::forward<zstd_encode_view>(o).compression_level_;
                input_ = std::move(std::forward<zstd_encode_view>(o).input_);
                return *this;
            }

            /**
             * Forward declaration of the zstd_encode_view end-of-sequence
             * sentinel.
             */
            struct sentinel;

            /**
             * The iterator for the zstd_encode_view providing a view of the
             * compressed stream.
             *
             * This uses the zstd_compressor class to do the work.
             */
            class iterator
            {
            public:
                using iterator_category = std::input_iterator_tag;
                using value_type = T;
                using difference_type = std::ptrdiff_t;
                using input_type = std::remove_cvref_t<std::ranges::range_value_t<R>>;
            private:
                zstd_compressor compress_;
                std::ranges::const_iterator_t<R> current_;
                size_t current_pos_{ 0 };
                std::ranges::const_sentinel_t<R> end_;
                value_type value_;
                // only need skippable_frame_ if sizeof(value_type) > 1
                struct empty {};
                using skippable_frame_t = std::conditional_t<sizeof(value_type) == 1, empty, std::optional<std::vector<uint8_t>>>;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"
#endif
                [[no_unique_address]] skippable_frame_t skippable_frame_{};
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                bool reading_complete_{ false };
                bool compressing_complete_{ false };
                bool at_end_{ false };
            public:
                /**
                 * Initialize a new instance of the zstd_encode_view::iterator
                 * class.
                 * @param compression_level Value clamped to ZSTD_minCLevel()
                 * and ZSTD_maxCLevel(). Zero for default compression (usually
                 * maps to compression level 3). Higher values compress more.
                 * @param begin The start of the input range to compress.
                 * @param end The end of the input range.
                 */
                iterator(int compression_level, std::ranges::const_iterator_t<R> begin, std::ranges::const_sentinel_t<R> end)
                    : compress_{ compression_level }, current_(begin), end_(end)
                {
                    load_next_value();
                }

                iterator() : compress_{}, current_{ std::ranges::sentinel_t<R>{} }, end_{}, value_ {} {}
                iterator(iterator const&) = default;
                iterator(iterator&&) = default;
                ~iterator() = default;
                iterator& operator=(iterator const&) = default;
                iterator& operator=(iterator&&) = default;

                /**
                 * Compare the provided iterator for equality.
                 * @param i The iterator to compare against.
                 * @return True if the provided iterator is the same as this one.
                 */
                auto equals(const iterator& i) const noexcept -> bool
                {
                    return current_ == i.current_ && compress_.in_pos() == i.compress_.in_pos() && compress_.out_pos() == i.compress_.out_pos();
                }

                /**
                 * Compare the provided sentinel for equality.
                 * @return True if at the end of the compressed view.
                 */
                auto equals(const sentinel&) const noexcept -> bool
                {
                    return at_end_;
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
                 * Gets the current decompressed value.
                 * @return The current decompressed value.
                 */
                auto operator*() const -> value_type
                {
                    return value_;
                }

                auto operator==(const iterator& other) const noexcept -> bool { return equals(other); }
                auto operator==(const sentinel& s) const noexcept -> bool { return equals(s); }
                auto operator!=(const iterator& other) const noexcept -> bool { return !equals(other); }
                auto operator!=(const sentinel& s) const noexcept -> bool { return !equals(s); }

            private:
                /**
				 * Get a skippable frame that can be appended to the end of a compressed buffer to make it a multiple of value_type elements.
                 * @param remaining_length How many bytes are needed to fill out a value_type at the end of data.
                 * @return A vector of uint8_t that is reverse of what must be appended to the end of a compressed buffer to make it a multiple of value_type elements.
                 */
                static auto skippable_frame(size_t remaining_length) -> std::vector<uint8_t>
                {
					assert(remaining_length > 0 && remaining_length < sizeof(value_type));
					size_t const length{ ((((remaining_length + 8) / sizeof(value_type)) + 1) * sizeof(value_type)) - (sizeof(value_type) - remaining_length) };
					std::vector<uint8_t> ret(length, static_cast<uint8_t>(0xCD));
					uint32_t user_length = static_cast<uint32_t>(length - 8);
					uint32_t constexpr zstd_user_frame_magic{ 0x184D2A50 }; // 0x184D2A5[0-F] works.
                    uint32_t magic{ zstd_user_frame_magic };
                    if constexpr (std::endian::native == std::endian::little)
                    {
						magic = std::byteswap(magic);
						user_length = std::byteswap(user_length);
                    }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                    std::copy_n(reinterpret_cast<uint8_t const*>(&magic), sizeof(magic), ret.data() + ret.size() - sizeof(magic));
                    std::copy_n(reinterpret_cast<uint8_t const*>(&user_length), sizeof(user_length), ret.data() + ret.size() - sizeof(magic) - sizeof(user_length));
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                    return ret;
                }

                /**
                 * Sets _value to the next compressed value.
                 */
                void load_next_value()
                {
                    if constexpr(sizeof(value_type) > 1)
                    {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
                        std::span<uint8_t, sizeof(value_type)> value_span{ reinterpret_cast<uint8_t*>(&value_), sizeof(value_type) };
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                        if (skippable_frame_)
	                    {
	                    	if (skippable_frame_->empty())
	                    	{
	                    		at_end_ = true;
	                    		return;
	                    	}

	                    	for (auto &v : value_span)
	                    	{
	                    		v = skippable_frame_->back();
	                    		skippable_frame_->pop_back();
	                    	}

	                    	return;
	                    }
                    
	                    for (auto [v_count, v] : std::views::enumerate(value_span))
	                    {
	                        if (compress_.out().pos >= compress_.out().size)
	                        {
                                while (true)
		                        {
			                        if (load_next_out() == false)
			                        {
			                        	if (v_count > 0)
			                        	{
                                            skippable_frame_ = skippable_frame(sizeof(value_type) - static_cast<size_t>(v_count));
			                        		for (auto &v1 : value_span.subspan(static_cast<size_t>(v_count)))
			                        		{
			                        			v1 = skippable_frame_->back();
			                        			skippable_frame_->pop_back();
			                        		}

			                        		return;
			                        	}

			                        	at_end_ = true;
			                        	return;
			                        }

		                        	if (compress_.out_size() > 0)
		                        	{
		                        		break;
		                        	}
		                        }
	                        }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                            v = static_cast<uint8_t const*>(compress_.out().dst)[compress_.out().pos];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                            ++compress_.out().pos;
	                    }
                    }
                    else
                    {
                        if (compress_.out().pos >= compress_.out().size)
                        {
                            while (true)
                            {
	                            if (load_next_out() == false)
	                            {
	                            	at_end_ = true;
	                            	return;
	                            }

                            	if (compress_.out_size() > 0)
                            	{
                            		break;
                            	}
                            }
                        }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                        value_ = static_cast<value_type*>(compress_.out().dst)[compress_.out().pos];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                        ++compress_.out().pos;
                    }
                }

                /**
                 * Performs compression on the next chunk, loading the next chunk as needed.
                 * @return True if more input; false otherwise.
                 */
                auto load_next_out() -> bool
                {
                    if (compressing_complete_)
                    {
                        return false;
                    }

                    if (compress_.in().pos < compress_.in().size)
                    {
                        // not done processing the in buffer.
                        compressing_complete_ = compress_(reading_complete_ ? ZSTD_e_end : ZSTD_e_continue);
                        return true;
                    }

                    if (!reading_complete_)
                    {
                        // not done reading from the input range
                        if (!load_next_in())
                        {
                            // reading_complete_ == true, done reading from the input range.
                            if (compressing_complete_)
                            {
	                            return false;
                            }

                            compressing_complete_ = compress_(ZSTD_e_end);
                            return true;
                        }

                        // load_next_in() can set reading_complete_ to true.
                        compressing_complete_ = compress_(reading_complete_ ? ZSTD_e_end : ZSTD_e_continue);
                    }
                    else
                    {
                        compressing_complete_ = compress_(ZSTD_e_end);
                    }

                    return !(compressing_complete_ && compress_.out_size() == 0);
                }

                /**
                 * Load the next chunk into the buffer the compressor works on.
                 * @return True if not at end; false otherwise.
                 */
                auto load_next_in() -> bool
                {
                    if (current_ == end_)
                    {
                        reading_complete_ = true;
                        return false;
                    }

                    size_t i{ 0 };
                    while (true)
                    {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                        compress_.in_src()[i] = reinterpret_cast<uint8_t const*>(&*current_)[current_pos_++];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                        ++i;
                        if (i == compress_.in_max_size())
                        {
                            if (current_pos_ == sizeof(input_type))
                            {
                                ++current_;
                                current_pos_ = 0;
                            }

                            compress_.in().size = i;
                            compress_.in().pos = 0;
                            return true;
                        }

                        if (current_pos_ == sizeof(input_type))
                        {
                            ++current_;
                            if (current_ == end_)
                            {
                                reading_complete_ = true;
                                compress_.in().size = i;
                                compress_.in().pos = 0;
                                if (i == 0)
                                {
                                    return false;
                                }

                                return true;
                            }

                            current_pos_ = 0;
                        }
                    }
                }
            };

            struct sentinel
            {
                auto operator==(const sentinel& /*other*/) const noexcept -> bool { return true; }
                auto operator==(const iterator& i) const noexcept -> bool { return i.equals(*this); }
                auto operator!=(const sentinel& /*other*/) const noexcept -> bool { return false; }
                auto operator!=(const iterator& i) const noexcept -> bool { return !i.equals(*this); }
            };

            iterator begin() const { return iterator(compression_level_, std::ranges::begin(input_), std::ranges::end(input_)); }

            sentinel end() const { return sentinel{}; }
        };

        template<std::ranges::viewable_range R, typename T = uint8_t>
        zstd_encode_view(R&&) -> zstd_encode_view<R, T>;

        /**
         * Functor that, given a range, provides a zstd compressed view of that range.
         * @tparam T The type to compress into.
         */
        template <typename T>
        class zstd_encode_fn : public std::ranges::range_adaptor_closure<zstd_encode_fn<T>>
        {
            int compression_level_;
        public:
            zstd_encode_fn(int compression_level) : compression_level_{compression_level}{}
            template <std::ranges::viewable_range R>
            [[nodiscard]] constexpr auto operator()(R&& range) const -> zstd_encode_view<std::views::all_t<R>, T>
            {
                return zstd_encode_view<std::views::all_t<R>, T>(compression_level_, std::views::all(std::forward<R>(range)));
            }
        };
    }
}

namespace sph::views
{
	/**
	 * A range adaptor that represents view of an underlying sequence after
	 * applying zstd compression to each element.
	 *
     * Where the fastest compression occurs varies widely depending on the
     * compressibility of the content. If the content can be easily compressed,
     * you may get the fastest compression between -100 and -25. If the content
     * doesn't compress as well, the negative compression levels will probably
     * result in a small net expansion.
     *
     * Likewise, the positive compression level performance can vary widely.
     * For easily compressed data, you may get the fastest compression at
     * level 5 and not get appreciable better compression until level 22
     * which, in this case, can take twice as long to compress as level 21.
     * On less compressible data, you may not see much of a difference in
     * compression until level 16 even though level 15 may take more than
     * four times longer than level 1 or 4.
     *
     * @tparam T The type to compress into. Defaults to uint8_t. Larger types may end up with a zstd skippable frame as padding.
     * @param compression_level The zstd compression level to apply when
	 * performing the compression. Clamped by the ZSTD_minCLevel() and
	 * ZSTD_maxCLevel() values. The minimum value is typically -131072. The
	 * maximum value is typically 22.
	 *
     * Know your data if you want the best results for your situation.

    * @return a functor that takes a range and returns a zstd compressed view of that range.
	 */
	template<typename T = uint8_t>
    auto zstd_encode(int compression_level = 0) -> sph::ranges::views::detail::zstd_encode_fn<T>
    {
        return {compression_level};
    }
}
