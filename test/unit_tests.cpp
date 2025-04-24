#include <algorithm>
#include <array>
#include <chrono>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <ranges>
#include <sph/ranges/views/zstd_decode.h>
#include <sph/ranges/views/zstd_encode.h>
#include <vector>

#include "doctest_util.h"
namespace
{
	class wont_compile
	{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunknown-attributes"
#pragma clang diagnostic ignored "-Wunused-member-function"
#endif
        [[no_unique_address]] std::array<uint8_t, 1> data_{};
		size_t value_;
    public:
		wont_compile(size_t v) : value_{ v }{}
        wont_compile(wont_compile const&) = default;
        wont_compile(wont_compile&&) = default;
		virtual ~wont_compile(){} // virtual function makes it non-standard layout
        auto operator=(wont_compile const&) -> wont_compile & = default;
        auto operator=(wont_compile&&) -> wont_compile & = default;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    };

	/**
	 * Lightly ported from the example code https://github.com/facebook/zstd/blob/dev/examples/streaming_compression.c
	 *
	 * Uses vectors instead of files.
	 * 
	 * @param to_compress The buffer to compress.
	 * @param compression_level The compression level to use.
	 * @param thread_count The thread count to use.
	 * @return The compressed buffer.
	 */
	auto stream_compress_old_school(std::span<uint8_t const> to_compress, int compression_level, int thread_count) -> std::vector<uint8_t>
    {
        /* Create the input and output buffers.
         * They may be any size, but we recommend using these functions to size them.
         * Performance will only suffer significantly for very tiny buffers.
         */
        std::vector<uint8_t> buf_in(ZSTD_CStreamInSize());
        std::vector<uint8_t> buf_out(ZSTD_CStreamOutSize());

        /* Create the context. */
        ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        if (cctx == nullptr)
        {
			throw std::runtime_error("ZSTD_createCCtx() failed!");
        }

        /* Set any parameters you want.
         * Here we set the compression level, and enable the checksum.
         */
		size_t res{ ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compression_level) };
		if (ZSTD_isError(res))
        {
			throw std::runtime_error(fmt::format("ZSTD_CCtx_setParameter(ZSTD_c_compressionLevel, {}) failed! {}", compression_level, ZSTD_getErrorName(res)));
        }

		res = ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
        if (ZSTD_isError(res))
        {
            throw std::runtime_error(fmt::format("ZSTD_CCtx_setParameter(ZSTD_c_checksumFlag, 1) failed! {}", ZSTD_getErrorName(res)));
        }

