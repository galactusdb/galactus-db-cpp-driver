#pragma once
#include "packstream.hpp"
#include <chrono>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace galactus {
inline constexpr const char *driver_version = "0.2.0";
inline constexpr const char *driver_product = "galactus-db-cpp-driver/0.2.0";

// Invalid URI, option or argument. Raised before any network activity.
struct ConfigurationError : std::invalid_argument {
  using std::invalid_argument::invalid_argument;
};
// I/O failure, timeout or closed socket. The connection is discarded.
struct ConnectionError : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// TLS handshake, certificate or record failure. The connection is discarded.
struct TlsError : ConnectionError {
  using ConnectionError::ConnectionError;
};
// The peer broke the Bolt protocol. The connection is discarded.
struct ProtocolError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Published server codes. Codes the server adds later still arrive as
// plain strings in DatabaseError::code.
namespace codes {
inline constexpr const char *constraint_validation_failed =
    "Galactus.ClientError.Schema.ConstraintValidationFailed";
inline constexpr const char *type_error =
    "Galactus.ClientError.Statement.TypeError";
inline constexpr const char *argument_error =
    "Galactus.ClientError.Statement.ArgumentError";
inline constexpr const char *sql_too_many_rows =
    "Galactus.ClientError.Statement.SqlTooManyRows";
inline constexpr const char *sql_cast_failed =
    "Galactus.ClientError.Statement.SqlCastFailed";
inline constexpr const char *table_too_large =
    "Galactus.ClientError.Statement.TableTooLarge";
inline constexpr const char *unsupported_feature =
    "Galactus.DatabaseError.Storage.UnsupportedFeature";
inline constexpr const char *procedure_not_found =
    "Galactus.ClientError.Procedure.ProcedureNotFound";
inline constexpr const char *license_restricted =
    "Galactus.ClientError.License.Restricted";
inline constexpr const char *read_only_replica =
    "Galactus.ClientError.Database.ReadOnlyReplica";
inline constexpr const char *transaction_timed_out =
    "Galactus.ClientError.Transaction.TimedOut";
inline constexpr const char *transaction_terminated =
    "Galactus.ClientError.Transaction.Terminated";
inline constexpr const char *snapshot_too_old =
    "Galactus.TransientError.Transaction.SnapshotTooOld";
inline constexpr const char *forbidden =
    "Galactus.ClientError.Security.Forbidden";
inline constexpr const char *syntax_error =
    "Neo.ClientError.Statement.SyntaxError";
inline constexpr const char *semantic_error =
    "Neo.ClientError.Statement.SemanticError";
inline constexpr const char *unauthorized =
    "Neo.ClientError.Security.Unauthorized";
inline constexpr const char *database_not_found =
    "Neo.ClientError.Database.DatabaseNotFound";
} // namespace codes

// A FAILURE from the server. code and what() are the server's code and
// message; classification, category and title are the parts of a
// {Vendor}.{Classification}.{Category}.{Title} code (empty otherwise).
struct DatabaseError : std::runtime_error {
  std::string code;
  Map metadata;
  std::string classification;
  std::string category;
  std::string title;
  explicit DatabaseError(Map m)
      : std::runtime_error(m.count("message")
                               ? m.at("message").as<std::string>()
                               : "Database failure"),
        code(m.count("code") ? m.at("code").as<std::string>()
                             : "DatabaseError"),
        metadata(std::move(m)) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
      auto dot = code.find('.', start);
      parts.push_back(code.substr(start, dot - start));
      if (dot == std::string::npos)
        break;
      start = dot + 1;
    }
    if (parts.size() == 4) {
      classification = parts[1];
      category = parts[2];
      title = parts[3];
    }
  }
  bool is_client_error() const { return classification == "ClientError"; }
  bool is_transient() const { return classification == "TransientError"; }
  bool is_database_error() const { return classification == "DatabaseError"; }
  // True for TransientError codes only. The driver never retries by itself.
  bool is_retryable() const { return is_transient(); }
};

struct Result {
  std::vector<std::string> keys;
  std::vector<Map> records;
  Map summary;
};

struct DriverOptions {
  // Empty: the server selects its configured default database.
  std::string database;
  int timeout_ms = 30000;
  // Path to one or more PEM certificates that replace the platform trust
  // store. Only valid with bolt+s://.
  std::string trusted_certificates;
};

