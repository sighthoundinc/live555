# Project

* This project is mirror of LIVE555 project available at <http://www.live555.com/liveMedia/>
* `master` branch contains unmodified source code committed at discrete points of time. The author provides the source as an archive, without version control history. This provides us with some history, albeit only at upgrade points.
* `sighthound` branch contains modified source code used in Sighthound projects. The modifications pursue the following objectives:
    * Ability to build the project with CMake
    * Ability to generate a usable shared library, which is LGPL-compliant.
        * The original source code doesn't contain export qualifiers on the API classes. This makes it impossible to use as a shared library on Windows, when using MSVC compilers. Using it as a static library is not something that is always possible under LGPL.
        * To overcome this problem, we use a wrapper API classes defined and implemented in `svlive555` folder. This is the API we use and link with.
    * Modifying some default buffer sizes
    * Adding an API method to access dropped frames count

# Building

The project can be built with CMake 3.19 or later.
* Linux/Mac
    * `mkdir -p .build`
    * `cd .build`
    * cmake -DOPENSSL_INCLUDE_DIR="/path/to/openssl/include" -DOPENSSL_LIB_DIR="/path/to/openssl/lib" -DOPENSSL_LIBS="ssl;crypto" ..
    * make
* Windows (using Ninja and clang-cl frontend)
    * `mkdir -p .build`
    * `cd .build`
    * cmake -DOPENSSL_INCLUDE_DIR=\path\to\openssl\include -DOPENSSL_LIB_DIR=\path\to\openssl\lib -DOPENSSL_LIBS="libssl;libcrypto" -G "Ninja" ..
    * ninja