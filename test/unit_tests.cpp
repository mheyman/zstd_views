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

TEST_CASE("zstd.foo")
{
	auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(1000)) | std::ranges::to<std::vector>() };
	auto compressed{ truth | sph::views::zstd_encode() | std::ranges::to<std::vector>()};
	CHECK_LT(compressed.size(), truth.size() * sizeof(size_t));
}

TEST_CASE("zstd.wont_compile")
{
	std::array<wont_compile, 4> a{ {wont_compile{1}, wont_compile{2}, wont_compile{3}, wont_compile{4}} };
	// auto encoded{ a | sph::views::zstd_encode() };
	std::array<uint8_t, 8> b{ {1, 2, 3, 4, 5, 6, 7, 8} };
	// auto decoded{ b | sph::views::zstd_decode<wont_compile>() };
}
