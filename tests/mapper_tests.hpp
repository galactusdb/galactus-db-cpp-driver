// The mapper's shared test (docs/MAPPER.md), and its checks that need no
// database. Included by test.cpp, which supplies check().
#pragma once
#include <galactus/mapper.hpp>
#include <iostream>

void check(bool b, const std::string &s);

namespace mapper_tests {
using namespace galactus;

struct Person {
  std::int64_t id{};
  std::string name;
  std::optional<std::string> email;
  std::optional<std::int64_t> age;
  bool operator==(const Person &o) const { return id == o.id && name == o.name && email == o.email && age == o.age; }
};
struct Company {
  std::string name;
  bool operator==(const Company &o) const { return name == o.name; }
};
struct WorksAt {
  std::int64_t since{};
  std::string role;
  bool operator==(const WorksAt &o) const { return since == o.since && role == o.role; }
};
struct Note {
  std::string text;
  int stars{};
  std::vector<std::string> tags;
  bool operator==(const Note &o) const { return text == o.text && stars == o.stars && tags == o.tags; }
};
struct BadLabel {
  std::int64_t x{};
};
struct BadProperty {
  std::int64_t x{};
};
} // namespace mapper_tests

namespace galactus {
template <> struct Mapped<mapper_tests::Person> {
  static auto describe() {
    using mapper_tests::Person;
    return node<Person>("Person")
        .key("id", &Person::id)
        .property("name", &Person::name)
        .property("email_address", &Person::email)
        .property("age", &Person::age);
  }
};
template <> struct Mapped<mapper_tests::Company> {
  static auto describe() { return node<mapper_tests::Company>("Company").key("name", &mapper_tests::Company::name); }
};
template <> struct Mapped<mapper_tests::WorksAt> {
  static auto describe() {
    using mapper_tests::WorksAt;
    return relationship<WorksAt>("WORKS_AT").property("since", &WorksAt::since).property("role", &WorksAt::role);
  }
};
template <> struct Mapped<mapper_tests::Note> {
  static auto describe() {
    using mapper_tests::Note;
    return node<Note>("Note").property("text", &Note::text).property("stars", &Note::stars).property("tags", &Note::tags);
  }
};
template <> struct Mapped<mapper_tests::BadLabel> {
  static auto describe() { return node<mapper_tests::BadLabel>("Bad`Label").property("x", &mapper_tests::BadLabel::x); }
};
template <> struct Mapped<mapper_tests::BadProperty> {
  static auto describe() { return node<mapper_tests::BadProperty>("Bad").property("a`b", &mapper_tests::BadProperty::x); }
};
} // namespace galactus