namespace detail {
inline std::string bolt_language() {
#if defined(_MSVC_LANG)
  long standard = _MSVC_LANG;
#else
  long standard = __cplusplus;
#endif
  int year = standard >= 202302L   ? 23
             : standard >= 202002L ? 20
             : standard >= 201703L ? 17
                                   : 14;
  return "C++/" + std::to_string(year);
}
inline std::string bolt_language_details() {
#if defined(__clang__)
  return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
  return "MSVC " + std::to_string(_MSC_FULL_VER);
#else
  return "unknown compiler";
#endif
}
inline std::string bolt_platform() {
#if defined(_WIN32)
  std::string os = "Windows";
#elif defined(__APPLE__)
  std::string os = "macOS";
#elif defined(__linux__)
  std::string os = "Linux";
#elif defined(__FreeBSD__)
  std::string os = "FreeBSD";
#else
  std::string os = "unknown";
#endif
#if defined(__x86_64__) || defined(_M_X64)
  return os + " x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return os + " aarch64";
#elif defined(__i386__) || defined(_M_IX86)
  return os + " x86";
#elif defined(__arm__) || defined(_M_ARM)
  return os + " arm";
#else
  return os;
#endif
}
} // namespace detail
inline Map bolt_agent() {
  return {{"product", driver_product},
          {"language", detail::bolt_language()},
          {"language_details", detail::bolt_language_details()},
          {"platform", detail::bolt_platform()}};
}
} // namespace galactus

#ifdef GALACTUS_WITH_TLS
#include "tls.hpp"
#endif

namespace galactus {
class Driver {
#ifdef _WIN32
  using Socket = SOCKET;
  static constexpr Socket invalid = INVALID_SOCKET;
  struct Winsock {
    Winsock() {
      WSADATA data;
      if (WSAStartup(MAKEWORD(2, 2), &data))
        throw std::runtime_error("WSAStartup failed");
    }
    ~Winsock() { WSACleanup(); }
  };
  static void close_socket(Socket s) { closesocket(s); }
#else
  using Socket = int;
  static constexpr Socket invalid = -1;
  static void close_socket(Socket s) { ::close(s); }
#endif
  Socket socket_ = invalid;
#ifdef GALACTUS_WITH_TLS
  std::unique_ptr<detail::TlsStream> tls_;
#endif
  std::string database_;
  bool transaction_ = false;
  Protocol protocol_ = Protocol::Bolt44;
  std::string protocol_version_;
  std::string server_agent_;
  std::string connection_id_;
  std::string last_bookmark_;

