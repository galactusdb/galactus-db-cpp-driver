#pragma once
#include "types.hpp"
#include <functional>

namespace galactus {
inline constexpr std::size_t max_message = 64 * 1024 * 1024;
inline bool valid_utf8(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned c = static_cast<unsigned char>(s[i++]);
    if (c < 128)
      continue;
    int n;
    unsigned code, min;
    if (c >= 0xc2 && c <= 0xdf) {
      n = 1;
      code = c & 31;
      min = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      n = 2;
      code = c & 15;
      min = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 3;
      code = c & 7;
      min = 65536;
    } else
      return false;
    if (i + n > s.size())
      return false;
    while (n--) {
      unsigned b = static_cast<unsigned char>(s[i++]);
      if ((b & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (b & 63);
    }
    if (code < min || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
      return false;
  }
  return true;
}
inline Bytes encode(const Value &value) {
  Bytes out;
  auto number = [&](std::uint64_t v, int n) {
    for (int i = n - 1; i >= 0; i--)
      out.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
  };
  auto header = [&](size_t n, int tiny, int base) {
    if (n > UINT32_MAX)
      throw std::length_error("Value too large");
    if (tiny && n < 16)
      out.push_back(static_cast<std::uint8_t>(tiny | n));
    else {
      int size = n < 256 ? 1 : n < 65536 ? 2 : 4;
      out.push_back(
          static_cast<std::uint8_t>(base + (size == 4 ? 2 : size - 1)));
      number(n, size);
    }
  };
  std::function<void(const Value &, int)> write;
  write = [&](const Value &v, int depth) {
    if (depth >= 64)
      throw std::invalid_argument("Nesting exceeds 64 levels");
    std::visit(
        [&](auto &&x) {
          using T = std::decay_t<decltype(x)>;
          if constexpr (std::is_same_v<T, std::monostate>)
            out.push_back(0xc0);
          else if constexpr (std::is_same_v<T, bool>)
            out.push_back(x ? 0xc3 : 0xc2);
          else if constexpr (std::is_same_v<T, std::int64_t>) {
            if (x >= -16 && x <= 127)
              out.push_back(static_cast<std::uint8_t>(x));
            else {
              int n = x >= -128 && x <= 127              ? 1
                      : x >= -32768 && x <= 32767        ? 2
                      : x >= INT32_MIN && x <= INT32_MAX ? 4
                                                         : 8;
              out.push_back(n == 1   ? 0xc8
                            : n == 2 ? 0xc9
                            : n == 4 ? 0xca
                                     : 0xcb);
              number(static_cast<std::uint64_t>(x), n);
            }
          } else if constexpr (std::is_same_v<T, double>) {
            out.push_back(0xc1);
            std::uint64_t bits;
            std::memcpy(&bits, &x, 8);
            number(bits, 8);
          } else if constexpr (std::is_same_v<T, std::string>) {
            if (!valid_utf8(x))
              throw std::invalid_argument("Invalid UTF-8");
            header(x.size(), 0x80, 0xd0);
            out.insert(out.end(), x.begin(), x.end());
          } else if constexpr (std::is_same_v<T, Bytes>) {
            header(x.size(), 0, 0xcc);
            out.insert(out.end(), x.begin(), x.end());
          } else if constexpr (std::is_same_v<T, List>) {
            header(x.size(), 0x90, 0xd4);
            for (const auto &f : x)
              write(f, depth + 1);
          } else if constexpr (std::is_same_v<T, Map>) {
            header(x.size(), 0xa0, 0xd8);
            for (const auto &f : x) {
              write(f.first, depth + 1);
              write(f.second, depth + 1);
            }
          } else if constexpr (std::is_same_v<T, std::shared_ptr<Structure>>) {
            if (!x || x->fields.size() > 15)
              throw std::invalid_argument("Invalid structure");
            out.push_back(static_cast<std::uint8_t>(0xb0 | x->fields.size()));
            out.push_back(x->tag);
            for (const auto &f : x->fields)
              write(f, depth + 1);
          } else if constexpr (std::is_same_v<T, std::shared_ptr<Spatial>>) {
            if (!x)
              throw std::invalid_argument("Null spatial pointer");
            write(x->to_map(), depth + 1);
          }
        },
        v.data);
    if (out.size() > max_message)
      throw std::length_error("Message exceeds 64 MiB");
  };
  write(value, 0);
  return out;
}
inline Value decode(const Bytes &data) {
  size_t p = 0;
  auto take = [&](size_t n) {
    if (n > data.size() - p)
      throw std::invalid_argument("Truncated PackStream");
    size_t old = p;
    p += n;
    return old;
  };
  auto number = [&](int n) {
    auto start = take(n);
    std::uint64_t v = 0;
    for (int i = 0; i < n; i++)
      v = (v << 8) | data[start + i];
    return v;
  };
  std::function<Value(int)> read;
  read = [&](int depth) -> Value {
    if (depth >= 64)
      throw std::invalid_argument("Nesting exceeds 64 levels");
    int m = static_cast<int>(number(1));
    if (m <= 127)
      return m;
    if (m >= 240)
      return m - 256;
    switch (m) {
    case 0xc0:
      return {};
    case 0xc2:
      return false;
    case 0xc3:
      return true;
    case 0xc1: {
      auto bits = number(8);
      double f;
      std::memcpy(&f, &bits, 8);
      return f;
    }
    case 0xc8:
    case 0xc9:
    case 0xca:
    case 0xcb: {
      int n = 1 << (m - 0xc8);
      auto bits = number(n);
      if (n < 8 && (bits & (std::uint64_t(1) << (n * 8 - 1))))
        bits |= (~std::uint64_t(0)) << (n * 8);
      std::int64_t i;
      std::memcpy(&i, &bits, 8);
      return i;
    }
    }
    int kind = m & 0xf0;
    size_t n = m & 15;
    if (m < 0x80 || m > 0xbf) {
      int base = 0;
      for (int b : {0xcc, 0xd0, 0xd4, 0xd8})
        if (m >= b && m <= b + 2) {
          base = b;
          break;
        }
      if (!base)
        throw std::invalid_argument("Unknown PackStream marker");
      kind = base == 0xcc   ? 0xcc
             : base == 0xd0 ? 0x80
             : base == 0xd4 ? 0x90
                            : 0xa0;
      n = static_cast<size_t>(number(1 << (m - base)));
    }
    if (n > data.size() - p)
      throw std::invalid_argument("Invalid collection size");
    if (kind == 0x80) {
      auto start = take(n);
      std::string s(data.begin() + start, data.begin() + start + n);
      if (!valid_utf8(s))
        throw std::invalid_argument("Invalid UTF-8");
      return s;
    }
    if (kind == 0xcc) {
      auto start = take(n);
      return Bytes(data.begin() + start, data.begin() + start + n);
    }
    if (kind == 0xb0) {
      auto tag = static_cast<std::uint8_t>(number(1));
      List f;
      for (size_t i = 0; i < n; i++)
        f.push_back(read(depth + 1));
      return hydrate_structure(tag, f);
    }
    if (kind == 0x90) {
      List f;
      for (size_t i = 0; i < n; i++)
        f.push_back(read(depth + 1));
      return f;
    }
    Map map;
    for (size_t i = 0; i < n; i++) {
      auto k = read(depth + 1).as<std::string>();
      auto v = read(depth + 1);
      if (!map.emplace(k, v).second)
        throw std::invalid_argument("Duplicate map key");
    }
    return hydrate_map(std::move(map));
  };
  auto value = read(0);
  if (p != data.size())
    throw std::invalid_argument("Trailing PackStream bytes");
  return value;
}
inline void validate_parameters(const Value &v, int depth = 0) {
  if (depth >= 64)
    throw std::invalid_argument("Nesting exceeds 64 levels");
  if (auto p = std::get_if<std::shared_ptr<Structure>>(&v.data)) {
    if (!*p)
      throw std::invalid_argument("Null structure");
    switch ((*p)->tag) {
    case 0x44:
    case 0x74:
    case 0x54:
    case 0x64:
    case 0x46:
    case 0x66:
    case 0x45:
    case 0x58:
    case 0x59:
      break;
    default:
      throw std::invalid_argument("Graph entities and unknown structures are "
                                  "result-only; pass properties or an ID");
    }
    for (const auto &f : (*p)->fields)
      validate_parameters(f, depth + 1);
  } else if (auto p = std::get_if<List>(&v.data)) {
    for (const auto &f : *p)
      validate_parameters(f, depth + 1);
  } else if (auto p = std::get_if<Map>(&v.data)) {
    for (const auto &f : *p)
      validate_parameters(f.second, depth + 1);
  }
}
} // namespace galactus
