// JSON minimal : analyse et sérialisation, ordre des clés conservé.
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace md::json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

class Value {
public:
    Value() : v_(nullptr) {}
    Value(std::nullptr_t) : v_(nullptr) {}
    Value(bool b) : v_(b) {}
    Value(double d) : v_(d) {}
    Value(int i) : v_(double(i)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(Array a) : v_(std::move(a)) {}
    Value(Object o) : v_(std::move(o)) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(v_); }
    bool isBool() const { return std::holds_alternative<bool>(v_); }
    bool isNumber() const { return std::holds_alternative<double>(v_); }
    bool isString() const { return std::holds_alternative<std::string>(v_); }
    bool isArray() const { return std::holds_alternative<Array>(v_); }
    bool isObject() const { return std::holds_alternative<Object>(v_); }

    bool asBool(bool def) const { return isBool() ? std::get<bool>(v_) : def; }
    double asNumber(double def) const { return isNumber() ? std::get<double>(v_) : def; }
    std::string asString(std::string def) const { return isString() ? std::get<std::string>(v_) : def; }
    const Array& asArray() const;
    const Object& asObject() const;

    const Value* find(std::string_view key) const;   // nullptr si absent ou pas un objet
    Value& set(std::string key, Value v);             // convertit en objet si besoin
    bool erase(std::string_view key);                 // false si absent ou pas un objet
    Value& push(Value v);                              // convertit en tableau si besoin

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v_;
};

std::optional<Value> parse(std::string_view text, std::string* error = nullptr);
std::string serialize(const Value& v, bool pretty = true);

} // namespace md::json