  void write_all(const Bytes &bytes) {
#ifdef GALACTUS_WITH_TLS
    if (tls_) {
      tls_->write(bytes.data(), bytes.size());
      return;
    }
#endif
    size_t p = 0;
    while (p < bytes.size()) {
#if defined(MSG_NOSIGNAL)
      int flags = MSG_NOSIGNAL;
#else
      int flags = 0;
#endif
      auto n = ::send(socket_, reinterpret_cast<const char *>(bytes.data() + p),
                      static_cast<int>(bytes.size() - p), flags);
      if (n <= 0)
        throw ConnectionError("Socket write failed");
      p += static_cast<size_t>(n);
    }
  }
  Bytes read_exact(size_t n) {
    Bytes bytes(n);
    size_t p = 0;
    while (p < n) {
#ifdef GALACTUS_WITH_TLS
      if (tls_) {
        p += tls_->read(bytes.data() + p, n - p);
        continue;
      }
#endif
      auto got = ::recv(socket_, reinterpret_cast<char *>(bytes.data() + p),
                        static_cast<int>(n - p), 0);
      if (got <= 0)
        throw ConnectionError("Socket read failed or connection closed");
      p += static_cast<size_t>(got);
    }
    return bytes;
  }
  // Encodes a complete request before anything is written.
  Bytes frame(std::uint8_t tag, List fields) const {
    auto b = encode(Structure(tag, std::move(fields)), protocol_);
    Bytes out;
    for (size_t p = 0; p < b.size(); p += 65535) {
      auto n = std::min<size_t>(65535, b.size() - p);
      out.push_back(static_cast<std::uint8_t>(n >> 8));
      out.push_back(static_cast<std::uint8_t>(n));
      out.insert(out.end(), b.begin() + p, b.begin() + p + n);
    }
    out.insert(out.end(), {0, 0});
    return out;
  }
  void send(std::uint8_t tag, List fields = {}) {
    write_all(frame(tag, std::move(fields)));
  }
  // Returns SUCCESS (0x70), RECORD (0x71) or IGNORED (0x7e); throws
  // DatabaseError for FAILURE and ProtocolError for anything else.
  std::pair<std::uint8_t, Value> receive() {
    Bytes body;
    for (;;) {
      auto h = read_exact(2);
      size_t n = h[0] * 256 + h[1];
      if (!n) {
        if (!body.empty())
          break;
        continue;
      }
      if (body.size() + n > max_message)
        throw std::length_error("Message exceeds 64 MiB");
      auto b = read_exact(n);
      body.insert(body.end(), b.begin(), b.end());
    }
    auto value = decode(body);
    auto structure = std::get_if<std::shared_ptr<Structure>>(&value.data);
    if (!structure || !*structure)
      throw ProtocolError("Invalid Bolt response");
    const auto &m = **structure;
    if (m.tag == 0x7e && m.fields.size() <= 1)
      return {m.tag, Value()};
    if (m.fields.size() != 1)
      throw ProtocolError("Invalid Bolt response");
    if (m.tag == 0x7f) {
      auto failure = std::get_if<Map>(&m.fields[0].data);
      if (!failure)
        throw ProtocolError("Invalid Bolt failure");
      throw DatabaseError(*failure);
    }
    if (m.tag != 0x70 && m.tag != 0x71)
      throw ProtocolError("Unexpected Bolt response");
    return {m.tag, m.fields[0]};
  }
  Map success() {
    auto m = receive();
    if (m.first != 0x70)
      throw ProtocolError("Expected SUCCESS");
    auto map = std::get_if<Map>(&m.second.data);
    if (!map)
      throw ProtocolError("Invalid SUCCESS metadata");
    return *map;
  }
  void ensure_open() const {
    if (socket_ == invalid)
      throw ConnectionError("Driver is closed");
  }
  // After a database FAILURE the connection itself is healthy: RESET clears
  // the failed state (the server rolls back any open transaction). If RESET
  // does not end in SUCCESS the connection is discarded.
  void recover() noexcept {
    transaction_ = false;
    try {
      send(0x0f);
      for (;;) {
        auto m = receive();
        if (m.first == 0x70)
          return;
        if (m.first != 0x7e)
          break;
      }
    } catch (...) {
    }
    close();
  }
  template <class F> auto guarded(F &&f) -> decltype(f()) {
    try {
      return f();
    } catch (const DatabaseError &) {
      recover();
      throw;
    } catch (...) {
      close();
      throw;
    }
  }
  void remember_bookmark(const Map &summary) {
    auto b = summary.find("bookmark");
    if (b != summary.end())
      if (auto s = std::get_if<std::string>(&b->second.data))
        last_bookmark_ = *s;
  }
  Map finish(std::uint8_t tag) {
    ensure_open();
    if (!transaction_)
      throw std::logic_error("No transaction");
    return guarded([&] {
      send(tag);
      auto m = success();
      transaction_ = false;
      if (tag == 0x12)
        remember_bookmark(m);
      return m;
    });
  }
  Map extra_metadata() const {
    Map extra;
    if (!database_.empty())
      extra["db"] = database_;
    return extra;
  }
  void connect(const std::string &uri, const std::string &username,
               const std::string &password, const DriverOptions &options) {
#ifdef _WIN32
    static Winsock winsock;
#endif
    auto scheme_end = uri.find("://");
    if (scheme_end == std::string::npos)
      throw ConfigurationError("Expected a bolt:// or bolt+s:// URI");
    auto scheme = uri.substr(0, scheme_end);
    bool tls = false;
    if (scheme == "bolt+s")
      tls = true;
    else if (scheme == "bolt+ssc")
      throw ConfigurationError(
          "bolt+ssc:// is not supported because it skips certificate "
          "verification; use bolt+s:// with trusted_certificates");
    else if (scheme == "neo4j" || scheme == "neo4j+s" || scheme == "neo4j+ssc")
      throw ConfigurationError(
          "Routing URIs are not supported; use bolt:// or bolt+s://");
    else if (scheme != "bolt")
      throw ConfigurationError("Unsupported URI scheme " + scheme +
                               "://; use bolt:// or bolt+s://");
    if (options.timeout_ms <= 0)
      throw ConfigurationError("Timeout must be positive");
    if (!options.trusted_certificates.empty()) {
      if (!tls)
        throw ConfigurationError(
            "trusted_certificates applies only to bolt+s:// URIs");
      std::ifstream file(options.trusted_certificates);
      if (!file)
        throw ConfigurationError("Cannot read trusted certificate file " +
                                 options.trusted_certificates);
    }
#ifndef GALACTUS_WITH_TLS
    if (tls)
      throw ConfigurationError(
          "bolt+s:// needs TLS support: build with the CMake option "
          "GALACTUS_WITH_TLS (macro GALACTUS_WITH_TLS)");
#endif
    auto addr = uri.substr(scheme_end + 3);
    if (!addr.empty() && addr.back() == '/')
      addr.pop_back();
    if (addr.empty() || addr.find_first_of("/?#@") != std::string::npos)
      throw ConfigurationError(
          "Invalid Bolt URI: expected scheme://host:port without "
          "credentials, path or query options");
    std::string host, port = "7687";
    if (addr[0] == '[') {
      auto end = addr.find(']');
      if (end == std::string::npos)
        throw ConfigurationError("Invalid IPv6 URI");
      host = addr.substr(1, end - 1);
      if (end + 1 < addr.size()) {
        if (addr[end + 1] != ':')
          throw ConfigurationError("Invalid port");
        port = addr.substr(end + 2);
      }
    } else {
      auto colon = addr.find(':');
      host = addr.substr(0, colon);
      if (colon != std::string::npos)
        port = addr.substr(colon + 1);
    }
    const int timeout_ms = options.timeout_ms;
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo *addresses = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses))
      throw ConnectionError("Address resolution failed");
    for (auto a = addresses; a; a = a->ai_next) {
      Socket s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (s == invalid)
        continue;
#ifdef _WIN32
      u_long mode = 1;
      ioctlsocket(s, FIONBIO, &mode);
#else
      int old = fcntl(s, F_GETFL, 0);
      fcntl(s, F_SETFL, old | O_NONBLOCK);
#endif
      int connected = ::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
      if (connected != 0) {
        fd_set writes;
        FD_ZERO(&writes);
        FD_SET(s, &writes);
        timeval wait{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        if (select(static_cast<int>(s + 1), nullptr, &writes, nullptr, &wait) >
            0) {
          int error = 0;
#ifdef _WIN32
          int length = sizeof(error);
          getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error),
                     &length);
#else
          socklen_t length = sizeof(error);
          getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &length);
#endif
          connected = error == 0 ? 0 : -1;
        }
      }
      if (connected != 0) {
        close_socket(s);
        continue;
      }
