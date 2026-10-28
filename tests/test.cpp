#include <cstdlib>
#include <fstream>
#include <galactus/driver.hpp>
#include <iostream>
using namespace galactus;
void check(bool b, const std::string &s) {
  if (!b)
    throw std::runtime_error(s);
}
Bytes unhex(const std::string &h) {
  Bytes b;
  for (size_t i = 0; i < h.size(); i += 2)
    b.push_back(
        static_cast<std::uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
  return b;
}
std::vector<std::string> lines(const std::string &name) {
  std::ifstream f("tests/fixtures/" + name);
  if (!f)
    throw std::runtime_error("Missing fixture " + name);
  std::vector<std::string> result;
  std::string s;
  while (std::getline(f, s)) {
    if (!s.empty() && s.back() == '\r')
      s.pop_back();
    if (!s.empty())
      result.push_back(s);
  }
  return result;
}
std::pair<std::string, std::string> split_tab(const std::string &line) {
  auto p = line.find('\t');
  return {line.substr(0, p), line.substr(p + 1)};
}
template <class E, class F> E expect_throw(F &&f, const std::string &what) {
  try {
    f();
  } catch (const E &e) {
    return e;
  }
  throw std::runtime_error("Expected an error: " + what);
}
std::int64_t single_int(Driver &d, const std::string &query) {
  return d.execute_query(query).records.at(0).begin()->second.as<std::int64_t>();
}

void codec_tests() {
  using Clock = std::chrono::system_clock;
  for (auto time : {Clock::time_point::min(), Clock::time_point::max(),
                    Clock::time_point(Clock::duration(-123456789))}) {
    auto value = decode(encode(Value(time)));
    check(to_system_time(value.as<DateTime>()) == time,
          "Native clock round-trip");
  }
  int count = 0;
  for (const auto &line : lines("values.tsv")) {
    auto [name, hex] = split_tab(line);
    auto b = unhex(hex);
    check(encode(decode(b)) == b, "values.tsv default mode: " + name);
    check(encode(decode(b), Protocol::Bolt44) == b, "values.tsv 4.4: " + name);
    ++count;
  }
  std::cout << "values.tsv: " << count << " rows round-trip in Bolt 4.4 mode\n";
  count = 0;
  for (const auto &line : lines("bolt5.tsv")) {
    auto [name, hex] = split_tab(line);
    auto b = unhex(hex);
    check(encode(decode(b), Protocol::Bolt5) == b, "bolt5.tsv: " + name);
    ++count;
  }
  std::cout << "bolt5.tsv: " << count << " rows round-trip in Bolt 5 mode\n";
  // Graph element ids: Bolt 5 carries them; on 4.4 they are the id's text.
  {
    std::map<std::string, Bytes> v4, v5;
    for (const auto &line : lines("values.tsv")) {
      auto [name, hex] = split_tab(line);
      v4[name] = unhex(hex);
    }
    for (const auto &line : lines("bolt5.tsv")) {
      auto [name, hex] = split_tab(line);
      v5[name] = unhex(hex);
    }
    auto n4v = decode(v4.at("node"));
    const auto &n4 = n4v.as<Node>();
    check(n4.element_id == "1", "4.4 node element_id from id");
    auto r4v = decode(v4.at("relationship"));
    const auto &r4 = r4v.as<Relationship>();
    check(r4.element_id == "2" && r4.start_element_id == "1" &&
              r4.end_element_id == "3",
          "4.4 relationship element ids from ids");
    auto r5v = decode(v5.at("relationship"));
    const auto &r5 = r5v.as<Relationship>();
    check(r5.element_id == "2" && r5.start_element_id == "1" &&
              r5.end_element_id == "3",
          "Bolt 5 relationship element ids");
    check(decode(v5.at("unbound_relationship"))
                  .as<UnboundRelationship>()
                  .element_id == "2",
          "Bolt 5 unbound relationship element id");
    // The encoder writes the form of the protocol it is given.
    check(encode(decode(v4.at("node")), Protocol::Bolt5) == v5.at("node"),
          "4.4 node encodes in Bolt 5 form");
    check(encode(decode(v5.at("node")), Protocol::Bolt44) == v4.at("node"),
          "Bolt 5 node encodes in 4.4 form");
    check(decode(v5.at("named_zone_utc")).as<ZonedDateTime>().basis ==
                  ZonedDateTime::Basis::Utc &&
              decode(v5.at("named_zone_local")).as<ZonedDateTime>().basis ==
                  ZonedDateTime::Basis::Local,
          "Named-zone basis follows the tag");
    check(encode(decode(v5.at("named_zone_utc")), Protocol::Bolt44) ==
              v5.at("named_zone_utc"),
          "Named-zone keeps its own basis on 4.4");
  }
  count = 0;
  for (const auto &line : lines("datetime_pairs.tsv")) {
    auto [f_hex, i_hex] = split_tab(line);
    auto f = unhex(f_hex), i = unhex(i_hex);
    auto fv = decode(f), iv = decode(i);
    const auto &from_f = fv.as<DateTime>();
    const auto &from_i = iv.as<DateTime>();
    check(from_f == from_i, "datetime pair decodes equal: " + line);
    check(encode(DateTime(from_f), Protocol::Bolt44) == f &&
              encode(DateTime(from_i), Protocol::Bolt44) == f,
          "datetime pair encodes F on 4.4: " + line);
    check(encode(DateTime(from_f), Protocol::Bolt5) == i &&
              encode(DateTime(from_i), Protocol::Bolt5) == i,
          "datetime pair encodes I on Bolt 5: " + line);
    ++count;
  }
  std::cout << "datetime_pairs.tsv: " << count << " pairs decode equal and "
            << "encode per protocol\n";
  count = 0;
  for (const auto &h : lines("malformed.tsv")) {
    bool failed = false;
    try {
      decode(unhex(h));
    } catch (...) {
      failed = true;
    }
    check(failed, "Malformed input");
    ++count;
  }
  std::cout << "malformed.tsv: " << count << " inputs rejected\n";
  // Error classification without a server.
  DatabaseError e(Map{{"code", codes::constraint_validation_failed},
                      {"message", "duplicate"}});
  check(e.classification == "ClientError" && e.category == "Schema" &&
            e.title == "ConstraintValidationFailed" && e.is_client_error() &&
            !e.is_retryable(),
        "Error classification");
  DatabaseError t(Map{{"code", codes::snapshot_too_old}});
  check(t.is_transient() && t.is_retryable(), "Transient classification");
  DatabaseError odd(Map{{"code", "Odd.Code"}});
  check(odd.classification.empty() && odd.title.empty() &&
            !odd.is_client_error(),
        "Short codes have no classification");
  // URI and option validation, before any network activity.
  for (auto uri : {"bolt+ssc://localhost:7687", "neo4j://localhost:7687",
                   "neo4j+s://localhost:7687", "http://localhost:7687",
                   "bolt://user:pw@localhost:7687", "bolt://localhost?x=1"})
    expect_throw<ConfigurationError>([&] { Driver d(uri, "gdb", "x"); }, uri);
  DriverOptions with_ca;
  with_ca.trusted_certificates = "tests/fixtures/values.tsv";
  auto misuse = expect_throw<ConfigurationError>(
      [&] { Driver d("bolt://localhost:7687", "gdb", "x", with_ca); },
      "trusted_certificates on bolt://");
  check(std::string(misuse.what()).find("trusted_certificates") !=
            std::string::npos,
        "Option misuse names the option");
#ifndef GALACTUS_WITH_TLS
  auto rejected = expect_throw<ConfigurationError>(
      [&] { Driver d("bolt+s://localhost:7687", "gdb", "x"); },
      "bolt+s:// without TLS support");
  check(std::string(rejected.what()).find("GALACTUS_WITH_TLS") !=
            std::string::npos,
        "bolt+s:// rejection names GALACTUS_WITH_TLS");
  std::cout << "bolt+s:// rejected without GALACTUS_WITH_TLS\n";
#endif
  auto agent = bolt_agent();
  check(agent.at("product").as<std::string>() ==
                "galactus-db-cpp-driver/0.2.0" &&
            agent.at("language").as<std::string>().rfind("C++/", 0) == 0,
        "bolt_agent");
}

void live_tests(const std::string &uri, const std::string &password) {
  const char *expect = std::getenv("GDB_TEST_EXPECT_BOLT");
  const char *ca = std::getenv("GDB_TEST_CA_FILE");
  DriverOptions options;
  if (ca && *ca)
    options.trusted_certificates = ca;
  Driver d(uri, "gdb", password, options);
  if (expect && *expect)
    check(d.protocol_version() == expect,
          "Agreed protocol " + d.protocol_version() + ", expected " + expect);
  check(!d.server_agent().empty(), "Server agent");
  check(!d.connection_id().empty(), "Connection id");
  std::cout << "Connected with Bolt " << d.protocol_version() << " to "
            << d.server_agent() << " (" << d.connection_id() << ")\n";

  Map values{{"n", INT64_MAX},
             {"b", Bytes{0, 255}},
             {"s", std::string(70000, 'x')},
             {"t", DateTime(-315615477, 456789123, 19800)}};
  auto got = d.execute_query("RETURN $v AS v", {{"v", values}})
                 .records[0]
                 .at("v")
                 .as<Map>();
  check(encode(got) == encode(values), "Native mapping");
  for (const auto &line : lines("spatial.tsv")) {
    auto tab = line.find('\t');
    auto s =
        d.execute_query(
             "RETURN spatial.fromWKT($wkt,{domain:$domain}) AS shape",
             {{"domain", line.substr(0, tab)}, {"wkt", line.substr(tab + 1)}})
            .records[0]
            .at("shape");
    s.as<Spatial>();
    Value nested = List{Map{{"shape", s}}};
    auto r = d.execute_query(
                  "RETURN spatial.fromMap($s) AS shape, $nested AS nested",
                  {{"s", s}, {"nested", nested}})
                 .records[0];
    check(encode(r.at("shape")) == encode(s) &&
              encode(r.at("nested")) == encode(nested),
          line);
  }
  d.begin();
  d.execute_query("CREATE (:DriverCpp {n:1})");
  d.rollback();
  check(single_int(d, "MATCH (n:DriverCpp) RETURN count(n) AS n") == 0,
        "Rollback");
  d.begin();
  d.execute_query("CREATE (:DriverCpp {n:2})");
  d.commit();
  check(d.execute_query("MATCH (n:DriverCpp) RETURN n")
                .records[0]
                .at("n")
                .as<Node>()
                .properties.at("n")
                .as<std::int64_t>() == 2,
        "Commit");

  // Element ids are the decimal text of the integer id.
  auto graph = d.execute_query("CREATE (a:DriverCppGraph)-[r:DRIVER_CPP_REL]->"
                               "(b:DriverCppGraph) RETURN a, r, b")
                   .records.at(0);
  const auto &a = graph.at("a").as<Node>();
  const auto &b = graph.at("b").as<Node>();
  const auto &r = graph.at("r").as<Relationship>();
  check(a.element_id == std::to_string(a.id) &&
            b.element_id == std::to_string(b.id),
        "Node element_id");
  check(r.element_id == std::to_string(r.id) &&
            r.start_element_id == a.element_id &&
            r.end_element_id == b.element_id,
        "Relationship element ids");

  // Offset and named-zone datetimes through RETURN $v.
  DateTime offset(1782907200 + 3600, 123456789, 3600);
  check(d.execute_query("RETURN $v AS v", {{"v", offset}})
                .records[0]
                .at("v")
                .as<DateTime>() == offset,
        "Offset datetime round-trip");
  // The server answers in its own protocol's basis (f on 4.4, i on 5.x), so
  // compare instants. London's offset is known for these dates: 15/01/2026
  // is GMT (+0) and 01/07/2026 is BST (+3600).
  auto instant = [](const ZonedDateTime &z, std::int64_t london_offset) {
    return z.basis == ZonedDateTime::Basis::Utc ? z.seconds
                                                : z.seconds - london_offset;
  };
  struct Case {
    std::int64_t utc, offset;
  };
  for (auto c : {Case{1768478400, 0}, Case{1782907200, 3600}}) {
    for (const auto &sent :
         {ZonedDateTime::utc(c.utc, 5, "Europe/London"),
          ZonedDateTime::local(c.utc + c.offset, 5, "Europe/London")}) {
      auto back = d.execute_query("RETURN $v AS v", {{"v", sent}})
                      .records[0]
                      .at("v")
                      .as<ZonedDateTime>();
      check(instant(back, c.offset) == c.utc && back.nanoseconds == 5 &&
                back.zone_id == "Europe/London",
            "Named-zone datetime round-trip");
    }
  }

  // A parameter that fails validation raises before anything is sent.
  expect_throw<std::invalid_argument>(
      [&] {
        d.execute_query("RETURN $v AS v", {{"v", std::string("\xff")}});
      },
      "invalid UTF-8 parameter");
  expect_throw<std::invalid_argument>(
      [&] { d.execute_query("RETURN $v AS v", {{"v", a}}); },
      "graph entity parameter");
  check(single_int(d, "RETURN 2 AS two") == 2, "Usable after invalid input");

  // A database error leaves the driver usable.
  auto syntax = expect_throw<DatabaseError>(
      [&] { d.execute_query("INVALID QUERY"); }, "INVALID QUERY");
  check(!syntax.code.empty() && !syntax.classification.empty() &&
            syntax.is_client_error() && !syntax.is_retryable(),
        "Classified syntax error: " + syntax.code);
  check(single_int(d, "RETURN 1 AS one") == 1, "Query after failure");

  // A failure inside a transaction closes it; the server rolled it back.
  d.begin();
  d.execute_query("CREATE (:DriverCppTx {n:1})");
  expect_throw<DatabaseError>([&] { d.execute_query("INVALID QUERY"); },
                              "failure in transaction");
  check(!d.in_transaction(), "Transaction closed after failure");
  expect_throw<std::logic_error>([&] { d.rollback(); },
                                 "rollback after failure");
  check(single_int(d, "MATCH (n:DriverCppTx) RETURN count(n) AS n") == 0,
        "Failed transaction rolled back");

  // Unique constraint violations carry the published code.
  d.execute_query(
      "CREATE CONSTRAINT FOR (n:DriverCppUnique) REQUIRE n.key IS UNIQUE");
  d.execute_query("CREATE (:DriverCppUnique {key:1})");
  auto duplicate = expect_throw<DatabaseError>(
      [&] { d.execute_query("CREATE (:DriverCppUnique {key:1})"); },
      "unique constraint");
  check(duplicate.code == codes::constraint_validation_failed &&
            duplicate.title == "ConstraintValidationFailed",
        "Constraint code: " + duplicate.code);
  check(single_int(d, "MATCH (n:DriverCppUnique) RETURN count(n) AS n") == 1,
        "Usable after constraint failure");

  // Decimals arrive as strings with their exact digits.
  check(d.execute_query("RETURN decimal($d) AS d", {{"d", "12.30"}})
                .records[0]
                .at("d")
                .as<std::string>() == "12.30",
        "Decimal as string");

  // Bookmarks from autocommit and COMMIT, when the server sends them.
  d.execute_query("CREATE (:DriverCppBookmark)");
  std::cout << "Last bookmark: "
            << (d.last_bookmark().empty() ? "(none sent)" : d.last_bookmark())
            << "\n";

  expect_throw<DatabaseError>(
      [&] { Driver bad(uri, "gdb", "wrong-password", options); },
      "Auth failure");

#ifdef GALACTUS_WITH_TLS
  if (uri.rfind("bolt+s://", 0) == 0) {
    auto untrusted = expect_throw<TlsError>(
        [&] { Driver plain_trust(uri, "gdb", password); },
        "TLS without the trusted certificate file");
    std::cout << "Without trusted_certificates: " << untrusted.what() << "\n";
    auto port = uri.substr(uri.rfind(':'));
    expect_throw<ConfigurationError>(
        [&] {
          Driver misuse("bolt://localhost" + port, "gdb", password, options);
        },
        "trusted_certificates on bolt://");
  }
#endif
}

int main() {
  try {
    codec_tests();
    const char *uri = std::getenv("GDB_TEST_URI");
    if (!uri) {
      std::cout << "C++ codec passed; live skipped\n";
      return 0;
    }
    const char *password = std::getenv("GDB_TEST_PASSWORD");
    live_tests(uri, password ? password : "");
    std::cout << "C++ codec and live tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
