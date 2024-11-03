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
	class zstd_compress_buf
	{
		size_t in_max_size_{ ZSTD_CStreamInSize() };
		size_t out_max_size_{ ZSTD_CStreamOutSize() };
		std::vector<uint8_t> buf_;
		ZSTD_inBuffer in_buf_;
		ZSTD_outBuffer out_buf_;
	public:
		zstd_compress_buf()
			: buf_(in_max_size_ + out_max_size_)
			, in_buf_{buf_.data(), in_max_size_, in_max_size_}
			, out_buf_{buf_.data() + in_max_size_, out_max_size_, out_max_size_}
		{}
		zstd_compress_buf(zstd_compress_buf const&) = default;
		zstd_compress_buf(zstd_compress_buf &&) = default;
		~zstd_compress_buf() = default;
		zstd_compress_buf &operator=(zstd_compress_buf const&) = default;
		zstd_compress_buf &operator=(zstd_compress_buf&&) = default;
		auto in() -> ZSTD_inBuffer& { return in_buf_; }
		[[nodiscard]] auto in_pos() const -> size_t { return in_buf_.pos; }
		auto in_max_size() const -> size_t { return in_max_size_; }
		auto out() -> ZSTD_outBuffer& { return out_buf_; }
			[[nodiscard]] auto out_pos() const -> size_t { return out_buf_.pos; }
		auto out_max_size() const -> size_t { return out_max_size_; }
	};

	class zstd_compressor
	{
		struct zstd_data
		{
			ZSTD_CCtx* ctx{};
			zstd_compress_buf buf{};
			explicit zstd_data(int level) : ctx{ init_ctx(level) } {}
			zstd_data(zstd_data const&) = delete;
			zstd_data(zstd_data&&) = default;
			~zstd_data() { ZSTD_freeCCtx(ctx); }
			auto operator=(zstd_data const&)->zstd_data & = delete;
			auto operator=(zstd_data&&)->zstd_data & = default;
		private:
			static auto init_ctx(int level) -> ZSTD_CCtx*
			{
				auto ret{ ZSTD_createCCtx() };
				if (ret == nullptr)
				{
					throw std::runtime_error("Failed to create zstd compress context.");
				}

				if (level > ZSTD_maxCLevel())
				{
					level = ZSTD_maxCLevel();
				}

				ZSTD_CCtx_setParameter(ret, ZSTD_c_compressionLevel, level);
				ZSTD_CCtx_setParameter(ret, ZSTD_c_checksumFlag, 1);

				return ret;
			}
		};

		std::shared_ptr<zstd_data> data_;
		bool can_compress_{ true };
	public:
		zstd_compressor(int level = 0) : data_{ std::make_shared<zstd_data>(level) } {}
		zstd_compressor(zstd_compressor const& o)
			: data_{ o.data_ }
			, can_compress_{ false } // only one copy can compress
		{}
		zstd_compressor(zstd_compressor &&) = default;
		~zstd_compressor() = default;
		auto operator=(zstd_compressor const& o) noexcept -> zstd_compressor&
		{
			if (&o != this)
			{
				data_ = o.data_;
				can_compress_ = false; // only one copy can compress
			}

			return *this;
		}

		auto operator=(zstd_compressor&&) -> zstd_compressor& = default;

		[[nodiscard]] auto in() -> ZSTD_inBuffer& { return data_->buf.in(); }
		[[nodiscard]] auto in_src() const -> uint8_t* { return const_cast<uint8_t*>(static_cast<uint8_t const*>(data_->buf.in().src)); }
		[[nodiscard]] auto in_pos() const -> size_t { return data_->buf.in().pos; }
		[[nodiscard]] auto in_size() const -> size_t { return data_->buf.in().size; }
		[[nodiscard]] auto in_max_size() const -> size_t { return data_->buf.in_max_size(); }
		[[nodiscard]] auto out() -> ZSTD_outBuffer& { return data_->buf.out(); }
		[[nodiscard]] auto out_pos() const -> size_t { return data_->buf.out().pos; }
		[[nodiscard]] auto out_size() const -> size_t { return data_->buf.out().size; }
		[[nodiscard]] auto out_max_size() const -> size_t { return data_->buf.out_max_size(); }

		/**
		 * Compress the data in the in() buffer (along with any remaining data
		 * in the compression pipeline) into the out() buffer.
		 *
		 * Expects either in() pos < size or remaining content in the
		 * compression pipeline (or both).
		 *
		 * Sets out() pos=0 and size to the number of compressed bytes.
		 *
		 * @param mode ZSTD_e_continue if there will be more input data;
		 * ZSTD_e_end if there won't.
		 * @return True if there remains data in the compression pipeline to
		 * put into out(); false otherwise.
		 */
		[[nodiscard]] auto operator()(ZSTD_EndDirective mode) const -> bool
		{
			if (!can_compress_)
			{
				throw std::runtime_error("Only one copy of the zstd compressor can compress. You probably made a copy of the iterator and tried to use it. Moving the iterator is fine.");
			}

			auto &o{ data_->buf.out() };
			auto &i{ data_->buf.in() };
			o.pos = 0;
			o.size = data_->buf.out_max_size();
			size_t const ret{ ZSTD_compressStream2(data_->ctx, &o, &i, mode) };
			if (ZSTD_isError(ret))
			{
				ZSTD_ErrorCode const err{ ZSTD_getErrorCode(ret) };
				ZSTD_CCtx_reset(data_->ctx, ZSTD_reset_session_only);
				throw std::runtime_error(std::format("zstd failed compression: {}.", ZSTD_getErrorString(err)));
			}

			o.size = o.pos;
			o.pos = 0;
			return ret == 0;
		}
	};
}