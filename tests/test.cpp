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
    result.push_back(s);
  }
  return result;
}
int main() {
  try {
    using Clock = std::chrono::system_clock;
    for (auto time : {Clock::time_point::min(), Clock::time_point::max(),
                      Clock::time_point(Clock::duration(-123456789))}) {
      auto value = decode(encode(Value(time)));
      check(to_system_time(value.as<DateTime>()) == time,
            "Native clock round-trip");
    }
    for (const auto &line : lines("values.tsv")) {
      auto p = line.find('\t');
      auto b = unhex(line.substr(p + 1));
      check(encode(decode(b)) == b, line.substr(0, p));
    }
    for (const auto &h : lines("malformed.tsv")) {
      bool failed = false;
      try {
        decode(unhex(h));
      } catch (...) {
        failed = true;
      }
      check(failed, "Malformed input");
    }
    const char *uri = std::getenv("GDB_TEST_URI");
    if (!uri) {
      std::cout << "C++ codec passed; live skipped\n";
      return 0;
    }
    Driver d(uri, "gdb", std::getenv("GDB_TEST_PASSWORD"));
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
    check(d.execute_query("MATCH (n:DriverCpp) RETURN count(n) AS n")
                  .records[0]
                  .at("n")
                  .as<std::int64_t>() == 0,
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
    bool failed = false;
    try {
      d.execute_query("INVALID QUERY");
    } catch (const DatabaseError &) {
      failed = true;
    }
    check(failed, "Database error");
    failed = false;
    try {
      Driver bad(uri, "gdb", "wrong-password");
    } catch (const DatabaseError &) {
      failed = true;
    }
    check(failed, "Auth failure");
    std::cout << "C++ codec and live tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
