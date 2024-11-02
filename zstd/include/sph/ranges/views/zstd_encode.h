#pragma once
#include <format>
#include <ranges>
#include <stdexcept>
#include <sph/ranges/views/detail/zstd_compress.h>

namespace sph::ranges::views
{
    namespace detail
    {
        // Custom transform view that filters bytes and then converts every 4 bytes into 5 bytes.
        template<std::ranges::viewable_range R, typename T>
            requires std::ranges::input_range<R>&& std::is_standard_layout_v<T>
        class zstd_encode_view : public std::ranges::view_interface<zstd_encode_view<R, T>> {
            R input_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
        public:
            explicit zstd_encode_view(R&& input)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
                : input_(std::forward<R>(input)) {}

            zstd_encode_view(zstd_encode_view const&) = default;
            zstd_encode_view(zstd_encode_view&&) = default;
            ~zstd_encode_view() noexcept = default;
            auto operator=(zstd_encode_view const&)->zstd_encode_view & = default;
            auto operator=(zstd_encode_view&& o) noexcept -> zstd_encode_view&
            {
                // not sure why "= default" doesn't work here...
                input_ = std::move(std::forward<zstd_encode_view>(o).input_);
                return *this;
            }

            struct sentinel;
            class iterator
            {
            public:
                using iterator_category = std::input_iterator_tag;
                using value_type = T;
                using difference_type = std::ptrdiff_t;
                using input_type = std::remove_cvref_t<std::ranges::range_value_t<R>>;
            private:
                zstd_compressor compress_;
                std::ranges::iterator_t<R> current_;
                size_t current_pos_{ 0 };
                std::ranges::iterator_t<R> end_;
                value_type value_;
                bool reading_complete_{ false };
                bool compressing_complete_{ false };
                bool at_end_{ false };
            public:

                iterator(int compression_level, std::ranges::iterator_t<R> begin, std::ranges::iterator_t<R> end)
                    : compress_{ compression_level }, current_(begin), end_(end)
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

                auto equals(const iterator& i) const -> bool
                {
                    return current_ == i.current_ && compress_.in().pos == i.compress_.in().pos && compress_.out().pos == i.compress_.out().pos;
                }

                auto equals(const sentinel&) const -> bool
                {
                    return at_end_;
                }

                auto operator*() const -> value_type
                {
                    return value_;
                }

                auto operator==(const iterator& other) const -> bool { return equals(other); }
                auto operator==(const sentinel& s) const -> bool { return equals(s); }
                auto operator!=(const iterator& other) const -> bool { return !equals(other); }
                auto operator!=(const sentinel& s) const -> bool { return !equals(s); }

            private:
                void load_next_value()
                {
                    for (auto [v_count, v] : std::views::enumerate(std::span { reinterpret_cast<uint8_t*>(&value_), sizeof(value_type) }))
                    {
                        if (compress_.out().pos >= compress_.out().size)
                        {
                            if (load_next_out() == false)
                            {
                                if (v_count > 0)
                                {
                                    throw std::runtime_error(std::format("zstd_encode: Partial type at end of data. Required {} bytes, received {}.", sizeof(value_type), v_count));
                                }

                                at_end_ = true;

                                return;
                            }
                        }

                        v = static_cast<uint8_t const*>(compress_.out().dst)[compress_.out().pos];
                        ++compress_.out().pos;
                    }
                }

                auto load_next_out() -> bool
                {
                    if (compress_.in().pos < compress_.in().size())
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
                    }

                    compressing_complete_ = compress_(ZSTD_e_continue);
                    return true;
                }

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
                        static_cast<uint8_t const*>(compress_.in().src)[i] = reinterpret_cast<uint8_t const*>(&*current_)[current_pos_++];
                        if (i == compress_.in().size)
                        {
                            if (current_pos_ == sizeof(input_type))
                            {
                                ++current_;
                                current_pos_ = 0;
                            }

                            compress_.in().pos = 0;
                            return true;
                        }

                        if (current_pos_ == sizeof(input_type))
                        {
                            ++current_;
                            if (current_ == end_)
                            {
                                reading_complete_ = true;
                                if (i == 0)
                                {
                                    return false;
                                }

                                compress_.in().size = i;
                                compress_.in().pos = 0;
                                return true;
                            }

                            current_pos_ = 0;
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

            iterator begin() { return iterator(std::ranges::begin(input_), std::ranges::end(input_)); }

            sentinel end() { return sentinel{}; }
        };

        struct zstd_encode_tag
        {

        };

        template<std::ranges::viewable_range R, typename T = uint8_t>
        zstd_encode_view(R&&) -> zstd_encode_view<R, T>;

        template <typename T>
        struct zstd_encode_fn : std::ranges::range_adaptor_closure<zstd_encode_fn<T>>
        {
            template <std::ranges::viewable_range R>
            [[nodiscard]] constexpr auto operator()(R&& range) const -> zstd_encode_view<R, T>
            {
                return zstd_encode_view<R, T>(std::forward<R>(range));
            }
        };
    }
}

namespace sph::views
{
    template<typename T = uint8_t>
    auto zstd_encode(sph::ranges::views::detail::zstd_encode_tag = {}) -> sph::ranges::views::detail::zstd_encode_fn<T>
    {
        return {};
    }
}