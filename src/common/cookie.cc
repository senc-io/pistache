/*
   Mathieu Stefani, 16 janvier 2016

   Cookie implementation
*/

#include <iterator>
#include <unordered_map>

#include <pistache/cookie.h>
#include <pistache/stream.h>

namespace Pistache {
namespace Http {

namespace {

    StreamCursor::Token matchValue(StreamCursor& cursor) {
        int c;
        if ((c = cursor.current()) != StreamCursor::Eof && c != '=')
            throw std::runtime_error("Invalid cookie");

        if (!cursor.advance(1))
            throw std::runtime_error("Invalid cookie, early eof");

        StreamCursor::Token token(cursor);
        match_until(';', cursor);

        return token;
    }

    template<typename T>
    struct AttributeMatcher;

    template<>
    struct AttributeMatcher<Optional<std::string>> {
        static void match(StreamCursor& cursor, Cookie* obj, Optional<std::string> Cookie::*attr) {
            auto token = matchValue(cursor);
            obj->*attr = Some(std::string(token.rawText(), token.size()));
        }
    };

    template<>
    struct AttributeMatcher<Optional<int>> {
        static void match(StreamCursor& cursor, Cookie* obj, Optional<int> Cookie::*attr) {
            auto token = matchValue(cursor);

            auto strntol = [](const char *str, size_t len) {
                int ret = 0;
                for (size_t i = 0; i < len; ++i) {
                    if (!isdigit(str[i]))
                        throw std::invalid_argument("Invalid conversion");

                    ret *= 10;
                    ret += str[i] - '0';
                };

                return ret;
            };

            obj->*attr = Some(strntol(token.rawText(), token.size()));
        }
    };

    template<>
    struct AttributeMatcher<bool> {
        static void match(StreamCursor&, Cookie* obj, bool Cookie::*attr) {
            obj->*attr = true;
        }
    };

    template<>
    struct AttributeMatcher<Optional<FullDate>> {
        static void match(StreamCursor& cursor, Cookie* obj, Optional<FullDate> Cookie::*attr) {
            auto token = matchValue(cursor);
            obj->*attr = Some(FullDate::fromRaw(token.rawText(), token.size()));
        }
    };

    template<typename T>
    bool match_attribute(const char* name, size_t len, StreamCursor& cursor, Cookie* obj, T Cookie::*attr) {
        if (match_string(name, len, cursor)) {
            AttributeMatcher<T>::match(cursor, obj, attr);
            cursor.advance(1);

            return true;
        }

        return false;
    }

}

Cookie::Cookie(std::string name, std::string value)
    : name(std::move(name))
    , value(std::move(value))
    , secure(false)
    , httpOnly(false)
{
}

Cookie
Cookie::fromRaw(const char* str, size_t len)
{
    RawStreamBuf<> buf(const_cast<char *>(str), len);
    StreamCursor cursor(&buf);

    StreamCursor::Token nameToken(cursor);

    if (!match_until('=', cursor))
        throw std::runtime_error("Invalid cookie, missing value");

    auto name = nameToken.text();

    if (!cursor.advance(1))
        throw std::runtime_error("Invalid cookie, missing value");

    StreamCursor::Token valueToken(cursor);

    match_until(';', cursor);
    auto value = valueToken.text();

    Cookie cookie(std::move(name), std::move(value));
    if (cursor.eof()) {
        return cookie;
    }

    cursor.advance(1);

#define STR(str) str, sizeof(str) - 1

    do {
        skip_whitespaces(cursor);

        if (match_attribute(STR("Path"), cursor, &cookie, &Cookie::path)) ;
        else if (match_attribute(STR("Domain"), cursor, &cookie, &Cookie::domain)) ;
        else if (match_attribute(STR("Secure"), cursor, &cookie, &Cookie::secure)) ;
        else if (match_attribute(STR("HttpOnly"), cursor, &cookie, &Cookie::httpOnly)) ;
        else if (match_attribute(STR("Max-Age"), cursor, &cookie, &Cookie::maxAge)) ;
        else if (match_attribute(STR("Expires"), cursor, &cookie, &Cookie::expires)) ;
        // ext
        else {
            StreamCursor::Token nameToken(cursor);
            match_until('=', cursor);

            auto name = nameToken.text();
            std::string value;
            if (!cursor.eof()) {
                auto token = matchValue(cursor);
                value = token.text();
            }
            cookie.ext.insert(std::make_pair(std::move(name), std::move(value)));

        }

    } while (!cursor.eof());

#undef STR

    return cookie;
}

Cookie
Cookie::fromString(const std::string& str) {
    return Cookie::fromRaw(str.c_str(), str.size());
}

void
Cookie::write(std::ostream& os) const {
    os << name << "=" << value;
    optionally_do(path, [&](const std::string& value) {
       os << "; ";
       os << "Path=" << value;
    });
    optionally_do(domain, [&](const std::string& value) {
        os << "; ";
        os << "Domain=" << value;
    });
    optionally_do(maxAge, [&](int value) {
        os << "; ";
        os << "Max-Age=" << value;
    });
    optionally_do(expires, [&](const FullDate& value) {
        os << "; ";
        os << "Expires=";
        value.write(os);
    });
    if (secure)
        os << "; Secure";
    if (httpOnly)
        os << "; HttpOnly";
    if (!ext.empty()) {
        os << "; ";
        for (auto it = std::begin(ext), end = std::end(ext); it != end; ++it) {
            os << it->first << "=" << it->second;
            if (std::distance(it, end) > 1)
                os << "; ";
        }
    }

}

CookieJar::CookieJar()
{
}

void
CookieJar::add(const Cookie& cookie) {
    cookies.insert(std::make_pair(cookie.name, cookie));
}

Cookie
CookieJar::get(const std::string& name) const {
    auto it = cookies.find(name);
    if (it == std::end(cookies))
        throw std::runtime_error("Could not find requested cookie");

    return it->second;
}

bool
CookieJar::has(const std::string& name) const {
    auto it = cookies.find(name);
    return it != std::end(cookies);
}

} // namespace Http
} // namespace Pistache
