# zstd_views

zstd encoding via a range adapter.

This will encode any range of standard layout types to a compressed view of 
that data and can take that compressed view and decode it back into a 
decompressed view the same as the original range.

# Using

```c++
#include <ranges>
#include <sph/ranges/views/zstd_encode.h>_
#include <sph/ranges/views/zstd_decode.h>_
#include <vector>
int main()
{
    auto uncompressed { 
        std::views::iota(static_cast<size_t>(1), static_cast<size_t>(1001)) | std::ranges::to<std::vector>()
    };
    auto compressed { 
        uncompressed | sph::views::zstd_encode() | std::ranges::to<std::vector>()
    };
    auto uncompressed_again { 
        compressed | sph::views::zstd_decode<size_t>() | std::ranges::to<std::vector>()
    };
}
```

There are more tricks up this library's sleeve. The zstd_encode view can encode
to any standard layout type, not just the default uint8_t. To manage this, if
the compressed length doesn't fit exactly, a zstd skippable frame gets appended
to use up any leftover bytes.

```c++
std::vector<size_t> compressed { 
    uncompressed | sph::views::zstd_encode<size_t>() | std::ranges::to<std::vector>()
};
```

The input and output types must be standard layout types. The following will get some 
form "invalid operands for |" compilation error.

```c++
class wont_compile
{
    size_t value_;
public:
    wont_compile(size_t v) : value_{ v }{}
    virtual ~wont_compile(){} // virtual function makes it non-standard layout
};
// : : :
std::vector<wont_compile> compressed { 
    uncompressed | sph::views::zstd_encode<wont_compile>() | std::ranges::to<std::vector>()
};
```


### Setting Compression Levels

The zstd_encode view can take a compression level parameter. If you don't 
supply one, defaults to using zero - the default for zstd. Zstd typically
converts this to 3 but it the actual value comes from the underlying zstd 
library. If you supply one, whatever you supply will be clamped by 
`ZSTD_minCLevel()` and  `ZSTD_maxCLevel()`. These values are typically 
-131072 and 22, respectively. But, again, the actual values come from the
underlying zstd library. 

Where the fastest compression occurs varies widely depending on the
compressibility of the content. If the content can be easily compressed,
you may get the fastest compression between -100 and -25. If the content
doesn't compress as well, the negative compression levels will probably
result in a small net expansion.

Likewise, the positive compression level performance can vary widely.
For easily compressed data, you may get the fastest compression at
level 5 and not get appreciable better compression until level 22
which, in this case, can take twice as long to compress as level 21.
On less compressible data, you may not see much of a difference in
compression until level 16 even though level 15 may take more than
four times longer than level 1 or 4.

Know your data if you want the best results for your situation.

### Setting Decompression Maximum Window Size

The zstd_decode view can take a maximum window size parameter. If you don't
supply one, it defaults to 0 - the default for zstd. Zstd typically
converts this to 27 but the actual value comes from the underlying zstd
library. This parameter can cause the decompressor to reject a compressed 
stream that requires too much memory from the host. The memory used comes from
2 raised to the maximum window size parameter. So, 27 (the typical default) 
means 2^27 bytes or 128MB. If you supply a value, whatever you supply will be
clamped by `ZSTD_dParam_getBounds(ZSTD_d_windowLogMax)` which is typically 11
and 30 (2KB and 1GB) for 32-bit and 11 and 31 (2KB and 2GB) for 64-bit. Again,
the actual values come from the underlying zstd library.

# Building

The zstd_views library has a dependency on the zstd vcpkg port and C++23. The
unit tests require doctest and fmt. To build, you need a vcpkg environment
setup. This build assumes you have done something like
`git clone https://github.com/microsoft/vcpkg.git .vcpkg` in your home 
directory. Then you run the bootstrap-vcpkg.{bat,sh} program in that 
directory to make vcpkg operational.

```sh
cmake -B build --preset={preset}
cmake --install build 
```

Where {preset} is one of

 * clang-debug
 * clang-debug-develop
 * clang-release
 * clang-release-develop
 * clang-test-debug
 * clang-test-release
 * clang-test-release
 * gcc-debug
 * gcc-debug-develop
 * gcc-release
 * gcc-release-develop
 * gcc-test-debug
 * gcc-test-release
 * clang-win-debug
 * clang-win-debug-develop
 * clang-win-release
 * clang-win-release-develop
 * clang-win-test-debug
 * clang-win-test-release
 * msvc-debug
 * msvc-debug-develop
 * msvc-release
 * msvc-release-develop
 * msvc-test-debug-develop
 * msvc-test-release-develop
The develop versions built the tests and include sanitizers. The test version also set up CMake testing. All these options are more aspirational then operational.