        if (thread_count > 1) {
            size_t const r = ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, thread_count);
            if (ZSTD_isError(r)) {
                fmt::print("Note: the linked libzstd library doesn't support multithreading. "
                    "Reverting to single-thread mode.\n");
            }
        }

        /* This loop read from the input file, compresses that entire chunk,
         * and writes all output produced to the output file.
         */
        size_t in_pos{ 0 };
		std::vector<uint8_t> ret;
        for (;;) {
			size_t const read = std::min(buf_in.size(), to_compress.size() - in_pos);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            std::copy_n(to_compress.data() + in_pos, read, buf_in.data());
            std::copy_n(to_compress.data() + in_pos, read, buf_in.data());
#ifdef __clang__
#pragma clang diagnostic pop
#endif
            in_pos += read;

            /* Select the flush mode.
             * If the read may not be finished (read == toRead) we use
             * ZSTD_e_continue. If this is the last chunk, we use ZSTD_e_end.
             * Zstd optimizes the case where the first flush mode is ZSTD_e_end,
             * since it knows it is compressing the entire source in one pass.
             */
            ZSTD_EndDirective const mode = read < buf_in.size() ? ZSTD_e_end : ZSTD_e_continue;
            /* Set the input buffer to what we just read.
             * We compress until the input buffer is empty, each time flushing the
             * output.
             */
            ZSTD_inBuffer input = { buf_in.data(), read, 0 };
            bool finished;
            do {
                /* Compress into the output buffer and write all the output to
                 * the return buffer so we can reuse the buffer next iteration.
                 */
                ZSTD_outBuffer output = { buf_out.data(), buf_out.size(), 0 };
                size_t const remaining = ZSTD_compressStream2(cctx, &output, &input, mode);
				if (ZSTD_isError(remaining))
				{
					throw std::runtime_error(fmt::format("ZSTD_compressStream2() failed! {}", ZSTD_getErrorName(remaining)));
				}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                ret.insert(ret.end(), buf_out.data(), buf_out.data() + output.pos);
#ifdef __clang
#pragma clang diagnostic pop
#endif
                /* If we're on the last chunk we're finished when zstd returns 0,
                 * which means its consumed all the input AND finished the frame.
                 * Otherwise, we're finished when we've consumed all the input.
                 */
                finished = mode == ZSTD_e_end ? (remaining == 0) : (input.pos == input.size);
			} while (!finished);
            if(input.pos != input.size)
            {
				fmt::print("Impossible: zstd only returns 0 when the input is completely consumed!");
            }

            if (mode == ZSTD_e_end)
			{
                break;
            }
        }

        ZSTD_freeCCtx(cctx);
        return ret;
    }

	/**
	 * Lightly ported example code from https://github.com/facebook/zstd/blob/dev/examples/streaming_decompression.c.
	 *
	 * Uses vectors instead of files.
	 * 
	 * @param to_decompress The data to decompress.
	 * @return The decompressed data.
	 */
	auto stream_decompress_old_school(std::span<uint8_t> to_decompress) -> std::vector<uint8_t>
    {
		std::vector<uint8_t> buf_in(ZSTD_DStreamInSize());
		std::vector<uint8_t> buf_out(ZSTD_DStreamOutSize());

        ZSTD_DCtx* const dctx = ZSTD_createDCtx();
		if (dctx == nullptr)
        {
			throw std::runtime_error("ZSTD_createDCtx() failed!");
		}

        /* This loop assumes that the input file is one or more concatenated zstd
         * streams. This example won't work if there is trailing non-zstd data at
         * the end, but streaming decompression in general handles this case.
         * ZSTD_decompressStream() returns 0 exactly when the frame is completed,
         * and doesn't consume input after the frame.
         */
        size_t last_result = 0;
        int isEmpty = 1;
        size_t to_decompress_pos{ 0 };
		std::vector<uint8_t> ret;
        while (to_decompress_pos < to_decompress.size())
        {
			size_t read{ std::min(buf_in.size(), to_decompress.size() - to_decompress_pos) };
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            std::copy_n(to_decompress.data() + to_decompress_pos, read, buf_in.data());
#ifdef __clang__
#pragma clang diagnostic pop
#endif
			to_decompress_pos += read;
            isEmpty = 0;
            ZSTD_inBuffer input { buf_in.data(), read, 0 };
            /* Given a valid frame, zstd won't consume the last byte of the frame
             * until it has flushed all the decompressed data of the frame.
             * Therefore, instead of checking if the return code is 0, we can
             * decompress just check if input.pos < input.size.
             */
            while (input.pos < input.size)
            {
                ZSTD_outBuffer output { buf_out.data(), buf_out.size(), 0 };
                /* The return code is zero if the frame is complete, but there may
                 * be multiple frames concatenated together. Zstd will automatically
                 * reset the context when a frame is complete. Still, calling
                 * ZSTD_DCtx_reset() can be useful to reset the context to a clean
                 * state, for instance if the last decompression call returned an
                 * error.
                 */
                size_t const res = ZSTD_decompressStream(dctx, &output, &input);
                if (ZSTD_isError(res))
                {
					throw std::runtime_error(fmt::format("ZSTD_decompressStream() failed: {}", ZSTD_getErrorName(res)));
				}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
                ret.insert(ret.end(), buf_out.data(), buf_out.data() + output.pos);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
                last_result = res;
            }
        }

        if (isEmpty)
        {
			throw std::runtime_error("input is empty");
        }

        if (last_result != 0)
        {
            /* The last return value from ZSTD_decompressStream did not end on a
             * frame, but we reached the end of the data! We assume this is an
             * error, and the input was truncated.
             */
			throw std::runtime_error(fmt::format("EOF before end of stream: {}", last_result));
        }

        ZSTD_freeDCtx(dctx);
        return ret;
    }

}

