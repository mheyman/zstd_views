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
			std::copy_n(to_compress.data() + in_pos, read, buf_in.data());
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

				ret.insert(ret.end(), buf_out.data(), buf_out.data() + output.pos);
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
}

TEST_CASE("zstd.ranges_vs_old_school")
{
	auto truth{ std::views::iota(static_cast<size_t>(0), static_cast<size_t>(100'000)) | std::ranges::to<std::vector>() };
	std::span<uint8_t const> truth_bytes(reinterpret_cast<uint8_t const*>(truth.data()), truth.size() * sizeof(size_t));
	auto old_school{ stream_compress_old_school(truth_bytes, 0, 0) };
	auto check{ truth | sph::views::zstd_encode() | std::ranges::to<std::vector>() };
	CHECK_EQ(check.size(), old_school.size());
	std::ranges::for_each(std::views::zip(old_school, check), [](auto&& v)
		{
			auto [t, c] {v};
			CHECK_EQ(t, c);
		});
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
