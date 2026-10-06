#include "json.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace md::json {

const Array& Value::asArray() const {
    static const Array empty;
    return isArray() ? std::get<Array>(v_) : empty;
}

const Object& Value::asObject() const {
    static const Object empty;
    return isObject() ? std::get<Object>(v_) : empty;
}

const Value* Value::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (auto& [k, v] : std::get<Object>(v_))
        if (k == key) return &v;
    return nullptr;
}

Value& Value::set(std::string key, Value v) {
    if (!isObject()) v_ = Object{};
    auto& o = std::get<Object>(v_);
    for (auto& [k, existing] : o)
        if (k == key) { existing = std::move(v); return existing; }
    o.emplace_back(std::move(key), std::move(v));
    return o.back().second;
}

Value& Value::push(Value v) {
    if (!isArray()) v_ = Array{};
    auto& a = std::get<Array>(v_);
    a.push_back(std::move(v));
    return a.back();
}

namespace {

constexpr int kMaxDepth = 256;

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    std::optional<Value> run(std::string* error) {
        auto v = value(0);
        skipWs();
        if (v && pos_ != s_.size()) fail("contenu après la valeur");
        if (!error_.empty()) {
            if (error) *error = error_ + " (position " + std::to_string(pos_) + ")";
            return std::nullopt;
        }
        return v;
    }

private:
    std::string_view s_;
    size_t pos_ = 0;
    std::string error_;

    std::optional<Value> fail(const char* msg) {
        if (error_.empty()) error_ = msg;
        return std::nullopt;
    }
    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }
    bool consume(std::string_view lit) {
        if (s_.substr(pos_, lit.size()) == lit) { pos_ += lit.size(); return true; }
        return false;
    }

    std::optional<Value> value(int depth) {
        if (depth > kMaxDepth) return fail("imbrication trop profonde");
        skipWs();
        if (pos_ >= s_.size()) return fail("fin inattendue");
        char c = s_[pos_];
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') { auto str = string(); if (!str) return std::nullopt; return Value(std::move(*str)); }
        if (consume("true")) return Value(true);
        if (consume("false")) return Value(false);
        if (consume("null")) return Value(nullptr);
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        return fail("caractère inattendu");
    }

    std::optional<Value> number() {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && (std::isdigit((unsigned char)s_[pos_]) || s_[pos_] == '.' || s_[pos_] == 'e' ||
                                    s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-'))
            ++pos_;
        double d = 0;
        auto r = std::from_chars(s_.data() + start, s_.data() + pos_, d);
        if (r.ec != std::errc() || r.ptr != s_.data() + pos_) return fail("nombre invalide");
        return Value(d);
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& out) {
        if (pos_ + 4 > s_.size()) return false;
        auto r = std::from_chars(s_.data() + pos_, s_.data() + pos_ + 4, out, 16);
        if (r.ec != std::errc() || r.ptr != s_.data() + pos_ + 4) return false;
        pos_ += 4;
        return true;
    }

    std::optional<std::string> string() {
        ++pos_;  // guillemet ouvrant
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return out;
            if ((unsigned char)c < 0x20) { fail("caractère de contrôle dans une chaîne"); return std::nullopt; }
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) break;
            char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) { fail("échappement \\u invalide"); return std::nullopt; }
                    if (cp >= 0xD800 && cp <= 0xDBFF && consume("\\u")) {
                        unsigned lo = 0;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) { fail("paire de substitution invalide"); return std::nullopt; }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("échappement inconnu"); return std::nullopt;
            }
        }
        fail("chaîne non terminée");
        return std::nullopt;
    }

    std::optional<Value> array(int depth) {
        ++pos_;
        Array a;
        skipWs();
        if (consume("]")) return Value(std::move(a));
        for (;;) {
            auto v = value(depth + 1);
            if (!v) return std::nullopt;
            a.push_back(std::move(*v));
            skipWs();
            if (consume(",")) continue;
            if (consume("]")) return Value(std::move(a));
            return fail("',' ou ']' attendu");
        }
    }

    std::optional<Value> object(int depth) {
        ++pos_;
        Value o = Object{};
        skipWs();
        if (consume("}")) return o;
        for (;;) {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') return fail("clé attendue");
            auto key = string();
            if (!key) return std::nullopt;
            skipWs();
            if (!consume(":")) return fail("':' attendu");
            auto v = value(depth + 1);
            if (!v) return std::nullopt;
            o.set(std::move(*key), std::move(*v));
            skipWs();
            if (consume(",")) continue;
            if (consume("}")) return o;
            return fail("',' ou '}' attendu");
        }
    }
};

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += char(c);
        }
    }
    out += '"';
}

void write(std::string& out, const Value& v, bool pretty, int indent) {
    auto newline = [&](int level) {
        if (!pretty) return;
        out += '\n';
        out.append(size_t(level) * 2, ' ');
    };
    if (v.isNull()) out += "null";
    else if (v.isBool()) out += v.asBool(false) ? "true" : "false";
    else if (v.isNumber()) {
        double d = v.asNumber(0);
        if (!std::isfinite(d)) out += "null";
        else if (d == std::floor(d) && std::fabs(d) < 1e15) out += std::to_string((long long)d);
        else { char buf[32]; std::snprintf(buf, sizeof buf, "%.17g", d); out += buf; }
    } else if (v.isString()) escape(out, v.asString(""));
    else if (v.isArray()) {
        auto& a = v.asArray();
        out += '[';
        for (size_t i = 0; i < a.size(); ++i) {
            if (i) out += ',';
            newline(indent + 1);
            write(out, a[i], pretty, indent + 1);
        }
        if (!a.empty()) newline(indent);
        out += ']';
    } else {
        auto& o = v.asObject();
        out += '{';
        for (size_t i = 0; i < o.size(); ++i) {
            if (i) out += ',';
            newline(indent + 1);
            escape(out, o[i].first);
            out += pretty ? ": " : ":";
            write(out, o[i].second, pretty, indent + 1);
        }
        if (!o.empty()) newline(indent);
        out += '}';
    }
}

} // namespace

std::optional<Value> parse(std::string_view text, std::string* error) {
    return Parser(text).run(error);
}

std::string serialize(const Value& v, bool pretty) {
    std::string out;
    write(out, v, pretty, 0);
    if (pretty) out += '\n';
    return out;
}

} // namespace md::json
