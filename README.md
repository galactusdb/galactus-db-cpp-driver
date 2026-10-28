# Galactus DB C++ Driver

[Galactus DB](https://galactusdb.com) · [Driver catalogue](https://galactusdb.com/product/drivers) · [Type mapping](docs/BLUEPRINT.md) · [Spatial guide](docs/SPATIAL.md) · [Changelog](CHANGELOG.md) · [Issues](https://github.com/galactusdb/galactus-db-cpp-driver/issues)

Connect C++ applications to Galactus DB with parameterised Cypher, native
values and explicit transactions. The driver is a header-only C++17 library
that speaks Bolt 5.4 and falls back to Bolt 4.4 for older servers. The default
build has **zero third-party dependencies**: it needs only the platform's
socket library. Turn on one CMake option and it connects over verified TLS
using the operating system's own TLS stack.

**Status: 0.2 / early access.** Source is available on `main`. Each driver owns
one connection and returns complete results. Install from source as shown below
and read the [scope notes](docs/OVERVIEW.md#scope) before you build on it.

## Contents

- [Quickstart](#quickstart)
- [Connection configuration](#connection-configuration)
- [Encrypted connections](#encrypted-connections)
- [Queries and results](#queries-and-results)
- [Transactions](#transactions)
- [Native type mapping](#native-type-mapping)
- [Spatial values](#spatial-values)
- [Errors and connection lifecycle](#errors-and-connection-lifecycle)
- [Testing](#testing)
- [What 0.2 includes](#what-02-includes)

## Quickstart

### 1. Get the source

Requirements: **C++17 and CMake 3.16+**, Git, and a reachable Galactus DB instance.

```sh
git clone --branch main https://github.com/galactusdb/galactus-db-cpp-driver.git
cd galactus-db-cpp-driver
```

Add this to your application's `CMakeLists.txt`.

```cmake
add_subdirectory(path/to/galactus-db-cpp-driver)
target_link_libraries(your_app PRIVATE Galactus::Driver)
```

Git-based installs follow `main`; pin a reviewed commit in your application for
reproducible builds. No npm, NuGet, PyPI, Maven Central or crates.io availability
is implied by the package names.

### 2. Configure your database credentials

Start Galactus DB on your infrastructure using the setup available from
[galactusdb.com](https://galactusdb.com). The examples use
`bolt://127.0.0.1:7687` and username `gdb`; replace them with your server settings.
Set your configured password in the application environment:

```sh
# Bash / zsh
export GDB_PASSWORD='your-database-password'
```

```powershell
# PowerShell
$env:GDB_PASSWORD = 'your-database-password'
```

`GDB_PASSWORD` is read by these examples; the driver does not load environment
variables itself or change the server password. Keep credentials out of source
control and connection URIs.

### 3. Run your first query

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
    std::cout << "Bolt " << driver.protocol_version() << '\n'; // "5.4"
} // RAII closes the connection and rolls back unfinished work.
```

The following examples reuse an open `driver` within the same resource scope.
Adapt error handling to your application.

## Connection configuration

There are two constructors. The 0.1 form still works:

```cpp
galactus::Driver driver(uri, username, password, database = "", timeout_ms = 30000);
```

The options form adds the TLS trust setting:

```cpp
galactus::DriverOptions options;
options.database = "app";                   // empty: the server's default database
options.timeout_ms = 30000;
options.trusted_certificates = "ca.pem";    // bolt+s:// only
galactus::Driver driver("bolt+s://db.example.com:7687", "gdb", password, options);
```

The driver is noncopyable; RAII closes the connection and `close()` is also available.

| Setting | Behaviour |
|---|---|
| URI | `bolt://host:port` (plaintext) or `bolt+s://host:port` (verified TLS); omitted port is 7687 |
| Protocol | Bolt 5.4 down to 5.0, or 4.4 when the server offers only that |
| Authentication | Username and password as arguments; no credentials in the URI |
| Database | Empty or omitted lets the server choose its configured default |
| Named database | Supply an existing database name explicitly, for example `app` |
| Timeout | Milliseconds; bounds connecting and each socket wait |
| Trusted certificates | Path to PEM certificates that replace the platform trust store |
| Result handling | Records are buffered in memory until the query completes |

URI query options, `bolt+ssc://` and the routing schemes (`neo4j://`,
`neo4j+s://`, `neo4j+ssc://`) are rejected with `galactus::ConfigurationError`
before any connection is made. The driver never falls back from TLS to
plaintext.

After connecting, the driver reports what the server agreed:

```cpp
driver.protocol_version(); // "5.4" or "4.4"
driver.server_agent();     // for example "GDB/2.1.5"
driver.connection_id();
driver.last_bookmark();    // latest bookmark from an autocommit query or COMMIT
```

Timeouts bound socket waits, not DNS lookup or a query's total duration. Do
not use one driver from several threads at once; give each worker its own.

## Encrypted connections

Galactus DB serves TLS itself: start the server with `GDB_TLS_CERT` and
`GDB_TLS_KEY` and it accepts `bolt+s://` on its usual port. Build the driver
with TLS to connect to it:

```cmake
set(GALACTUS_WITH_TLS ON CACHE BOOL "" FORCE)
add_subdirectory(path/to/galactus-db-cpp-driver)
```

or `-DGALACTUS_WITH_TLS=ON` on the command line. The option defines the macro
`GALACTUS_WITH_TLS` and links the operating system's TLS stack:

| Platform | TLS provider | Libraries linked |
|---|---|---|
| Windows | SChannel | `secur32`, `crypt32` |
| Linux, macOS | OpenSSL 1.1.1 or 3.x | `OpenSSL::SSL`, `OpenSSL::Crypto` (found with `find_package(OpenSSL)`) |

Every `bolt+s://` connection is verified. The driver sends the URI host name for
SNI and checks the server certificate against it. Trust comes from the platform
store, so a certificate from a public CA works with no extra setting. For a
private CA or a self-signed development server, set `trusted_certificates` to a
PEM file; the driver then trusts only the certificates in that file. There is
no setting that skips verification.

A build without the option rejects `bolt+s://` with a `ConfigurationError`
that names `GALACTUS_WITH_TLS`.

## Queries and results

Bind data as parameters, never interpolate user input into Cypher:

```cpp
auto result = driver.execute_query(
    "CREATE (p:Person {name:$name, age:$age}) RETURN p.name AS name, p.age AS age",
    {{"name", "Ada"}, {"age", 37}});
std::cout << result.records.at(0).at("name").as<std::string>() << '\n';
```

Results expose column keys, records indexed by column name, and server summary
metadata. Read a column with `result.records.at(0).at("name").as<std::string>()`.
Use unique column aliases because records are maps. Summary fields can include
the database, query classification, timings, bookmark and update statistics;
only fields supplied by the server are present. A query without rows still
returns its summary.

Lists and maps can be nested. Values are recursively encoded and hydrated,
including graph properties and spatial envelopes. Use Cypher `LIMIT` or
application-level pagination for large result sets, because results are
collected in full before `execute_query` returns.

## Transactions

Queries outside an explicit transaction autocommit. Group related writes with
begin/commit; explicitly roll back when abandoning work:

```cpp
driver.begin();
driver.execute_query("CREATE (:Person {name:$name})", {{"name", "Ada"}});
driver.execute_query("CREATE (:Person {name:$name})", {{"name", "Grace"}});
driver.commit();
// To deliberately abandon an open transaction: driver.rollback();
```

The transaction belongs to the connection. Keep both statements on the same
driver and finish the transaction before reusing it for unrelated work. Closing
a connection rolls back an unfinished transaction; it never commits implicitly.
`begin(true)` requests a read transaction.

If a statement inside a transaction fails on the server, the server rolls the
whole transaction back. The driver marks it closed (`in_transaction()` returns
false) and stays connected, so the next query runs as normal. A
`DatabaseError` from `commit()` means nothing was committed. A lost connection
during `commit()` means the outcome is unknown; check the data before retrying
a write.

## Native type mapping

`Value` wraps `std::variant` with native integers, double, bool, UTF-8 strings,
`Bytes`, `List` and `Map`. Read named values with `as<Node>()`,
`as<DateTime>()` or `as<Spatial>()`. `system_clock::time_point` inputs and
checked `to_system_time(DateTime)` conversion are supported on the targeted
Unix-epoch clocks.

- Database integers are signed 64-bit; out-of-range integer inputs fail.
- Bytes stay binary; values do not pass through JSON.
- Temporal wrappers keep nanoseconds, offsets and named zones exactly.
  `DateTime` holds local seconds plus the offset, whichever Bolt version
  carried it. `ZonedDateTime` records whether its seconds are local or UTC
  (`ZonedDateTime::local(...)`, `ZonedDateTime::utc(...)`), because moving
  between the two needs a time zone database.
- Calendar duration keeps months, days, seconds and nanoseconds separately.
- `Node`, `Relationship` and `UnboundRelationship` carry `element_id` (and
  `start_element_id` / `end_element_id` on relationships) as strings. Galactus
  element ids are the decimal text of the integer id, on Bolt 5 and 4.4 alike.
- `DECIMAL` values arrive as strings holding their exact digits, such as
  `"12.30"`. Send a string and convert it in Cypher with `decimal($d)`.
- Graph nodes, relationships and paths are result types. Bind their IDs or
  properties as parameters; the graph entities themselves are rejected.
- Unknown wire structures remain explicit structures and cannot be used as
  generic application-object serialization.

No ORM or automatic arbitrary-object mapping is included. Convert application
models and UUIDs explicitly to a supported value. See the
[complete type mapping](docs/BLUEPRINT.md) for precision and range details.

## Spatial values

The driver supports `Point2D`, `Point3D`, and a native `Spatial` value covering
every shape family Galactus DB supports:

| Shape family | Geometry | Geography |
|---|---|---|
| Point / LineString / Polygon, including holes | Yes | Yes |
| MultiPoint / MultiLineString / MultiPolygon | Yes | Yes |
| GeometryCollection, including nested collections | Yes | Yes |
| XY, XYZ, and typed empty shapes | Yes | Yes |

`Spatial` preserves WKB bytes together with domain, SRID, coordinate layout and
model. Geometry uses `planar`; geography uses `greatCircle`. Neither domain nor
SRID is inferred by converting through GeoJSON.

```cpp
auto result = driver.execute_query(
    "RETURN spatial.fromWKT($wkt, {domain:$domain}) AS shape",
    {{"wkt", "POLYGON((0 0,4 0,4 4,0 4,0 0))"}, {"domain", "geometry"}});
auto shape = result.records.at(0).at("shape");
driver.execute_query(
    "CREATE (:Region {name:$name, shape:spatial.fromMap($shape)})",
    {{"name", "Square"}, {"shape", shape}});
std::cout << shape.as<galactus::Spatial>().domain << '\n';
```

Binding `Spatial` sends a lossless envelope. Use `spatial.fromMap($shape)` to
promote it into a database spatial value. `RETURN $shape` alone echoes the map;
it does not change the server's value type. To import external bytes or text,
use `spatial.fromWKB`, `spatial.fromWKT`, or `spatial.fromGeoJSON` with parameters.
The server performs geometry and topology validation. See the
[spatial guide](docs/SPATIAL.md).

## Errors and connection lifecycle

A server error is a `galactus::DatabaseError` with the server's `code`,
`what()` message and `metadata`, plus the parts of the code and helpers to act
on them:

```cpp
try {
    driver.execute_query("CREATE (:User {email:$email})", {{"email", email}});
} catch (const galactus::DatabaseError& e) {
    if (e.code == galactus::codes::constraint_validation_failed) {
        // the email is already taken
    } else if (e.is_retryable()) {
        // a TransientError: safe to run the transaction again
    }
}
driver.execute_query("RETURN 1"); // the same driver keeps working
```

`classification`, `category` and `title` hold the parts of a
`Vendor.Classification.Category.Title` code. `is_client_error()`,
`is_transient()` and `is_database_error()` test the classification, and
`is_retryable()` is true for transient errors. The driver never retries on its
own. `galactus::codes` publishes the documented codes, from
`constraint_validation_failed` to `database_not_found`; codes the server adds
later still arrive as plain strings.

After a database error the driver resets the session and stays connected. The
other error types tell you what happened to the connection:

| Error | Meaning | Connection |
|---|---|---|
| `DatabaseError` | The server refused the query or transaction | Kept and ready |
| `DatabaseError` while connecting | Authentication failed | Closed |
| `std::invalid_argument` from a query | A parameter could not be sent | Kept; nothing was sent |
| `ConfigurationError` | Bad URI or option (derives from `std::invalid_argument`) | Never opened |
| `TlsError` | TLS handshake or certificate check failed | Closed |
| `ConnectionError` | Socket failure, timeout or closed connection | Closed |
| `ProtocolError` | The peer broke the Bolt protocol | Closed |

`TlsError` derives from `ConnectionError`; both derive from
`std::runtime_error`. Open a new driver after a closed connection.

| Symptom | Check |
|---|---|
| Connection refused | Host, port, listener and network reachability |
| Authentication rejected | Database username and configured password |
| Database not found | Explicit database name; omit it to use the server default |
| Certificate rejected | The certificate's host name, and `trusted_certificates` for a private CA |
| `bolt+s://` names `GALACTUS_WITH_TLS` | Rebuild with `-DGALACTUS_WITH_TLS=ON` |
| Unsupported parameter | Convert the object to a supported scalar, container or named value |

Credentials and parameters are never logged by the driver. Keep them out of
application logs when reporting errors.

## Testing

Run from the repository root:

```sh
cmake -S . -B build -DGALACTUS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Add `-DGALACTUS_WITH_TLS=ON` to build the tests with TLS. Fixtures and tests
are self-contained. Without `GDB_TEST_URI`, live database tests are skipped.
For integration tests, use a fresh disposable server and set `GDB_TEST_URI` and
`GDB_TEST_PASSWORD`; the tests create data and exercise transactions.

See [test instructions](tests/README.md) for platform prerequisites, TLS test
settings and environment examples. The shared live suite covers Bolt 5.4,
Bolt 4.4 and TLS, 57 spatial cases, integer boundaries, native values, large
chunked payloads, transactions, recovery after errors and constraint codes.

## What 0.2 includes

Direct `bolt://` and verified `bolt+s://` connections, Bolt 5.4 with a 4.4
fallback, basic authentication, parameterised queries, complete results with
summaries and bookmarks, explicit transactions, recovery after database
errors, classified errors with published code constants, native type mapping
and full spatial interchange. Messages are capped at 64 MiB and nesting at 64
levels.

Windows (SChannel and OpenSSL) has been tested against a real Galactus server
over plaintext and TLS. The [implementation overview](docs/OVERVIEW.md) and
[shared blueprint](docs/BLUEPRINT.md) set out the precise contract and scope.

## Feedback and contribution

Use [GitHub issues](https://github.com/galactusdb/galactus-db-cpp-driver/issues) for reproducible
bugs and focused feature requests. Include runtime/OS versions, driver commit,
expected behaviour and a small reproduction with credentials and sensitive
data removed. Keep changes dependency-light and run the relevant tests before
opening a pull request against `main`.

Visit [galactusdb.com](https://galactusdb.com) for Galactus DB, or browse the
[other language drivers](https://galactusdb.com/product/drivers).
