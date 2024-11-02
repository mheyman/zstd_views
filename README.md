# zstd_views

zstd encoding via a range adapter.

Something is still wrong at the actual view layer but I *think* I have the 
compression/decompression logic right underneath.

# Using

*Notional* (it doesn't work yet...)

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

# Building
The zstd_views_ library has a dependency on the zstd vcpkg port and C++23. The
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

clang-debug
clang-debug-develop
clang-release
clang-release-develop
clang-test-debug
clang-test-release
clang-test-release
gcc-debug
gcc-debug-develop
gcc-release
gcc-release-develop
gcc-test-debug
gcc-test-release
clang-win-debug
clang-win-debug-develop
clang-win-release
clang-win-release-develop
clang-win-test-debug
clang-win-test-release
msvc-debug
msvc-debug-develop
msvc-release
msvc-release-develop
msvc-test-debug-develop
msvc-test-release-develop
The develop versions built the tests and include sanitizers. The test version also set up CMake testing. All these options are more aspirational then operational.
