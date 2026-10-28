#pragma once
#include "value.hpp"
namespace galactus {
// Graph values. element_id strings come from Bolt 5; on Bolt 4.4 they are
// filled with the decimal text of the integer id, which is what Galactus
// returns from elementId(). The encoder writes the form of the protocol.
struct Node : Structure {
  const std::int64_t id;
  const List labels;
  const Map properties;
  const std::string element_id;
  Node(std::int64_t id, List labels, Map properties)
      : Node(id, std::move(labels), std::move(properties), std::to_string(id),
             false) {}
  Node(std::int64_t id, List labels, Map properties, std::string element_id)
      : Node(id, std::move(labels), std::move(properties),
             std::move(element_id), true) {}
  std::uint8_t wire_tag(Protocol) const override { return 78; }
  List wire_fields(Protocol p) const override {
    if (p == Protocol::Bolt5)
      return List{id, labels, properties, element_id};
    return List{id, labels, properties};
  }

private:
  Node(std::int64_t id, List labels, Map properties, std::string element_id,
       bool bolt5)
      : Structure(78, bolt5 ? List{id, labels, properties, element_id}
                            : List{id, labels, properties}),
        id(id), labels(std::move(labels)), properties(std::move(properties)),
        element_id(std::move(element_id)) {}
};
struct Relationship : Structure {
  const std::int64_t id;
  const std::int64_t start_id;
  const std::int64_t end_id;
  const std::string kind;
  const Map properties;
  const std::string element_id;
  const std::string start_element_id;
  const std::string end_element_id;
  Relationship(std::int64_t id, std::int64_t start_id, std::int64_t end_id,
               std::string kind, Map properties)
      : Relationship(id, start_id, end_id, std::move(kind),
                     std::move(properties), std::to_string(id),
                     std::to_string(start_id), std::to_string(end_id), false) {
  }
  Relationship(std::int64_t id, std::int64_t start_id, std::int64_t end_id,
               std::string kind, Map properties, std::string element_id,
               std::string start_element_id, std::string end_element_id)
      : Relationship(id, start_id, end_id, std::move(kind),
                     std::move(properties), std::move(element_id),
                     std::move(start_element_id), std::move(end_element_id),
                     true) {}
  std::uint8_t wire_tag(Protocol) const override { return 82; }
  List wire_fields(Protocol p) const override {
    if (p == Protocol::Bolt5)
      return List{id,         start_id,   end_id,           kind,
                  properties, element_id, start_element_id, end_element_id};
    return List{id, start_id, end_id, kind, properties};
  }

private:
  Relationship(std::int64_t id, std::int64_t start_id, std::int64_t end_id,
               std::string kind, Map properties, std::string element_id,
               std::string start_element_id, std::string end_element_id,
               bool bolt5)
      : Structure(82, bolt5 ? List{id, start_id, end_id, kind, properties,
                                   element_id, start_element_id,
                                   end_element_id}
                            : List{id, start_id, end_id, kind, properties}),
        id(id), start_id(start_id), end_id(end_id), kind(std::move(kind)),
        properties(std::move(properties)), element_id(std::move(element_id)),
        start_element_id(std::move(start_element_id)),
        end_element_id(std::move(end_element_id)) {}
};
struct UnboundRelationship : Structure {
  const std::int64_t id;
  const std::string kind;
  const Map properties;
  const std::string element_id;
  UnboundRelationship(std::int64_t id, std::string kind, Map properties)
      : UnboundRelationship(id, std::move(kind), std::move(properties),
                            std::to_string(id), false) {}
  UnboundRelationship(std::int64_t id, std::string kind, Map properties,
                      std::string element_id)
      : UnboundRelationship(id, std::move(kind), std::move(properties),
                            std::move(element_id), true) {}
  std::uint8_t wire_tag(Protocol) const override { return 114; }
  List wire_fields(Protocol p) const override {
    if (p == Protocol::Bolt5)
      return List{id, kind, properties, element_id};
    return List{id, kind, properties};
  }

private:
  UnboundRelationship(std::int64_t id, std::string kind, Map properties,
                      std::string element_id, bool bolt5)
      : Structure(114, bolt5 ? List{id, kind, properties, element_id}
                             : List{id, kind, properties}),
        id(id), kind(std::move(kind)), properties(std::move(properties)),
        element_id(std::move(element_id)) {}
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
// Offset datetime. seconds are LOCAL epoch seconds (UTC is
// seconds - offset_seconds), as in 0.1. Decoders accept tag F (local seconds)
// and tag I (UTC seconds); the encoder writes F on Bolt 4.4 and I on Bolt 5.
struct DateTime : Structure {
  const std::int64_t seconds;
  const std::int64_t nanoseconds;
  const std::int64_t offset_seconds;
  DateTime(std::int64_t seconds, std::int64_t nanoseconds,
           std::int64_t offset_seconds)
      : Structure(70, List{seconds, nanoseconds, offset_seconds}),
        seconds(seconds), nanoseconds(nanoseconds),
        offset_seconds(offset_seconds) {}
  // Builds the value from UTC epoch seconds (the Bolt 5 form).
  static DateTime from_utc(std::int64_t utc_seconds, std::int64_t nanoseconds,
                           std::int64_t offset_seconds) {
    if ((offset_seconds > 0 && utc_seconds > INT64_MAX - offset_seconds) ||
        (offset_seconds < 0 && utc_seconds < INT64_MIN - offset_seconds))
      throw std::out_of_range("Datetime overflow");
    return DateTime(utc_seconds + offset_seconds, nanoseconds, offset_seconds);
  }
  std::int64_t utc_seconds() const {
    if ((offset_seconds > 0 && seconds < INT64_MIN + offset_seconds) ||
        (offset_seconds < 0 && seconds > INT64_MAX + offset_seconds))
      throw std::out_of_range("Datetime overflow");
    return seconds - offset_seconds;
  }
  std::uint8_t wire_tag(Protocol p) const override {
    return p == Protocol::Bolt5 ? 0x49 : 0x46;
  }
  List wire_fields(Protocol p) const override {
    if (p == Protocol::Bolt5)
      return List{utc_seconds(), nanoseconds, offset_seconds};
    return List{seconds, nanoseconds, offset_seconds};
  }
  bool operator==(const DateTime &o) const {
    return seconds == o.seconds && nanoseconds == o.nanoseconds &&
           offset_seconds == o.offset_seconds;
  }
  bool operator!=(const DateTime &o) const { return !(*this == o); }
};
// Named-zone datetime. Converting between local and UTC seconds needs a time
// zone database, so the value records which basis its seconds use. Tag f
// decodes as Basis::Local and tag i as Basis::Utc; the encoder writes the
// value's own basis (f or i) on every protocol.
struct ZonedDateTime : Structure {
  enum class Basis { Local, Utc };
  const std::int64_t seconds;
  const std::int64_t nanoseconds;
  const std::string zone_id;
  const Basis basis;
  // 0.1 constructor: local seconds in the zone.
  ZonedDateTime(std::int64_t seconds, std::int64_t nanoseconds,
                std::string zone_id)
      : ZonedDateTime(seconds, nanoseconds, std::move(zone_id), Basis::Local) {}
  ZonedDateTime(std::int64_t seconds, std::int64_t nanoseconds,
                std::string zone_id, Basis basis)
      : Structure(basis == Basis::Utc ? 0x69 : 0x66,
                  List{seconds, nanoseconds, zone_id}),
        seconds(seconds), nanoseconds(nanoseconds), zone_id(zone_id),
        basis(basis) {}
  static ZonedDateTime local(std::int64_t local_seconds,
                             std::int64_t nanoseconds, std::string zone_id) {
    return ZonedDateTime(local_seconds, nanoseconds, std::move(zone_id),
                         Basis::Local);
  }
  static ZonedDateTime utc(std::int64_t utc_seconds, std::int64_t nanoseconds,
                           std::string zone_id) {
    return ZonedDateTime(utc_seconds, nanoseconds, std::move(zone_id),
                         Basis::Utc);
  }
  bool operator==(const ZonedDateTime &o) const {
    return basis == o.basis && seconds == o.seconds &&
           nanoseconds == o.nanoseconds && zone_id == o.zone_id;
  }
  bool operator!=(const ZonedDateTime &o) const { return !(*this == o); }
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
    if (f.size() == 4)
      return Node(f[0].as<std::int64_t>(), f[1].as<List>(), f[2].as<Map>(),
                  f[3].as<std::string>());
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return Node(f[0].as<std::int64_t>(), f[1].as<List>(), f[2].as<Map>());
  case 82:
    if (f.size() == 8)
      return Relationship(f[0].as<std::int64_t>(), f[1].as<std::int64_t>(),
                          f[2].as<std::int64_t>(), f[3].as<std::string>(),
                          f[4].as<Map>(), f[5].as<std::string>(),
                          f[6].as<std::string>(), f[7].as<std::string>());
    if (f.size() != 5)
      throw std::invalid_argument("Invalid structure field count");
    return Relationship(f[0].as<std::int64_t>(), f[1].as<std::int64_t>(),
                        f[2].as<std::int64_t>(), f[3].as<std::string>(),
                        f[4].as<Map>());
  case 114:
    if (f.size() == 4)
      return UnboundRelationship(f[0].as<std::int64_t>(),
                                 f[1].as<std::string>(), f[2].as<Map>(),
                                 f[3].as<std::string>());
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
  case 0x49:
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return DateTime::from_utc(f[0].as<std::int64_t>(), f[1].as<std::int64_t>(),
                              f[2].as<std::int64_t>());
  case 102:
  case 0x69:
    if (f.size() != 3)
      throw std::invalid_argument("Invalid structure field count");
    return ZonedDateTime(f[0].as<std::int64_t>(), f[1].as<std::int64_t>(),
                         f[2].as<std::string>(),
                         tag == 0x69 ? ZonedDateTime::Basis::Utc
                                     : ZonedDateTime::Basis::Local);
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