namespace mapper_tests {

template <class F> bool refused(F action) {
  try {
    action();
    return false;
  } catch (const MappingError &) {
    return true;
  }
}

inline std::vector<std::int64_t> ids(const std::vector<Person> &people) {
  std::vector<std::int64_t> out;
  for (const auto &person : people)
    out.push_back(person.id);
  return out;
}

inline void offline() {
  Person ann{1, "Ann", std::string("ann@example.com"), std::nullopt};
  Map properties = properties_of(ann);
  check(properties.size() == 4 && properties.at("id").as<std::int64_t>() == 1 &&
            properties.at("name").as<std::string>() == "Ann" &&
            properties.at("email_address").as<std::string>() == "ann@example.com" &&
            mapping::is_null(properties.at("age")),
        "mapper: properties use stored names");
  Node node(7, List{"Person"}, Map{{"id", 1}, {"name", "Ann"}, {"email_address", "a@b"}, {"extra", true}});
  check(from_node<Person>(node) == Person{1, "Ann", std::string("a@b"), std::nullopt}, "mapper: a node becomes an object");
  check(from_node<Person>(Node(7, List{}, Map{{"id", 2}})) == Person{2, "", std::nullopt, std::nullopt},
        "mapper: a node that lacks properties");
  check(refused([] { from_node<Person>(Node(7, List{}, Map{{"id", "one"}})); }), "mapper: a value of the wrong kind");
  Note note = from_node<Note>(Node(1, List{}, Map{{"stars", 5}, {"tags", List{"a", "b"}}}));
  check(note.stars == 5 && note.tags == std::vector<std::string>{"a", "b"}, "mapper: widths and lists");
  check(refused([] { from_node<Note>(Node(1, List{}, Map{{"stars", std::int64_t(1) << 40}})); }),
        "mapper: a number too large for its field");
  check(refused([] { properties_of(BadLabel{}); }), "mapper: a label with a backtick");
  check(refused([] { properties_of(BadProperty{}); }), "mapper: a property with a backtick");
  check(refused([&] { from_node<WorksAt>(node); }), "mapper: a relationship type from a node");
  check(refused([] { relationship<WorksAt>("X").key("since", &WorksAt::since); }), "mapper: a key on a relationship");
  std::cout << "C++ mapper checks passed\n";
}

inline void live(Driver &driver) {
  driver.execute_query("MATCH (n) WHERE n:Person OR n:Company OR n:Note DETACH DELETE n");
  Mapper mapper(driver);
  using Ids = std::vector<std::int64_t>;
  // 2. create, find, and the stored property name
  Person ann{1, "Ann", std::string("ann@example.com"), std::nullopt};
  check(mapper.create(ann) == ann, "mapper: create returns the object as stored");
  check(mapper.find<Person>(1) == std::optional<Person>(ann), "mapper: find returns an equal object");
  auto raw = driver.execute_query("MATCH (p:Person {id: 1}) RETURN p.email_address AS e, p.email AS none").records.at(0);
  check(raw.at("e").as<std::string>() == "ann@example.com" && mapping::is_null(raw.at("none")),
        "mapper: the stored property name");
  // 3. save changes, and creates
  ann.age = 41;
  check(mapper.save(ann).age == std::optional<std::int64_t>(41), "mapper: save changes");
  check(mapper.save(Person{2, "Bob", std::nullopt, std::nullopt}) == Person{2, "Bob", std::nullopt, std::nullopt},
        "mapper: save creates");
  // 4. a missing key
  check(!mapper.find<Person>(99), "mapper: a missing key");
  // 5. find all
  mapper.save(Person{3, "Bob", std::nullopt, 41});
  check(ids(mapper.find_all<Person>()) == Ids{1, 2, 3}, "mapper: find all");
  check(ids(mapper.find_all<Person>(Map{{"name", "Bob"}})) == Ids{2, 3}, "mapper: find all with a filter");
  check(ids(mapper.find_all<Person>(Map{{"name", "Bob"}, {"age", 41}})) == Ids{3}, "mapper: find all with two filters");
  check(ids(mapper.find_all<Person>({}, 2)) == Ids{1, 2}, "mapper: find all with a limit");
  check(refused([&] { mapper.find_all<Person>(Map{{"height", 1}}); }), "mapper: a filter on a property the type lacks");
  // 6. relate and related
  Company acme = mapper.save(Company{"Acme"});
  check(mapper.relate(ann, WorksAt{2020, "engineer"}, acme) == WorksAt{2020, "engineer"}, "mapper: relate");
  mapper.relate(ann, WorksAt{2021, "lead"}, acme);
  auto jobs = mapper.related<WorksAt, Company>(ann);
  check(jobs.size() == 1 && jobs[0].first == WorksAt{2021, "lead"} && jobs[0].second == Company{"Acme"},
        "mapper: related");
  check(refused([&] { mapper.relate(Person{77, "Nobody", std::nullopt, std::nullopt}, WorksAt{1, "x"}, acme); }),
        "mapper: relating a person who is not stored");
  // 7. a hand-written statement
  check(ids(mapper.query<Person>("MATCH (p:Person) WHERE p.age >= $age RETURN p ORDER BY p.id", Map{{"age", 41}})) ==
            Ids{1, 3},
        "mapper: query");
  check(mapper.query<Company>("RETURN {name: $n} AS c", Map{{"n", "Made up"}}) == std::vector<Company>{Company{"Made up"}},
        "mapper: query maps a map");
  // 8. delete
  check(mapper.remove<Person>(3) && !mapper.remove<Person>(3), "mapper: delete");
  // 9. no key
  Note note{"hello", 3, {"first"}};
  check(mapper.create(note) == note, "mapper: create with no key");
  check(refused([&] { mapper.save(note); }), "mapper: saving a type with no key");
  check(refused([&] { mapper.find<Note>("x"); }), "mapper: finding a type with no key");
  check(refused([&] { mapper.find<Person>(nullptr); }), "mapper: finding by no key");
  // the operations join an open transaction
  driver.begin();
  mapper.save(Person{50, "Temporary", std::nullopt, std::nullopt});
  driver.rollback();
  check(!mapper.find<Person>(50), "mapper: a rolled-back save");
  std::cout << "C++ mapper live test passed\n";
}

} // namespace mapper_tests
