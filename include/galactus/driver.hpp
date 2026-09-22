#pragma once
#include "packstream.hpp"
#include <chrono>
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
struct DatabaseError : std::runtime_error {
  std::string code;
  Map metadata;
  explicit DatabaseError(Map m)
      : std::runtime_error(m.count("message")
                               ? m.at("message").as<std::string>()
                               : "Database failure"),
        code(m.count("code") ? m.at("code").as<std::string>()
                             : "DatabaseError"),
        metadata(std::move(m)) {}
};
struct Result {
  std::vector<std::string> keys;
  std::vector<Map> records;
  Map summary;
};
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
  std::string database_;
  bool transaction_ = false;
  void write_all(const Bytes &bytes) {
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
        throw std::runtime_error("Socket write failed");
      p += static_cast<size_t>(n);
    }
  }
  Bytes read_exact(size_t n) {
    Bytes bytes(n);
    size_t p = 0;
    while (p < n) {
      auto got = ::recv(socket_, reinterpret_cast<char *>(bytes.data() + p),
                        static_cast<int>(n - p), 0);
      if (got <= 0)
        throw std::runtime_error("Socket read failed or connection closed");
      p += static_cast<size_t>(got);
    }
    return bytes;
  }
  void send(std::uint8_t tag, List fields = {}) {
    if (tag == 0x10 || tag == 0x11) {
      auto index = tag == 0x10 ? 2 : 0;
      auto extra = fields.at(index).as<Map>();
      auto db = extra.find("db");
      if (db != extra.end() && db->second.as<std::string>().empty()) extra.erase(db);
      fields.at(index) = std::move(extra);
    }
    auto b = encode(Structure(tag, std::move(fields)));
    Bytes frame;
    for (size_t p = 0; p < b.size(); p += 65535) {
      auto n = std::min<size_t>(65535, b.size() - p);
      frame.push_back(static_cast<std::uint8_t>(n >> 8));
      frame.push_back(static_cast<std::uint8_t>(n));
      frame.insert(frame.end(), b.begin() + p, b.begin() + p + n);
    }
    frame.insert(frame.end(), {0, 0});
    write_all(frame);
  }
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
    const auto &m = value.as<Structure>();
    if (m.fields.size() != 1)
      throw std::runtime_error("Invalid Bolt response");
    if (m.tag == 0x7f)
      throw DatabaseError(m.fields[0].as<Map>());
    if (m.tag != 0x70 && m.tag != 0x71)
      throw std::runtime_error("Unexpected Bolt response");
    return {m.tag, m.fields[0]};
  }
  Map success() {
    auto m = receive();
    if (m.first != 0x70)
      throw std::runtime_error("Expected SUCCESS");
    return m.second.as<Map>();
  }
  Map finish(std::uint8_t tag) {
    if (!transaction_)
      throw std::logic_error("No transaction");
    try {
      send(tag);
      auto m = success();
      transaction_ = false;
      return m;
    } catch (...) {
      close();
      throw;
    }
  }

public:
  Driver(const std::string &uri, const std::string &username,
         const std::string &password, std::string database = "",
         int timeout_ms = 30000)
      : database_(std::move(database)) {
#ifdef _WIN32
    static Winsock winsock;
#endif
    if (uri.compare(0, 7, "bolt://") != 0 || timeout_ms <= 0)
      throw std::invalid_argument("Expected bolt://host:port and positive "
                                  "timeout; TLS requires a tunnel");
    auto addr = uri.substr(7);
    if (!addr.empty() && addr.back() == '/')
      addr.pop_back();
    if (addr.empty() || addr.find_first_of("/?#@") != std::string::npos)
      throw std::invalid_argument("Invalid Bolt URI");
    std::string host, port = "7687";
    if (addr[0] == '[') {
      auto end = addr.find(']');
      if (end == std::string::npos)
        throw std::invalid_argument("Invalid IPv6 URI");
      host = addr.substr(1, end - 1);
      if (end + 1 < addr.size()) {
        if (addr[end + 1] != ':')
          throw std::invalid_argument("Invalid port");
        port = addr.substr(end + 2);
      }
    } else {
      auto colon = addr.find(':');
      host = addr.substr(0, colon);
      if (colon != std::string::npos)
        port = addr.substr(colon + 1);
    }
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo *addresses = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses))
      throw std::runtime_error("Address resolution failed");
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
      throw std::runtime_error("Connection failed");
    try {
      write_all({0x60, 0x60, 0xb0, 0x17, 0, 0, 4, 4, 0, 0,
                 0,    0,    0,    0,    0, 0, 0, 0, 0, 0});
      if (read_exact(4) != Bytes{0, 0, 4, 4})
        throw std::runtime_error("Server did not select Bolt 4.4");
      send(1, {Map{{"user_agent", "galactus-cpp/0.1"},
                   {"scheme", "basic"},
                   {"principal", username},
                   {"credentials", password}}});
      success();
    } catch (...) {
      close();
      throw;
    }
  }
  Driver(const Driver &) = delete;
  Driver &operator=(const Driver &) = delete;
  ~Driver() { close(); }
  Result execute_query(const std::string &query, Map params = {}) {
    validate_parameters(Value(params));
    try {
      send(0x10, {query, std::move(params),
                  transaction_ ? Map{} : Map{{"db", database_}}});
      auto meta = success();
      Result result;
      for (const auto &k : meta.at("fields").as<List>())
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
          result.summary = std::move(meta);
          return result;
        }
        const auto &values = m.second.as<List>();
        if (values.size() != result.keys.size())
          throw std::runtime_error("Record width mismatch");
        Map row;
        for (size_t i = 0; i < values.size(); i++)
          row[result.keys[i]] = values[i];
        result.records.push_back(std::move(row));
      }
    } catch (...) {
      close();
      throw;
    }
  }
  void begin(bool read_only = false) {
    if (transaction_)
      throw std::logic_error("Transaction already open");
    try {
      send(0x11, {Map{{"db", database_}, {"mode", read_only ? "r" : "w"}}});
      success();
      transaction_ = true;
    } catch (...) {
      close();
      throw;
    }
  }
  Map commit() { return finish(0x12); }
  void rollback() { finish(0x13); }
  void close() {
    transaction_ = false;
    if (socket_ != invalid) {
      close_socket(socket_);
      socket_ = invalid;
    }
  }
};
} // namespace galactus
