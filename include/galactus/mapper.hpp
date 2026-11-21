// Object mapping: nodes and relationships as the application's own structs,
// and the Cypher for everyday operations. Standard library only;
// docs/MAPPER.md is the contract every Galactus mapper follows.
//
//   struct Person {
//     std::int64_t id{};
//     std::string name;
//     std::optional<std::string> email;
//   };
//   template <> struct galactus::Mapped<Person> {
//     static auto describe() {
//       return galactus::node<Person>("Person")
//           .key("id", &Person::id)
//           .property("name", &Person::name)
//           .property("email_address", &Person::email);
//     }
//   };
//
//   galactus::Mapper people(driver);
//   people.save(Person{1, "Ann", {}});
//   std::optional<Person> ann = people.find<Person>(1);
#pragma once
#include "driver.hpp"
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace galactus {

/// A type that cannot be mapped, or a value that does not fit it.
struct MappingError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

namespace mapping {

inline std::string checked(std::string name, const std::string &what) {
  if (name.empty() || name.find('`') != std::string::npos)
    throw MappingError(what + " must be a name without backticks, got '" + name + "'");
  return name;
}
inline std::string quote(const std::string &name) { return "`" + name + "`"; }
inline bool is_null(const Value &value) { return std::holds_alternative<std::monostate>(value.data); }

template <class T> struct is_optional : std::false_type {};
template <class T> struct is_optional<std::optional<T>> : std::true_type {};
template <class T> struct is_vector : std::false_type {};
template <class T> struct is_vector<std::vector<T>> : std::true_type {};

// A field as a database value.
template <class F> Value to_value(const F &field) {
  if constexpr (is_optional<F>::value) {
    return field ? to_value(*field) : Value(nullptr);
  } else if constexpr (std::is_same_v<F, Bytes> || std::is_same_v<F, Value>) {
    return Value(field);
  } else if constexpr (is_vector<F>::value) {
    List items;
    for (const auto &item : field)
      items.push_back(to_value(item));
    return Value(std::move(items));
  } else {
    return Value(field);
  }
}

// A database value as a field. Null and an absent property are the field's
// empty value.
template <class F> F from_value(const Value &value) {
  if (is_null(value))
    return F{};
  if constexpr (is_optional<F>::value) {
    return F(from_value<typename F::value_type>(value));
  } else if constexpr (std::is_same_v<F, Value>) {
    return value;
  } else if constexpr (std::is_same_v<F, bool>) {
    return std::get<bool>(value.data);
  } else if constexpr (std::is_integral_v<F>) {
    std::int64_t whole = std::get<std::int64_t>(value.data);
    if (static_cast<std::int64_t>(static_cast<F>(whole)) != whole || (std::is_unsigned_v<F> && whole < 0))
      throw std::out_of_range("the number does not fit the field");
    return static_cast<F>(whole);
  } else if constexpr (std::is_floating_point_v<F>) {
    if (std::holds_alternative<std::int64_t>(value.data))
      return static_cast<F>(std::get<std::int64_t>(value.data));
    return static_cast<F>(std::get<double>(value.data));
  } else if constexpr (std::is_same_v<F, Bytes>) {
    return std::get<Bytes>(value.data);
  } else if constexpr (is_vector<F>::value) {
    F items;
    for (const auto &item : std::get<List>(value.data))
      items.push_back(from_value<typename F::value_type>(item));
    return items;
  } else {
    return std::get<F>(value.data);
  }
}

} // namespace mapping

/// What the mapper knows of a type: made with node() or relationship().
template <class T> class Description {
public:
  struct Field {
    std::string stored;
    std::function<Value(const T &)> get;
    std::function<void(T &, const Value &)> set;
  };

  Description(std::string name, bool is_relationship)
      : name_(mapping::checked(std::move(name), "a label or relationship type")),
        relationship_(is_relationship) {}

  /// A member stored as the property `stored`.
  template <class F> Description &property(std::string stored, F T::*member) {
    stored = mapping::checked(std::move(stored), name_ + "'s property");
    fields_.push_back(Field{
        stored, [member](const T &object) { return mapping::to_value(object.*member); },
        [member, stored, type = name_](T &object, const Value &value) {
          try {
            object.*member = mapping::from_value<F>(value);
          } catch (const MappingError &) {
            throw;
          } catch (const std::exception &) {
            throw MappingError(type + "." + stored + ": the value does not fit the field");
          }
        }});
    return *this;
  }

  /// The member that identifies a node to the application.
  template <class F> Description &key(std::string stored, F T::*member) {
    if (relationship_)
      throw MappingError(name_ + " is a relationship type and has no key");
    if (key_)
      throw MappingError(name_ + " has more than one key");
    property(std::move(stored), member);
    key_ = fields_.size() - 1;
    return *this;
  }

  const std::string &name() const { return name_; }
  bool is_relationship() const { return relationship_; }
  const std::vector<Field> &fields() const { return fields_; }
  const Field *key_field() const { return key_ ? &fields_[*key_] : nullptr; }

  Map properties(const T &object) const {
    Map out;
    for (const auto &field : fields_)
      out[field.stored] = field.get(object);
    return out;
  }
  T build(const Map &properties) const {
    T object{};
    for (const auto &field : fields_) {
      auto found = properties.find(field.stored);
      field.set(object, found == properties.end() ? Value(nullptr) : found->second);
    }
    return object;
  }

private:
  std::string name_;
  bool relationship_;
  std::vector<Field> fields_;
  std::optional<std::size_t> key_;
};

/// Begin describing a node type with its label.
template <class T> Description<T> node(std::string label) { return Description<T>(std::move(label), false); }
/// Begin describing a relationship type with its type name.
template <class T> Description<T> relationship(std::string type) { return Description<T>(std::move(type), true); }

/// Specialise for each mapped type, with a static `describe()` that returns
/// its Description.
template <class T> struct Mapped;

/// The description of a mapped type, made once.
template <class T> const Description<T> &description() {
  static const Description<T> made = Mapped<T>::describe();
  return made;
}

/// Every described property of an object, by its stored name.
template <class T> Map properties_of(const T &object) { return description<T>().properties(object); }

/// An object of a node type from a node.
template <class T> T from_node(const Node &node) {
  if (description<T>().is_relationship())
    throw MappingError(description<T>().name() + " is a relationship type, not a node type");
  return description<T>().build(node.properties);
}

/// An object of a relationship type from a relationship.
template <class T> T from_relationship(const Relationship &relationship) {
  if (!description<T>().is_relationship())
    throw MappingError(description<T>().name() + " is a node type, not a relationship type");
  return description<T>().build(relationship.properties);
}

/// Runs the everyday operations on a driver, each as one statement.
class Mapper {
public:
  explicit Mapper(Driver &driver) : driver_(driver) {}
  Driver &driver() { return driver_; }

  /// Makes a new node of the object and returns it as stored.
  template <class T> T create(const T &object) {
    const auto &shape = node_shape<T>();
    auto result = driver_.execute_query("CREATE (n:" + mapping::quote(shape.name()) + ") SET n = $properties RETURN n",
                                        Map{{"properties", shape.properties(object)}});
    return from_value<T>(result.records.at(0).at("n"));
  }

  /// Creates the node with the object's key, or changes it, and returns it as stored.
  template <class T> T save(const T &object) {
    const auto &shape = keyed<T>();
    auto result = driver_.execute_query(
        "MERGE " + match_key<T>("n", "key") + " SET n += $properties RETURN n",
        Map{{"key", key_value<T>(shape.key_field()->get(object))}, {"properties", shape.properties(object)}});
    return from_value<T>(result.records.at(0).at("n"));
  }

  /// The node with the key, when there is one.
  template <class T> std::optional<T> find(const Value &key) {
    keyed<T>();
    auto result =
        driver_.execute_query("MATCH " + match_key<T>("n", "key") + " RETURN n LIMIT 1", Map{{"key", key_value<T>(key)}});
    if (result.records.empty())
      return std::nullopt;
    return from_value<T>(result.records[0].at("n"));
  }

  /// Every node of the type whose properties equal those in `filter`, which
  /// is keyed by property name.
  template <class T> std::vector<T> find_all(const Map &filter = {}, std::optional<std::int64_t> limit = std::nullopt) {
    const auto &shape = node_shape<T>();
    Map parameters;
    std::string conditions;
    std::size_t index = 0;
    for (const auto &entry : filter) {
      bool known = false;
      for (const auto &field : shape.fields())
        known = known || field.stored == entry.first;
      if (!known)
        throw MappingError(shape.name() + " has no property '" + entry.first + "' to filter by");
      std::string parameter = "p" + std::to_string(index++);
      parameters[parameter] = entry.second;
      conditions += (conditions.empty() ? " WHERE " : " AND ") + ("n." + mapping::quote(entry.first) + " = $" + parameter);
    }
    std::string query = "MATCH (n:" + mapping::quote(shape.name()) + ")" + conditions + " RETURN n";
    if (shape.key_field())
      query += " ORDER BY n." + mapping::quote(shape.key_field()->stored);
    if (limit) {
      if (*limit < 0)
        throw MappingError("limit must be zero or more");
      parameters["limit"] = *limit;
      query += " LIMIT $limit";
    }
    std::vector<T> out;
    for (const auto &row : driver_.execute_query(query, parameters).records)
      out.push_back(from_value<T>(row.at("n")));
    return out;
  }

  /// Removes the node with the key and its relationships. Whether there was one.
  template <class T> bool remove(const Value &key) {
    keyed<T>();
    auto result = driver_.execute_query("MATCH " + match_key<T>("n", "key") + " DETACH DELETE n RETURN count(*) AS deleted",
                                        Map{{"key", key_value<T>(key)}});
    return std::get<std::int64_t>(result.records.at(0).at("deleted").data) > 0;
  }

  /// Joins two stored nodes with a relationship, or changes the one between them.
  template <class A, class R, class B> R relate(const A &from, const R &link, const B &to) {
    const auto &a = keyed<A>();
    const auto &b = keyed<B>();
    const auto &r = relationship_shape<R>();
    auto result = driver_.execute_query(
        "MATCH " + match_key<A>("a", "a") + ", " + match_key<B>("b", "b") + " MERGE (a)-[r:" + mapping::quote(r.name()) +
            "]->(b) SET r += $properties RETURN r",
        Map{{"a", key_value<A>(a.key_field()->get(from))},
            {"b", key_value<B>(b.key_field()->get(to))},
            {"properties", r.properties(link)}});
    if (result.records.empty())
      throw MappingError(a.name() + " or " + b.name() + " was not found to relate");
    return from_value<R>(result.records[0].at("r"));
  }

  /// The relationships of type R from a node to nodes of type B.
  template <class R, class B, class A> std::vector<std::pair<R, B>> related(const A &from) {
    const auto &a = keyed<A>();
    const auto &b = node_shape<B>();
    const auto &r = relationship_shape<R>();
    std::string query = "MATCH " + match_key<A>("a", "a") + "-[r:" + mapping::quote(r.name()) + "]->(b:" +
                        mapping::quote(b.name()) + ") RETURN r, b";
    if (b.key_field())
      query += " ORDER BY b." + mapping::quote(b.key_field()->stored);
    std::vector<std::pair<R, B>> out;
    for (const auto &row : driver_.execute_query(query, Map{{"a", key_value<A>(a.key_field()->get(from))}}).records)
      out.emplace_back(from_value<R>(row.at("r")), from_value<B>(row.at("b")));
    return out;
  }

  /// Runs a statement and maps the first column of each row: a node, a
  /// relationship, or a map of properties.
  template <class T> std::vector<T> query(const std::string &cypher, Map parameters = {}) {
    description<T>();
    auto result = driver_.execute_query(cypher, std::move(parameters));
    std::vector<T> out;
    if (result.keys.empty())
      return out;
    for (const auto &row : result.records)
      out.push_back(from_value<T>(row.at(result.keys[0])));
    return out;
  }

private:
  Driver &driver_;

  template <class T> static const Description<T> &node_shape() {
    const auto &shape = description<T>();
    if (shape.is_relationship())
      throw MappingError(shape.name() + " is a relationship type, not a node type");
    return shape;
  }
  template <class T> static const Description<T> &relationship_shape() {
    const auto &shape = description<T>();
    if (!shape.is_relationship())
      throw MappingError(shape.name() + " is a node type, not a relationship type");
    return shape;
  }
  template <class T> static const Description<T> &keyed() {
    const auto &shape = node_shape<T>();
    if (!shape.key_field())
      throw MappingError(shape.name() + " has no key");
    return shape;
  }
  template <class T> static Value key_value(Value key) {
    if (mapping::is_null(key))
      throw MappingError(description<T>().name() + ": the key has no value");
    return key;
  }
  template <class T> static std::string match_key(const std::string &variable, const std::string &parameter) {
    const auto &shape = keyed<T>();
    return "(" + variable + ":" + mapping::quote(shape.name()) + " {" + mapping::quote(shape.key_field()->stored) +
           ": $" + parameter + "})";
  }
  template <class T> static T from_value(const Value &value) {
    const auto &shape = description<T>();
    if (std::holds_alternative<Map>(value.data))
      return shape.build(std::get<Map>(value.data));
    if (std::holds_alternative<std::shared_ptr<Structure>>(value.data)) {
      const auto &structure = std::get<std::shared_ptr<Structure>>(value.data);
      if (auto node = std::dynamic_pointer_cast<Node>(structure))
        return from_node<T>(*node);
      if (auto relationship = std::dynamic_pointer_cast<Relationship>(structure))
        return from_relationship<T>(*relationship);
      if (auto unbound = std::dynamic_pointer_cast<UnboundRelationship>(structure))
        return relationship_shape<T>().build(unbound->properties);
    }
    throw MappingError(shape.name() + ": expected a node, a relationship or a map");
  }
};

} // namespace galactus
