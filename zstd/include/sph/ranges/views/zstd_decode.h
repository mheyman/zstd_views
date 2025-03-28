#pragma once
#include <format>
#include <ranges>
#include <stdexcept>
#include <sph/ranges/views/detail/zstd_decompress.h>

namespace sph::ranges::views
{
    namespace detail
    {
        // Custom view that takes a zstd-encoded range and converts it to a decompressed range.
        template<std::ranges::viewable_range R, typename T>
            requires std::ranges::input_range<R> && std::is_standard_layout_v<T>
        class zstd_decode_view : public std::ranges::view_interface<zstd_decode_view<R, T>> {
            R input_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        public:
            explicit zstd_decode_view(R&& input)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
                : input_(std::forward<R>(input)) {}

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

            struct sentinel;
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

                iterator(std::ranges::const_iterator_t<R> begin, std::ranges::const_sentinel_t<R> end)
                    : current_(std::move(begin)), end_(std::move(end))
                {
                    load_next_value();
                }

                auto operator++(int) -> iterator&
                {
                    auto ret{ *this };
                    load_next_value();
                    return ret;
                }

                auto operator++() -> iterator&
                {
                    load_next_value();
                    return *this;
                }

                auto equals(const iterator& i) const noexcept -> bool
                {
                    return current_ == i.current_ && decompress_.in().pos == i.decompress_.in().pos && decompress_.out().pos == i.decompress_.out().pos;
                }

            	auto equals(const sentinel&) const noexcept -> bool
                {
                    return at_end_;
                }

                auto operator*() const -> value_type
                {
	                return value_;
                }

                auto operator==(const iterator& other) const noexcept -> bool { return equals(other); }
                auto operator==(const sentinel&s) const noexcept -> bool { return equals(s); }
                auto operator!=(const iterator& other) const noexcept -> bool { return !equals(other); }
                auto operator!=(const sentinel&s) const noexcept -> bool { return !equals(s); }

            private:
                void load_next_value()
                {
                    std::span<uint8_t> vs{ reinterpret_cast<uint8_t*>(&value_), sizeof(value_type) };
                    for (auto t : std::views::enumerate(vs))
                    {
                        auto [v_count, v] {t};
                        if (decompress_.out().pos >= decompress_.out().size)
                        {
                            if (load_next_out() == false)
                            {
                                if (v_count > 0)
                                {
                                    throw std::runtime_error(std::format("zstd_decode: Partial type at end of data. Required {} bytes, received {}.", sizeof(value_type), v_count));
                                }

                                at_end_ = true;
                                if (!maybe_done_)
                                {
                                    throw std::runtime_error("zstd_decode: Truncated input. Failed decompression at end of input.");
                                }

                                return;
                            }
                        }

                        v = static_cast<uint8_t const*>(decompress_.out().dst)[decompress_.out().pos];
                        ++decompress_.out().pos;
                    }
                }

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
                    return true;
                }

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
                            decompress_.in_src()[i] = static_cast<uint8_t>(*current_);
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
	                        decompress_.in_src()[i] = reinterpret_cast<uint8_t const*>(&current)[current_pos_];
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
                auto operator==(const sentinel& other) const -> bool { return true; }
                auto operator==(const iterator& i) const -> bool { return i.equals(*this); }
                auto operator!=(const sentinel& other) const -> bool { return false; }
                auto operator!=(const iterator& i) const -> bool { return !i.equals(*this); }
            };

            iterator begin() const { return iterator(std::ranges::begin(input_), std::ranges::end(input_)); }

            sentinel end() const { return sentinel{}; }
        };

        template<std::ranges::viewable_range R, typename T = uint8_t>
        zstd_decode_view(R&&) -> zstd_decode_view<R, T>;

        template <typename T>
        struct zstd_decode_fn : std::ranges::range_adaptor_closure<zstd_decode_fn<T>>
        {
            template <std::ranges::viewable_range R>
            [[nodiscard]] constexpr auto operator()(R&& range) const -> zstd_decode_view<std::views::all_t<R>, T>
            {
                return zstd_decode_view<std::views::all_t<R>, T>(std::views::all(std::forward<R>(range)));
            }
        };
    }
}

namespace sph::views
{
    template<typename T = uint8_t>
    auto zstd_decode() -> sph::ranges::views::detail::zstd_decode_fn<T>
    {
        return {};
    }
}