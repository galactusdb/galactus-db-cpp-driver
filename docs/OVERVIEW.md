# Galactus DB drivers

Seven native **database drivers**, with a shared API and zero third-party runtime
packages in their default builds. Version **0.2.0** is early access. All seven
source repositories are public under [galactusdb](https://github.com/galactusdb),
with `main` as the default branch. Registry packages have not been published;
install from source using the language Quickstart and pin a reviewed commit.
Each driver speaks **Bolt 5.0 to 5.4** directly and falls back to **Bolt 4.4**
when the server offers only that. Each uses its host language directly and needs
neither a helper runtime nor the server source at runtime.

Use **driver** for package names and public APIs. Use **SDK** for a future wider
bundle containing drivers, administration tools, examples, and integrations.

| Language | Folder | Public import / namespace | Runtime package dependencies |
|---|---|---|---|
| C# | [galactus-db-csharp-driver](https://github.com/galactusdb/galactus-db-csharp-driver) | `Galactus`; package `Galactus.Driver` | 0 |
| Rust | [galactus-db-rust-driver](https://github.com/galactusdb/galactus-db-rust-driver) | `galactus` | 0 (`rustls` with the `tls` feature) |
| Java | [galactus-db-java-driver](https://github.com/galactusdb/galactus-db-java-driver) | `db.galactus` | 0 |
| Go | [galactus-db-go-driver](https://github.com/galactusdb/galactus-db-go-driver) | `galactus` | 0 |
| Node / TypeScript | [galactus-db-node-driver](https://github.com/galactusdb/galactus-db-node-driver) | `galactus-db-node-driver` | 0 |
| C++ | [galactus-db-cpp-driver](https://github.com/galactusdb/galactus-db-cpp-driver) | `galactus`; CMake `Galactus::Driver` | 0 (platform TLS libraries with `GALACTUS_WITH_TLS`) |
| Python | [galactus-db-python-driver](https://github.com/galactusdb/galactus-db-python-driver) | `galactus` | 0 |

See the [driver catalogue](https://galactusdb.com/product/drivers) for repository
links and runtime requirements.

## Shared developer experience

1. Open a driver with a URI, username, password, and optional database/timeout
   and trusted certificate file.
2. Execute parameterised Cypher using `execute_query` / `executeQuery` /
   `ExecuteQuery`, following the language's conventions.
3. Read `records` by column name and retain `keys` and `summary` metadata.
4. Use `begin`, `commit`, and `rollback` for explicit transactions.
5. Read the agreed `protocol_version`, the `server_agent`, the `connection_id`
   and the `last_bookmark` from the driver.
6. Close/dispose the driver; closing an open transaction rolls it back on the
   server. Rust and C++ also close on destruction.

No generated models, ORM, schema registration, JSON conversion, or special
integer class is needed for ordinary values. Parameter values are encoded
recursively, and returned values are hydrated recursively, including spatial
values inside graph properties and nested containers.

Database integers are signed 64-bit. Node returns **native `bigint`**, while
JavaScript `number` parameters encode as floating point. Other languages use
their native signed integer representations. Out-of-range integers fail rather
than being truncated. Unknown wire structures remain explicit structures.

See [the blueprint and mapping table](BLUEPRINT.md) for exact semantics and
[the spatial contract](SPATIAL.md) for geometry/geography handling.

## Bolt 5 and 4.4

The handshake proposes 5.4 down to 5.0, then 4.4. On 5.1 and newer the HELLO
message carries `user_agent` and `bolt_agent` and a separate LOGON message
authenticates; on 5.0 and 4.4 one HELLO carries the credentials. A server
started with `GDB_BOLT_MAX_VERSION=4.4` answers 4.4 and the same driver uses
the 4.4 message and value forms.

Bolt 5 changes two value families, and the drivers read both forms on any
version:

- Nodes and relationships carry string element ids. On 4.4 the drivers fill
  them with the decimal text of the integer id, which is what Galactus returns
  from `elementId()` on every version.
- Offset datetimes travel with UTC seconds (tag `I`) on Bolt 5 and local
  seconds (tag `F`) on 4.4. The encoder writes the form of the agreed protocol,
  so values compare equal whichever form carried them. Named-zone datetimes
  record whether their seconds are local or UTC and keep that basis.

`DECIMAL` values are returned as strings holding their exact digits. C#, Java
and Python also accept their standard decimal types as parameters; Go, Rust,
Node and C++ send strings and convert them in Cypher with `decimal($param)`.

## Encrypted connections

Galactus DB terminates TLS itself when started with `GDB_TLS_CERT` and
`GDB_TLS_KEY`, on the same port as plaintext, and then requires TLS on that
port unless `GDB_TLS_REQUIRED=false`.

Every driver supports verified `bolt+s://`. The host name from the URI is used
for SNI and checked against the certificate. Trust comes from the platform
store, or only from the PEM certificates in an optional trusted certificate
file (`trusted_certificates` in C++). There is no option to skip verification,
`bolt+ssc://` is rejected and no driver falls back to plaintext.

| Language | Provider | Default build |
|---|---|---|
| Python, Node, Go, Java, C# | Standard library TLS | Included |
| Rust | `rustls` with the platform verifier | Behind the `tls` cargo feature |
| C++ | SChannel on Windows, OpenSSL on Linux and macOS | Behind the CMake option `GALACTUS_WITH_TLS` |

The Rust and C++ default builds keep zero dependencies and reject `bolt+s://`
with an error naming the feature or option.

C++ specifics: the SChannel backend links `secur32` and `crypt32`. It disables
SChannel's automatic validation and builds the peer chain itself with
`CertGetCertificateChain` (against the platform store, or a chain engine whose
exclusive root store holds the trusted certificate file), then applies
`CERT_CHAIN_POLICY_SSL` with the URI host. It uses the `SCHANNEL_CRED`
credential structure, so connections negotiate TLS 1.2. The OpenSSL backend
uses `SSL_VERIFY_PEER`, `SSL_set1_host` (or the IP address check for IP
literals) and SNI, and loads the trusted file with
`SSL_CTX_load_verify_locations` or the system paths with
`SSL_CTX_set_default_verify_paths`. Defining `GALACTUS_TLS_OPENSSL` selects
OpenSSL on Windows too, which is how it was tested (OpenSSL 1.1.1). Neither
backend checks certificate revocation. On Linux, OpenSSL writes to the socket
without `MSG_NOSIGNAL`; applications should ignore `SIGPIPE`.

## Errors and recovery

When the server answers a query, BEGIN, COMMIT or ROLLBACK with a failure, the
connection is healthy. The drivers send RESET, mark any open transaction as
closed (the server has rolled it back) and raise the database error; the same
driver then runs the next query. A parameter that fails validation raises
before anything is sent and leaves the connection usable.

Connections are discarded after an I/O, timeout or TLS failure, a protocol
violation, a RESET that does not succeed, or failed authentication. A failure
answering COMMIT means the transaction did not commit; a transport failure
during COMMIT is an **unknown commit outcome**, and the drivers never replay a
write automatically.

Database errors keep the server's `code` and message and add the code's
`classification`, `category` and `title`, `is_client_error`, `is_transient`,
`is_database_error`, and `is_retryable` (true for `TransientError` codes), in
each language's naming. Every driver publishes the documented codes as
constants in a codes namespace (`galactus::codes::constraint_validation_failed`
in C++).

## Scope

These are small **single-connection, eager-result drivers**. One connection is
owned by each driver. Use a separate driver for each concurrent unit of work,
especially transactions. Go, Java and C# serialize calls; Node rejects overlapping
operations; Python and C++ must not be shared concurrently; Rust requires mutable
access. There is no connection pool, routing discovery, automatic retry,
managed transaction callback, lazy cursor, ORM, or async API outside Node yet.
Drivers do not send bookmarks, re-authenticate sessions, send telemetry or
route. Speaking Bolt 5 does not imply cluster routing or full compatibility
with other Bolt drivers, and the drivers are not yet declared production ready.

Messages are limited to 64 MiB and nesting to 64 levels. Query results are eager
and may collectively exceed one message; use Cypher `LIMIT` for large queries.
I/O timeouts default to 30 seconds. They bound socket waits (Go uses a deadline
for the complete operation), not total DNS resolution or every query's total
elapsed time.

Returned graph entities are result-only database types; bind their IDs or
properties rather than the entities. Arbitrary application objects and UUIDs
are not implicitly converted. Convert them explicitly to a supported
representation. Use unique result aliases when reading record maps.

## Verification

[Test instructions](../tests/README.md) describe the shared golden wire fixtures
(`values.tsv` in 4.4 mode, `bolt5.tsv` in Bolt 5 mode, `datetime_pairs.tsv`
across both), malformed-input tests, and the isolated real-server runner. The
runner starts fresh servers for three passes: Bolt 5.4, Bolt 4.4
(`GDB_BOLT_MAX_VERSION=4.4`) and TLS with a throwaway test CA. The common
spatial fixture has 57 cases per language, including both domains, XY/XYZ,
empties, holes, nested collections, and antimeridian geography.

Platform validation so far is Windows. The C++ SChannel and OpenSSL backends
both pass the TLS pass there; Linux and macOS builds, pooling under load,
cancellation and production hardening remain release work.

## Further reading

See the [driver blueprint](BLUEPRINT.md) for the API and type mapping contract,
and [spatial mapping](SPATIAL.md) for geometry and geography interchange.
Visit [galactusdb.com](https://galactusdb.com) for the database and product guides.