TEST_CASE("zstd.ranges_vs_old_school")
{
	auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(100'000)) | std::ranges::to<std::vector>() };
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    std::span<uint8_t const> truth_bytes(reinterpret_cast<uint8_t const*>(truth.data()), truth.size() * sizeof(size_t));
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	auto old_school_compressed{ stream_compress_old_school(truth_bytes, 0, 0) };
	auto ranges_compressed{ truth | sph::views::zstd_encode() | std::ranges::to<std::vector>() };
	CHECK_EQ(ranges_compressed.size(), old_school_compressed.size());
	std::ranges::for_each(std::views::zip(old_school_compressed, ranges_compressed), [](auto&& v)
		{
			auto [t, c] {v};
			CHECK_EQ(t, c);
		});

	auto old_school_decompressed{ stream_decompress_old_school(old_school_compressed) };
	auto ranges_decompressed{ old_school_compressed | sph::views::zstd_decode() | std::ranges::to<std::vector>() };
    CHECK_EQ(old_school_decompressed.size(), ranges_decompressed.size());
	CHECK_EQ(truth.size() * sizeof(size_t), ranges_decompressed.size());
    std::ranges::for_each(std::views::zip(old_school_decompressed, ranges_decompressed), [](auto&& v)
        {
            auto [t, c] {v};
            CHECK_EQ(t, c);
        });
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}

TEST_CASE("zstd.basic")
{
    auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(1'000))
        | std::views::transform([](size_t v) -> std::array<uint8_t, sizeof(size_t)>
        {
            std::array<uint8_t, sizeof(size_t)> ret;
            std::copy_n(reinterpret_cast<uint8_t const*>(&v), sizeof(size_t), ret.data());
            return ret;
        })
        | std::views::join
        | std::ranges::to<std::vector>()
    };
	auto compressed{ truth | sph::views::zstd_encode(0) | std::ranges::to<std::vector>()};
	CHECK_LT(compressed.size(), truth.size());
	auto check{ compressed | sph::views::zstd_decode() | std::ranges::to<std::vector>() };
	CHECK_EQ(check.size(), truth.size());
	std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
		{
			auto [t, c] {v};
			CHECK_EQ(t, c);
		});
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}

TEST_CASE("zstd.from_multibyte")
{
    auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(1'000))
        | std::ranges::to<std::vector>()
    };
    auto compressed{ truth | sph::views::zstd_encode(0) | std::ranges::to<std::vector>() };
    CHECK_LT(compressed.size(), truth.size() * sizeof(size_t));
    auto check{ compressed | sph::views::zstd_decode<size_t>() | std::ranges::to<std::vector>() };
    CHECK_EQ(check.size(), truth.size());
    std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
        {
            auto [t, c] {v};
            CHECK_EQ(t, c);
        });
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}

TEST_CASE("zstd.to_multibyte")
{
    auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(1'000))
        | std::views::transform([](size_t v) -> std::array<uint8_t, sizeof(size_t)>
        {
            std::array<uint8_t, sizeof(size_t)> ret;
            std::copy_n(reinterpret_cast<uint8_t const*>(&v), sizeof(size_t), ret.data());
            return ret;
        })
        | std::views::join
        | std::ranges::to<std::vector>()
    };
    auto compressed{ truth | sph::views::zstd_encode<size_t>(0) | std::ranges::to<std::vector>() };
    CHECK_LT(compressed.size() * sizeof(size_t), truth.size());
    auto check{ compressed | sph::views::zstd_decode() | std::ranges::to<std::vector>() };
    CHECK_EQ(check.size(), truth.size());
    std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
        {
            auto [t, c] {v};
            CHECK_EQ(t, c);
        });
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}

TEST_CASE("zstd.multibyte_to_multibyte")
{
    auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(1'000))
        | std::ranges::to<std::vector>()
    };
    auto compressed{ truth | sph::views::zstd_encode<size_t>(0) | std::ranges::to<std::vector>() };
    CHECK_LT(compressed.size(), truth.size());
    auto check{ compressed | sph::views::zstd_decode<size_t>() | std::ranges::to<std::vector>() };
    CHECK_EQ(check.size(), truth.size());
    std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
        {
            auto [t, c] {v};
            CHECK_EQ(t, c);
        });
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}


