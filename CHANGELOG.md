# Changelog

## 0.2.0 - 28/10/2026

### Added

- Bolt 5.0 to 5.4 with a Bolt 4.4 fallback. From 5.1, HELLO carries
  `bolt_agent` and LOGON authenticates.
- `protocol_version()`, `server_agent()`, `connection_id()`,
  `last_bookmark()`, `in_transaction()` and `is_open()` on `Driver`.
- Verified `bolt+s://` behind the CMake option `GALACTUS_WITH_TLS`: SChannel on
  Windows (`secur32`, `crypt32`), OpenSSL on Linux and macOS. SNI and host-name
  checks against the URI host; platform trust by default.
- `DriverOptions` with `database`, `timeout_ms` and `trusted_certificates`, a
  PEM file that replaces the platform trust store.
- `element_id` on `Node`, `Relationship` and `UnboundRelationship`, and
  `start_element_id` / `end_element_id` on `Relationship`. On Bolt 4.4 they hold
  the decimal text of the integer id.
- Datetime tags `I` and `i`. `encode(value, Protocol)` writes offset datetimes
  as `I` on Bolt 5 and `F` on 4.4. `ZonedDateTime` records a local or UTC
  basis (`ZonedDateTime::local`, `ZonedDateTime::utc`) and keeps it on the wire.
- `DatabaseError::classification`, `category` and `title`, with
  `is_client_error()`, `is_transient()`, `is_database_error()` and
  `is_retryable()`.
- Published error codes in `galactus::codes`.
- `ConfigurationError`, `ConnectionError`, `TlsError` and `ProtocolError`.

### Changed

- After a database failure the driver sends RESET and stays usable; an open
  transaction is marked closed. 0.1 discarded the connection.
- A parameter that fails validation or encoding raises before anything is sent
  and leaves the connection open.
- `user_agent` is now `galactus-db-cpp-driver/0.2.0`.
- `bolt+s://` without `GALACTUS_WITH_TLS`, `bolt+ssc://`, routing schemes and
  URI query options raise `ConfigurationError` (a `std::invalid_argument`, as
  before).
- `DECIMAL` results are documented as exact strings; send strings and use
  `decimal($param)` in Cypher.

The 0.1 API keeps working.

## 0.1.0

- First release: Bolt 4.4, parameterised queries, explicit transactions,
  native type mapping and spatial interchange.
