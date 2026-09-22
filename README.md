# Galactus C++ driver

[Galactus DB website](https://galactusdb.com) · [Source](https://github.com/galactusdb/galactus-db-cpp-driver) · [Type mapping](docs/BLUEPRINT.md) · [Spatial types](docs/SPATIAL.md)

Native Bolt 4.4 driver for Galactus DB. This experimental 0.1 implementation
has zero third-party runtime package dependencies. Source is available here;
no npm, NuGet, PyPI, Maven Central, or crates.io release is implied.

## How To

### 1. Get the driver

```sh
git clone --branch main https://github.com/galactusdb/galactus-db-cpp-driver.git
cd galactus-db-cpp-driver
```

C++17; header-only, standard library and OS sockets only. Add this checkout to your application CMake project:

```cmake
add_subdirectory(path/to/galactus-db-cpp-driver)
target_link_libraries(your_app PRIVATE Galactus::Driver)
```

### 2. Configure your connection

Start or obtain a Galactus DB instance; visit [galactusdb.com](https://galactusdb.com)
for database information. Use its Bolt address (locally, `bolt://127.0.0.1:7687`),
username, and configured password. The example reads `GDB_PASSWORD` from your
environment; it is an application variable, not a command to change the server password.

```sh
# Bash / zsh
export GDB_PASSWORD='your-database-password'
```

```powershell
# PowerShell
$env:GDB_PASSWORD = 'your-database-password'
```

### 3. Execute a parameterised query

```cpp
#include <galactus/driver.hpp>
#include <cstdlib>
#include <iostream>

int main() {
    const char* password = std::getenv("GDB_PASSWORD");
    if (!password) return 1;
    galactus::Driver driver("bolt://127.0.0.1:7687", "gdb", password);
    auto result = driver.execute_query("RETURN $name AS name", {{"name", "Ada"}});
    std::cout << result.records.at(0).at("name").as<std::string>() << '\n';
} // RAII closes the connection and rolls back unfinished work.
```

Optional constructor arguments are database (`neo4j`) and timeout milliseconds
(30000). The driver is noncopyable and must not be used concurrently.
`begin(read_only)`, `commit()`, `rollback()` and `close()` manage transactions.
TCP only: use a TLS tunnel if required; encrypted URI schemes are rejected.

`Value` uses std::variant with native scalar/container representations.
Named graph/temporal values are obtained with `value.as<Node>()`,
`value.as<DateTime>()`, etc.; `value.as<Spatial>()` exposes lossless shape data.
Construct parameters from named values directly. Native system_clock::time_point
input is supported; `to_system_time(DateTime)` performs checked output conversion.
The clock conversion assumes the platform's system_clock uses the Unix epoch,
as on the targeted Windows/POSIX implementations. Signed integer overflow is rejected.

See [mapping](docs/BLUEPRINT.md), [spatial examples](docs/SPATIAL.md), and [scope](docs/OVERVIEW.md#scope-of-01).

### 4. Run the tests

From this repository's root:

```sh
cmake -S . -B build -DGALACTUS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

See [test instructions](tests/README.md) for prerequisites and opt-in live tests.
Connection failures discard the connection; writes are never automatically retried.
Results are eager and each driver owns one connection; see the documented scope.

Learn more at [Galactus DB website](https://galactusdb.com).