TEST_CASE("zstd.levels")
{
    std::string_view pinwheel{ "|/-\\" };
    size_t pinwheel_item{ 0 };
    auto print_next_spin = [&pinwheel, &pinwheel_item](int level) -> void
        {
            fmt::print("{}  {}          \r", pinwheel[pinwheel_item], level);
            pinwheel_item = (pinwheel_item + 1) % pinwheel.size();
        };
    fmt::print("min: {}, max: {}\n", ZSTD_minCLevel(), ZSTD_maxCLevel());
    size_t to_compress_start{ 0x55C3A53CAACC5A33 };
#if _DEBUG
    size_t to_compress_size{ 180'000 };
#else
    size_t to_compress_size{ 1'000'000 };
#endif
    size_t to_compress_step{ 2'326'440'619 }; // 0x8AAAAAAB, prime number with lots of As
    std::vector<size_t> to_compress{
        std::views::iota(static_cast<size_t>(0), to_compress_size)
        | std::views::transform([to_compress_start, to_compress_step](size_t index) -> size_t
        {
            return to_compress_start + (index * to_compress_step);
        })
        | std::ranges::to<std::vector>() };
    auto len = [&to_compress, &print_next_spin](int level) -> std::tuple<size_t, double>
        {
            print_next_spin(level);
            auto const tick{ std::chrono::system_clock::now() };
            auto const ret{std::ranges::distance(to_compress | sph::views::zstd_encode(level))};
            return {ret, std::chrono::duration<double>(std::chrono::system_clock::now() - tick).count()};
        };
    std::vector<std::tuple<int, std::tuple<size_t, double>>> sizes;
    int level{ ZSTD_minCLevel() };
    while (true)
    {
        sizes.emplace_back(level, len(level));
        if (level == -1)
        {
            break;
        }

        level /= 2;
    }

    for (level = 1; level <= ZSTD_maxCLevel(); ++level)
    {
        sizes.emplace_back(level, len(level));
    }
    fmt::print(
        "{}\n",
        fmt::join(
            sizes | std::views::transform([original_length = static_cast<double>(to_compress.size() * sizeof(to_compress[0]))](auto&& t) -> std::string
            {
                auto [compression_level, t2] {t};
                auto [compressed_length, compression_time] {t2};
                return fmt::format("{}: {} ({:.3f}%), {:.3f}", compression_level, compressed_length, 100. * static_cast<double>(compressed_length) / original_length, compression_time);
            }), 
            "\n"));
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}


TEST_CASE("zstd.bigger")
{
	auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(10'000'000)) | std::ranges::to<std::vector>() };
	auto compressed{ truth | sph::views::zstd_encode<size_t>(0) | std::ranges::to<std::vector>() };
	fmt::print("Compressed size: {} ({} * {}), {:.2F}% of original\n", compressed.size()* sizeof(size_t), compressed.size(), sizeof(size_t), 100. * static_cast<double>(compressed.size()) / static_cast<double>(truth.size()));
	CHECK_LT(compressed.size() * sizeof(size_t), truth.size() * sizeof(size_t));
    std::span<uint8_t const> compressed_bytes(reinterpret_cast<uint8_t const*>(compressed.data()), compressed.size() * sizeof(size_t));
	auto check{ compressed | sph::views::zstd_decode<size_t>() | std::ranges::to<std::vector>() };
	CHECK_EQ(check.size(), truth.size());
	std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
		{
			auto [t, c] {v};
			CHECK_EQ(t, c);
		});
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}

TEST_CASE("zstd.encode_decode")
{
	auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(1'000'000)) | std::ranges::to<std::vector>() };
	auto check{ truth | sph::views::zstd_encode(0) | sph::views::zstd_decode<size_t>(0) | std::ranges::to<std::vector>() };
	CHECK_EQ(check.size(), truth.size());
	std::ranges::for_each(std::views::zip(truth, check), [](auto&& v)
		{
			auto [t, c] {v};
			CHECK_EQ(t, c);
		});
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}


TEST_CASE("zstd.wont_compile")
{
	[[maybe_unused]] std::array<wont_compile, 4> a{ {wont_compile{1}, wont_compile{2}, wont_compile{3}, wont_compile{4}} };
    // [[maybe_unused]] auto encoded{ a | sph::views::zstd_encode() };
	[[maybe_unused]] std::array<uint8_t, 8> b{ {1, 2, 3, 4, 5, 6, 7, 8} };
    // [[maybe_unused]] auto decoded{ b | sph::views::zstd_decode<wont_compile>() };
    fmt::print("{} {}/{}, {:0.5f} seconds\n", get_current_test_name(), get_current_test_assert_count() - get_current_test_assert_failed_count(), get_current_test_assert_count(), get_current_test_elapsed());
}
