#pragma once
#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>
#include <vector>
#include <zstd.h>
#include <zstd_errors.h>
namespace sph::ranges::views::detail
{
	/**
	 * The zstd decompressor works on a pair of buffers, input and output. This
	 * class manages those buffers.
	 */
	class zstd_decompress_buf
	{
		size_t in_max_size_{ ZSTD_DStreamInSize() };
		size_t out_max_size_{ ZSTD_DStreamOutSize() };
		std::vector<uint8_t> buf_;
		ZSTD_inBuffer in_buf_;
		ZSTD_outBuffer out_buf_;
	public:
		zstd_decompress_buf()
			: buf_(in_max_size_ + out_max_size_)
			, in_buf_{ buf_.data(), in_max_size_, in_max_size_ }
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
			, out_buf_{ buf_.data() + in_max_size_, out_max_size_, out_max_size_ }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	    {}
		zstd_decompress_buf(zstd_decompress_buf const&) = default;
		zstd_decompress_buf(zstd_decompress_buf&&) = default;
		~zstd_decompress_buf() = default;
		zstd_decompress_buf& operator=(zstd_decompress_buf const&) = default;
		zstd_decompress_buf& operator=(zstd_decompress_buf&&) = default;
		auto in() -> ZSTD_inBuffer& { return in_buf_; }
		[[nodiscard]] auto in_pos() const -> size_t { return in_buf_.pos; }
		[[nodiscard]] auto in_max_size() const -> size_t { return in_max_size_; }
		auto out() -> ZSTD_outBuffer& { return out_buf_; }
		[[nodiscard]] auto out_pos() const -> size_t { return out_buf_.pos; }
		[[nodiscard]] auto out_max_size() const -> size_t { return out_max_size_; }
	};

	/**
	 * A zstd decompression functor.
	 *
	 * Since zstd is written so old-school, its state cannot be handled between
	 * copies easily. So zstd_decompressor keeps the state in a std::shared_ptr
	 * and keeps track on whether it has been moved or copied. If moved,
	 * decompression can continue, if copied it is a std::logic_error to attempt
	 * to decompress using the copy.
	 */
	class zstd_decompressor
	{
		/**
		 * Holds the state for the zstd_decompressor.
		 */
		struct zstd_data
		{
			ZSTD_DCtx* ctx{};
			zstd_decompress_buf buf{};
			zstd_data() : ctx{ init_ctx() } {}
			zstd_data(zstd_data const&) = delete;
			zstd_data(zstd_data&&) = default;
			~zstd_data() { ZSTD_freeDCtx(ctx); }
			auto operator=(zstd_data const&)->zstd_data & = delete;
			auto operator=(zstd_data&&)->zstd_data & = default;
		private:
			static auto init_ctx() -> ZSTD_DCtx*
			{
				auto const ret{ ZSTD_createDCtx() };
				if (ret == nullptr)
				{
					throw std::runtime_error("Failed to create zstd decompress context.");
				}

				return ret;
			}
		};
		std::shared_ptr<zstd_data> data_;
		bool can_decompress_{ true };

	public:
		zstd_decompressor() : data_{ std::make_shared<zstd_data>() } {}
		zstd_decompressor(zstd_decompressor const&o)
			: data_{o.data_}
			, can_decompress_{false} // only  one copy can decompress at a time
		{
		}
		zstd_decompressor(zstd_decompressor&&) = default;
		~zstd_decompressor() = default;
		auto operator=(zstd_decompressor const& o) noexcept -> zstd_decompressor&
		{
			if (&o != this)
			{
				data_ = o.data_;
				can_decompress_ = false; // only one copy can decompress at a time
			}

			return *this;
		}
		auto operator=(zstd_decompressor&&) -> zstd_decompressor& = default;

		[[nodiscard]] auto in() const -> ZSTD_inBuffer& { return data_->buf.in(); }
		[[nodiscard]] auto in_src() const -> uint8_t* { return const_cast<uint8_t*>(static_cast<uint8_t const*>(data_->buf.in().src)); }
		[[nodiscard]] auto in_max_size() const -> size_t { return data_->buf.in_max_size(); }
		[[nodiscard]] auto out() const -> ZSTD_outBuffer& { return data_->buf.out(); }
		[[nodiscard]] auto out_max_size() const -> size_t { return data_->buf.out_max_size(); }

		/**
		 * Runs a decompression on the input into the output. The output size will be set.
		 *
		 * Expects in() to have pos < size.
		 *
		 * Will set out() pos=0 and size to the number of bytes decompressed.
		 *
		 * @return True if fully decoded and flushed; false if some decoding and flushing still remains.
		 */
		[[nodiscard]] auto operator()() const -> bool
		{
			if (!can_decompress_)
			{
				throw std::logic_error("Only one copy of the zstd decompressor can decompress. You probably made a copy of the iterator and tried to use it. Moving the iterator is fine.");
			}
			auto &o{ data_->buf.out() };
			auto &i{ data_->buf.in() };
			o.pos = 0;
			o.size = data_->buf.out_max_size();
			size_t const ret{ ZSTD_decompressStream(data_->ctx, &o, &i) };
			if (ZSTD_isError(ret))
			{
				ZSTD_ErrorCode const err{ ZSTD_getErrorCode(ret) };
				ZSTD_DCtx_reset(data_->ctx, ZSTD_reset_session_only);
				if (err == ZSTD_error_memory_allocation)
				{
					// I think memory allocation issues is the only error that can happen that should be a runtime_error here.
					throw std::runtime_error(std::format("zstd failed decompression: {}.", ZSTD_getErrorString(err)));
				}

				throw std::invalid_argument(std::format("zstd failed decompression: {}.", ZSTD_getErrorString(err)));
			}

			o.size = o.pos;
			o.pos = 0;
			return ret == 0;
		}
	};
}
