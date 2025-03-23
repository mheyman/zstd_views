#include <algorithm>
#include <doctest/doctest.h>

#include <array>
#include <sph/ranges/views/zstd_encode.h>
#include <sph/ranges/views/zstd_decode.h>
#include <ranges>
#include <vector>
#include <fmt/format.h>
#include <fmt/ranges.h>

namespace
{
	class wont_compile
	{
		size_t value_;
	public:
		wont_compile(size_t v) : value_{ v }{}
		virtual ~wont_compile(){}; // virtual function makes it non-standard layout
	};
}

TEST_CASE("zstd.basic")
{
	auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(1'000)) | std::ranges::to<std::vector>() };
	auto compressed{ truth | sph::views::zstd_encode(0) | std::ranges::to<std::vector>()};
	CHECK_LT(compressed.size(), truth.size() * sizeof(size_t));
	auto check{ compressed | sph::views::zstd_decode<size_t>() | std::ranges::to<std::vector>() };
	CHECK_EQ(check.size(), truth.size());
	std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
		{
			auto [t, c] {v};
			CHECK_EQ(t, c);
		});
}

TEST_CASE("zstd.bigger")
{
	auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(10'000'000)) | std::ranges::to<std::vector>() };
	auto compressed{ truth | sph::views::zstd_encode<size_t>(0) | std::ranges::to<std::vector>() };
	fmt::print("Compressed size: {} ({} * {})\n", compressed.size()* sizeof(size_t), compressed.size(), sizeof(size_t));
	CHECK_LT(compressed.size() * sizeof(size_t), truth.size() * sizeof(size_t));
	auto check{ compressed | sph::views::zstd_decode<size_t>() | std::ranges::to<std::vector>() };
	CHECK_EQ(check.size(), truth.size());
	std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
		{
			auto [t, c] {v};
			CHECK_EQ(t, c);
		});
}

TEST_CASE("zstd.encode_decode")
{
	auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(1'000'000)) | std::ranges::to<std::vector>() };
	auto check{ truth | sph::views::zstd_encode(0) | sph::views::zstd_decode<size_t>() | std::ranges::to<std::vector>() };
	CHECK_EQ(check.size(), truth.size());
	std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
		{
			auto [t, c] {v};
			CHECK_EQ(t, c);
		});
}


TEST_CASE("zstd.wont_compile")
{
	[[maybe_unused]] std::array<wont_compile, 4> a{ {wont_compile{1}, wont_compile{2}, wont_compile{3}, wont_compile{4}} };
	// auto encoded{ a | sph::views::zstd_encode() };
	[[maybe_unused]] std::array<uint8_t, 8> b{ {1, 2, 3, 4, 5, 6, 7, 8} };
	// auto decoded{ b | sph::views::zstd_decode<wont_compile>() };
}