#ifdef _WIN32
      mode = 0;
      ioctlsocket(s, FIONBIO, &mode);
      DWORD ms = static_cast<DWORD>(timeout_ms);
      setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&ms), sizeof(ms));
      setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char *>(&ms), sizeof(ms));
#else
      fcntl(s, F_SETFL, old);
      timeval wait{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
      setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
      setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
#endif
      socket_ = s;
      break;
    }
    freeaddrinfo(addresses);
    if (socket_ == invalid)
      throw ConnectionError("Connection failed");
    try {
#ifdef GALACTUS_WITH_TLS
      if (tls)
        tls_ = std::make_unique<detail::TlsStream>(
            socket_, host, options.trusted_certificates);
#endif
      // Propose 5.4 down to 5.0, then 4.4.
      write_all({0x60, 0x60, 0xb0, 0x17, 0, 4, 4, 5, 0, 0,
                 4,    4,    0,    0,    0, 0, 0, 0, 0, 0});
      auto agreed = read_exact(4);
      int minor = 0;
      if (agreed[0] == 0 && agreed[1] == 0 && agreed[3] == 5 && agreed[2] <= 4) {
        protocol_ = Protocol::Bolt5;
        minor = agreed[2];
        protocol_version_ = "5." + std::to_string(minor);
      } else if (agreed == Bytes{0, 0, 4, 4}) {
        protocol_ = Protocol::Bolt44;
        protocol_version_ = "4.4";
      } else
        throw ProtocolError(
            "Server did not agree a supported Bolt version (5.0-5.4 or 4.4)");
      Map auth{{"scheme", "basic"},
               {"principal", username},
               {"credentials", password}};
      Map hello{{"user_agent", driver_product}};
      Map greeting;
      if (protocol_ == Protocol::Bolt5 && minor >= 1) {
        // 5.1+: HELLO only greets; LOGON authenticates. Not pipelined.
        hello["bolt_agent"] = bolt_agent();
        send(0x01, {std::move(hello)});
        greeting = success();
        send(0x6a, {std::move(auth)});
        success();
      } else {
        for (auto &entry : auth)
          hello.insert(entry);
        send(0x01, {std::move(hello)});
        greeting = success();
      }
      auto text = [&](const char *key) {
        auto found = greeting.find(key);
        if (found != greeting.end())
          if (auto s = std::get_if<std::string>(&found->second.data))
            return *s;
        return std::string();
      };
      server_agent_ = text("server");
      connection_id_ = text("connection_id");
    } catch (...) {
      // Includes authentication failure, which is terminal.
      close();
      throw;
    }
  }

