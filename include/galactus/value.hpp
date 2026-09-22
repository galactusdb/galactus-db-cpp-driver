#pragma once
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace galactus {
struct Structure;
struct Spatial;
struct Value;
using Bytes = std::vector<std::uint8_t>;
using List = std::vector<Value>;
using Map = std::map<std::string, Value>;
struct Value {
  Value(std::chrono::system_clock::time_point value);
  using Storage =
      std::variant<std::monostate, bool, std::int64_t, double, std::string,
                   Bytes, List, Map, std::shared_ptr<Structure>,
                   std::shared_ptr<Spatial>>;
  Storage data;
  Value() = default;
  Value(std::nullptr_t) : data(std::monostate{}) {}
  Value(bool v) : data(v) {}
  template <class T,
            std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                             int> = 0>
  Value(T v) {
    if constexpr (std::is_unsigned_v<T>)
      if (v > static_cast<std::uint64_t>(INT64_MAX))
        throw std::out_of_range("Integer outside signed 64-bit range");
    data = static_cast<std::int64_t>(v);
  }
  Value(float v) : data(double(v)) {}
  Value(double v) : data(v) {}
  Value(const char *v) : data(std::string(v)) {}
  Value(std::string v) : data(std::move(v)) {}
  Value(Bytes v) : data(std::move(v)) {}
  Value(List v) : data(std::move(v)) {}
  Value(Map v) : data(std::move(v)) {}
  Value(std::shared_ptr<Structure> v) : data(std::move(v)) {}
  Value(std::shared_ptr<Spatial> v) : data(std::move(v)) {}
  template <class T, std::enable_if_t<std::is_base_of_v<Structure, T>, int> = 0>
  Value(const T &v)
      : data(std::static_pointer_cast<Structure>(std::make_shared<T>(v))) {}
  Value(const Spatial &v);
  template <class T> const T &as() const {
    if constexpr (std::is_base_of_v<Structure, T>) {
      auto p = std::dynamic_pointer_cast<T>(
          std::get<std::shared_ptr<Structure>>(data));
      if (!p)
        throw std::bad_cast();
      return *p;
    } else if constexpr (std::is_same_v<Spatial, T>)
      return *std::get<std::shared_ptr<Spatial>>(data);
    else
      return std::get<T>(data);
  }
};
struct Structure {
  const std::uint8_t tag;
  const List fields;
  Structure(std::uint8_t tag, List fields)
      : tag(tag), fields(std::move(fields)) {}
  virtual ~Structure() = default;
};
struct Spatial {
  std::string domain;
  std::int64_t srid;
  std::string layout, model;
  Bytes wkb;
  Spatial(std::string domain, std::int64_t srid, std::string layout,
          std::string model, Bytes wkb)
      : domain(std::move(domain)), srid(srid), layout(std::move(layout)),
        model(std::move(model)), wkb(std::move(wkb)) {
    if ((this->domain != "geometry" && this->domain != "geography") ||
        (this->layout != "XY" && this->layout != "XYZ") ||
        this->model != (this->domain == "geometry" ? "planar" : "greatCircle"))
      throw std::invalid_argument("Unsupported spatial metadata");
  }
  Map to_map() const {
    return {{"$gdbType", "spatial"},
            {"version", 1},
            {"domain", domain},
            {"srid", srid},
            {"layout", layout},
            {"model", model},
            {"wkb", wkb}};
  }
  static Spatial from_map(const Map &m) {
    if (m.size() != 7 || m.at("$gdbType").as<std::string>() != "spatial" ||
        m.at("version").as<std::int64_t>() != 1)
      throw std::invalid_argument("Invalid spatial envelope");
    Bytes b;
    if (m.count("wkbHex")) {
      auto h = m.at("wkbHex").as<std::string>();
      if (h.size() % 2)
        throw std::invalid_argument("Invalid WKB hex");
      auto digit = [](char c) {
        if (c >= '0' && c <= '9')
          return c - '0';
        if (c >= 'a' && c <= 'f')
          return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
          return c - 'A' + 10;
        throw std::invalid_argument("Invalid WKB hex");
      };
      for (size_t i = 0; i < h.size(); i += 2)
        b.push_back(
            static_cast<std::uint8_t>(digit(h[i]) * 16 + digit(h[i + 1])));
    } else
      b = m.at("wkb").as<Bytes>();
    return Spatial(m.at("domain").as<std::string>(),
                   m.at("srid").as<std::int64_t>(),
                   m.at("layout").as<std::string>(),
                   m.at("model").as<std::string>(), std::move(b));
  }
};
inline Value::Value(const Spatial &v) : data(std::make_shared<Spatial>(v)) {}
inline Value hydrate_map(Map m) {
  auto type = m.find("$gdbType"), version = m.find("version");
  if (type != m.end() && version != m.end() &&
      std::holds_alternative<std::string>(type->second.data) &&
      type->second.as<std::string>() == "spatial" &&
      std::holds_alternative<std::int64_t>(version->second.data) &&
      version->second.as<std::int64_t>() == 1)
    return Spatial::from_map(m);
  return m;
}
} // namespace galactus
