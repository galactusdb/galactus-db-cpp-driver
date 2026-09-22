#pragma once
#include "value.hpp"
namespace galactus {
struct Node : Structure {
  const std::int64_t id;
  const List labels;
  const Map properties;
  Node(std::int64_t id, List labels, Map properties)
      : Structure(78, List{id, labels, properties}), id(id), labels(labels),
        properties(properties) {}
};
struct Relationship : Structure {
  const std::int64_t id;
  const std::int64_t start_id;
  const std::int64_t end_id;
  const std::string kind;
  const Map properties;
  Relationship(std::int64_t id, std::int64_t start_id, std::int64_t end_id,
               std::string kind, Map properties)
      : Structure(82, List{id, start_id, end_id, kind, properties}), id(id),
        start_id(start_id), end_id(end_id), kind(kind), properties(properties) {
  }
};
struct UnboundRelationship : Structure {
  const std::int64_t id;
  const std::string kind;
  const Map properties;
  UnboundRelationship(std::int64_t id, std::string kind, Map properties)
      : Structure(114, List{id, kind, properties}), id(id), kind(kind),
        properties(properties) {}
};
struct Path : Structure {
  const List nodes;
  const List relationships;
  const List sequence;
  Path(List nodes, List relationships, List sequence)
      : Structure(80, List{nodes, relationships, sequence}), nodes(nodes),
        relationships(relationships), sequence(sequence) {}
};
struct Date : Structure {
  const std::int64_t days;
  Date(std::int64_t days) : Structure(68, List{days}), days(days) {}
};
struct LocalTime : Structure {
  const std::int64_t nanoseconds;
  LocalTime(std::int64_t nanoseconds)
      : Structure(116, List{nanoseconds}), nanoseconds(nanoseconds) {}
};
struct Time : Structure {
  const std::int64_t nanoseconds;
  const std::int64_t offset_seconds;
  Time(std::int64_t nanoseconds, std::int64_t offset_seconds)
      : Structure(84, List{nanoseconds, offset_seconds}),
        nanoseconds(nanoseconds), offset_seconds(offset_seconds) {}
};
struct LocalDateTime : Structure {
  const std::int64_t seconds;
  const std::int64_t nanoseconds;
  LocalDateTime(std::int64_t seconds, std::int64_t nanoseconds)
      : Structure(100, List{seconds, nanoseconds}), seconds(seconds),
        nanoseconds(nanoseconds) {}
};
struct DateTime : Structure {
  const std::int64_t seconds;
  const std::int64_t nanoseconds;
  const std::int64_t offset_seconds;
  DateTime(std::int64_t seconds, std::int64_t nanoseconds,
           std::int64_t offset_seconds)
      : Structure(70, List{seconds, nanoseconds, offset_seconds}),
        seconds(seconds), nanoseconds(nanoseconds),
        offset_seconds(offset_seconds) {}
};
struct ZonedDateTime : Structure {
  const std::int64_t seconds;
  const std::int64_t nanoseconds;
  const std::string zone_id;
  ZonedDateTime(std::int64_t seconds, std::int64_t nanoseconds,
                std::string zone_id)
      : Structure(102, List{seconds, nanoseconds, zone_id}), seconds(seconds),
        nanoseconds(nanoseconds), zone_id(zone_id) {}
};
struct Duration : Structure {
  const std::int64_t months;
  const std::int64_t days;
  const std::int64_t seconds;
  const std::int64_t nanoseconds;
  Duration(std::int64_t months, std::int64_t days, std::int64_t seconds,
           std::int64_t nanoseconds)
      : Structure(69, List{months, days, seconds, nanoseconds}), months(months),
        days(days), seconds(seconds), nanoseconds(nanoseconds) {}
};
struct Point2D : Structure {
  const std::int64_t srid;
  const double x;
  const double y;
  Point2D(std::int64_t srid, double x, double y)
      : Structure(88, List{srid, x, y}), srid(srid), x(x), y(y) {}
};
struct Point3D : Structure {
  const std::int64_t srid;
  const double x;
  const double y;
  const double z;
  Point3D(std::int64_t srid, double x, double y, double z)
      : Structure(89, List{srid, x, y, z}), srid(srid), x(x), y(y), z(z) {}
};
inline Value hydrate_structure(std::uint8_t tag, const List &f) {
  switch (tag) {
  case 78:
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return Node(f[0].as<std::int64_t>(), f[1].as<List>(), f[2].as<Map>());
  case 82:
    if (f.size() != 5)
      throw std::invalid_argument("Invalid structure field count");
    return Relationship(f[0].as<std::int64_t>(), f[1].as<std::int64_t>(),
                        f[2].as<std::int64_t>(), f[3].as<std::string>(),
                        f[4].as<Map>());
  case 114:
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return UnboundRelationship(f[0].as<std::int64_t>(), f[1].as<std::string>(),
                               f[2].as<Map>());
  case 80:
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return Path(f[0].as<List>(), f[1].as<List>(), f[2].as<List>());
  case 68:
    if (f.size() != 1)
      throw std::invalid_argument("Invalid structure field count");
    return Date(f[0].as<std::int64_t>());
  case 116:
    if (f.size() != 1)
      throw std::invalid_argument("Invalid structure field count");
    return LocalTime(f[0].as<std::int64_t>());
  case 84:
    if (f.size() != 2)
      throw std::invalid_argument("Invalid structure field count");
    return Time(f[0].as<std::int64_t>(), f[1].as<std::int64_t>());
  case 100:
    if (f.size() != 2)
      throw std::invalid_argument("Invalid structure field count");
    return LocalDateTime(f[0].as<std::int64_t>(), f[1].as<std::int64_t>());
  case 70:
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return DateTime(f[0].as<std::int64_t>(), f[1].as<std::int64_t>(),
                    f[2].as<std::int64_t>());
  case 102:
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return ZonedDateTime(f[0].as<std::int64_t>(), f[1].as<std::int64_t>(),
                         f[2].as<std::string>());
  case 69:
    if (f.size() != 4)
      throw std::invalid_argument("Invalid structure field count");
    return Duration(f[0].as<std::int64_t>(), f[1].as<std::int64_t>(),
                    f[2].as<std::int64_t>(), f[3].as<std::int64_t>());
  case 88:
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return Point2D(f[0].as<std::int64_t>(), f[1].as<double>(),
                   f[2].as<double>());
  case 89:
    if (f.size() != 4)
      throw std::invalid_argument("Invalid structure field count");
    return Point3D(f[0].as<std::int64_t>(), f[1].as<double>(),
                   f[2].as<double>(), f[3].as<double>());
  default:
    return Structure(tag, f);
  }
}
inline Value::Value(std::chrono::system_clock::time_point value) {
  using Native = std::chrono::system_clock::duration;
  static_assert(Native::period::num == 1 && Native::period::den <= 1000000000 &&
                    1000000000 % Native::period::den == 0,
                "unsupported native clock period");
  const auto ticks = value.time_since_epoch().count();
  auto seconds = ticks / Native::period::den;
  auto remainder = ticks % Native::period::den;
  if (remainder < 0) {
    --seconds;
    remainder += Native::period::den;
  }
  data = std::static_pointer_cast<Structure>(std::make_shared<DateTime>(
      seconds, remainder * (1000000000 / Native::period::den), 0));
}
// Explicit conversion refuses values outside the native clock range/precision.
inline std::chrono::system_clock::time_point
to_system_time(const DateTime &value) {
  using Native = std::chrono::system_clock::duration;
  static_assert(Native::period::num == 1 && Native::period::den <= 1000000000 &&
                    1000000000 % Native::period::den == 0,
                "unsupported native clock period");
  constexpr std::int64_t scale = Native::period::den;
  constexpr std::int64_t nanos_per_tick = 1000000000 / scale;
  if (value.nanoseconds < 0 || value.nanoseconds >= 1000000000 ||
      value.nanoseconds % nanos_per_tick)
    throw std::out_of_range(
        "Datetime cannot be represented at native precision");
  if ((value.offset_seconds > 0 &&
       value.seconds < INT64_MIN + value.offset_seconds) ||
      (value.offset_seconds < 0 &&
       value.seconds > INT64_MAX + value.offset_seconds))
    throw std::out_of_range("Datetime overflow");
  auto seconds = value.seconds - value.offset_seconds;
  auto fraction = value.nanoseconds / nanos_per_tick;
  // For negative fractional instants, multiply the ceiling second so that
  // native time_point::min() does not overflow an intermediate product.
  if (seconds < 0 && fraction > 0) {
    ++seconds;
    if (seconds < INT64_MIN / scale)
      throw std::out_of_range("Datetime outside native range");
    auto ticks = seconds * scale;
    auto remainder = scale - fraction;
    if (ticks < INT64_MIN + remainder)
      throw std::out_of_range("Datetime outside native range");
    return std::chrono::system_clock::time_point(Native(ticks - remainder));
  }
  if (seconds < INT64_MIN / scale || seconds > INT64_MAX / scale)
    throw std::out_of_range("Datetime outside native range");
  auto ticks = seconds * scale;
  if (ticks > INT64_MAX - fraction)
    throw std::out_of_range("Datetime outside native range");
  return std::chrono::system_clock::time_point(Native(ticks + fraction));
}
} // namespace galactus