public:
  // 0.1 signature: plaintext or (with GALACTUS_WITH_TLS) platform-trusted TLS.
  Driver(const std::string &uri, const std::string &username,
         const std::string &password, std::string database = "",
         int timeout_ms = 30000) {
    DriverOptions options;
    options.database = std::move(database);
    options.timeout_ms = timeout_ms;
    database_ = options.database;
    connect(uri, username, password, options);
  }
  Driver(const std::string &uri, const std::string &username,
         const std::string &password, const DriverOptions &options)
      : database_(options.database) {
    connect(uri, username, password, options);
  }
  Driver(const Driver &) = delete;
  Driver &operator=(const Driver &) = delete;
  ~Driver() { close(); }

  // Agreed Bolt version, for example "5.4" or "4.4".
  const std::string &protocol_version() const { return protocol_version_; }
  Protocol protocol() const { return protocol_; }
  const std::string &server_agent() const { return server_agent_; }
  const std::string &connection_id() const { return connection_id_; }
  // Most recent bookmark from an autocommit query or COMMIT (empty if none).
  const std::string &last_bookmark() const { return last_bookmark_; }
  bool in_transaction() const { return transaction_; }
  bool is_open() const { return socket_ != invalid; }

  Result execute_query(const std::string &query, Map params = {}) {
    ensure_open();
    validate_parameters(Value(params));
    // Encoding errors leave the connection untouched.
    auto run = frame(0x10, {query, std::move(params),
                            transaction_ ? Map{} : extra_metadata()});
    return guarded([&] {
      write_all(run);
      auto meta = success();
      Result result;
      auto fields = meta.find("fields");
      if (fields == meta.end())
        throw ProtocolError("RUN SUCCESS without fields");
      for (const auto &k : fields->second.as<List>())
        result.keys.push_back(k.as<std::string>());
      send(0x3f, {Map{{"n", -1}}});
      for (;;) {
        auto m = receive();
        if (m.first == 0x70) {
          auto summary = m.second.as<Map>();
          auto more = summary.find("has_more");
          if (more != summary.end() && more->second.as<bool>()) {
            send(0x3f, {Map{{"n", -1}}});
            continue;
          }
          for (const auto &e : summary)
            meta[e.first] = e.second;
          if (!transaction_)
            remember_bookmark(summary);
          result.summary = std::move(meta);
          return result;
        }
        if (m.first != 0x71)
          throw ProtocolError("Unexpected response to PULL");
        const auto &values = m.second.as<List>();
        if (values.size() != result.keys.size())
          throw ProtocolError("Record width mismatch");
        Map row;
        for (size_t i = 0; i < values.size(); i++)
          row[result.keys[i]] = values[i];
        result.records.push_back(std::move(row));
      }
    });
  }
  void begin(bool read_only = false) {
    ensure_open();
    if (transaction_)
      throw std::logic_error("Transaction already open");
    auto extra = extra_metadata();
    extra["mode"] = read_only ? "r" : "w";
    guarded([&] {
      send(0x11, {std::move(extra)});
      success();
      transaction_ = true;
    });
  }
  // A DatabaseError from COMMIT means the transaction did not commit. A
  // ConnectionError means the outcome is unknown; never retry it blindly.
  Map commit() { return finish(0x12); }
  void rollback() { finish(0x13); }
  void close() noexcept {
    transaction_ = false;
#ifdef GALACTUS_WITH_TLS
    if (tls_) {
      tls_->shutdown();
      tls_.reset();
    }
#endif
    if (socket_ != invalid) {
      close_socket(socket_);
      socket_ = invalid;
    }
  }
};
} // namespace galactus
