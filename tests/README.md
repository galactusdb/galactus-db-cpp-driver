# galactus-db-cpp-driver tests

[Galactus DB website](https://galactusdb.com) · [Driver Quickstart](../README.md)

## Quickstart

Run these commands from the repository root:

```sh
cmake -S . -B build -DGALACTUS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Add `-DGALACTUS_WITH_TLS=ON` to build the same tests with `bolt+s://` support
(SChannel on Windows; OpenSSL on Linux and macOS, found with
`find_package(OpenSSL)`). Without it, the tests check that `bolt+s://` is
rejected with an error naming `GALACTUS_WITH_TLS`.

On Windows with MinGW, pass `-G "MinGW Makefiles"`. A direct compile also works:

```sh
g++ -std=c++17 -Wall -Wextra -pedantic -I include tests/test.cpp -lws2_32 -o cpp-test
g++ -std=c++17 -Wall -Wextra -pedantic -DGALACTUS_WITH_TLS -I include tests/test.cpp \
    -lws2_32 -lsecur32 -lcrypt32 -o cpp-test-tls
```

Run the program from the repository root so it finds `tests/fixtures`.

The fixtures are included in `tests/fixtures`; no sibling repository is needed.
The tests use standard runtime facilities, with no test framework packages.

| Fixture | Checked |
|---|---|
| `values.tsv` | Round-trips in Bolt 4.4 mode: integer boundaries, UTF-8, temporal, graph and point values, unknown structures |
| `bolt5.tsv` | Round-trips in Bolt 5 mode: element ids, UTC datetimes, both named-zone bases |
| `datetime_pairs.tsv` | Each row decodes to equal values from its `F` and `I` columns and encodes to `F` on 4.4 and `I` on Bolt 5 |
| `malformed.tsv` | Every input is rejected |
| `spatial.tsv` | 57 geometry/geography cases against a live server |

The codec tests also cover error classification, URI and option validation
and the `bolt_agent` map.

### Live database tests

Without `GDB_TEST_URI`, live tests are skipped. To include them, start a **fresh,
disposable** Galactus database and set these environment variables before running
the same test command:

| Variable | Meaning |
|---|---|
| `GDB_TEST_URI` | `bolt://host:port`, or `bolt+s://host:port` for a TLS build |
| `GDB_TEST_PASSWORD` | Password for user `gdb` |
| `GDB_TEST_EXPECT_BOLT` | Optional: the protocol the server should agree, `5.4` or `4.4` |
| `GDB_TEST_CA_FILE` | Optional: PEM file passed as `trusted_certificates` for `bolt+s://` |

```sh
export GDB_TEST_URI='bolt://127.0.0.1:7687'
export GDB_TEST_PASSWORD='your-disposable-database-password'
```

```powershell
$env:GDB_TEST_URI = 'bolt://127.0.0.1:7687'
$env:GDB_TEST_PASSWORD = 'your-disposable-database-password'
```

The live test uses the server's default database and creates nodes with
`DriverCpp*` labels and a unique constraint on `DriverCppUnique`. It checks
the agreed protocol, server agent and connection id, native values, spatial
interchange, commit and rollback, element ids, offset and named-zone
datetimes, decimals as strings, recovery after a failed query in and outside a
transaction, rejected parameters, the constraint violation code, and rejected
authentication. With a `bolt+s://` URI it also checks that connecting without
the trusted certificate file fails certificate verification and that the
option is refused on `bolt://`. Do not point it at valuable data, and reset
the database between runs.

The shared runner in the drivers workspace (`tests/run_live.py`) starts fresh
servers for three passes and runs the prebuilt `cpp-test.exe` (Bolt 5.4 and
4.4 passes) and `cpp-test-tls.exe` (TLS pass with a throwaway test CA).
See [implementation scope](../docs/OVERVIEW.md#scope).

Database information: [Galactus DB website](https://galactusdb.com).

CMake links the Windows socket library automatically. A C++17 compiler and
CMake 3.16+ are required.
