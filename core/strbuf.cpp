#include "base.h"

namespace tsb {

StrBuf& StrBuf::c(char ch) {
    if (len + 1 < cap) {
        buf[len++] = ch;
        buf[len] = 0;
    }
    return *this;
}

StrBuf& StrBuf::s(const char* str) {
    if (!str) str = "(null)";
    while (*str) c(*str++);
    return *this;
}

StrBuf& StrBuf::u(u64 v) {
    char tmp[24];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    while (n) c(tmp[--n]);
    return *this;
}

StrBuf& StrBuf::i(i64 v) {
    if (v < 0) {
        c('-');
        return u((u64)(-(v + 1)) + 1);
    }
    return u((u64)v);
}

StrBuf& StrBuf::hex64(u64 v) {
    static const char* digits = "0123456789abcdef";
    for (int sh = 60; sh >= 0; sh -= 4) c(digits[(v >> sh) & 15]);
    return *this;
}

StrBuf& StrBuf::f(double v, int decimals) {
    if (v != v) return s("0");  // NaN never reaches JSON
    if (v < 0) {
        c('-');
        v = -v;
    }
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;
    double scale = 1.0;
    for (int k = 0; k < decimals; ++k) scale *= 10.0;
    if (v > 9.0e15 / scale) v = 9.0e15 / scale;
    u64 fixed = (u64)(v * scale + 0.5);
    u64 scaleInt = (u64)scale;
    u(fixed / scaleInt);
    if (decimals > 0) {
        c('.');
        u64 frac = fixed % scaleInt;
        char tmp[8];
        for (int k = decimals - 1; k >= 0; --k) {
            tmp[k] = (char)('0' + (frac % 10));
            frac /= 10;
        }
        for (int k = 0; k < decimals; ++k) c(tmp[k]);
    }
    return *this;
}

StrBuf& StrBuf::json_str(const char* str) {
    c('"');
    if (str) {
        for (; *str; ++str) {
            unsigned char ch = (unsigned char)*str;
            if (ch == '"' || ch == '\\') {
                c('\\');
                c((char)ch);
            } else if (ch == '\n') {
                s("\\n");
            } else if (ch < 0x20) {
                c(' ');
            } else {
                c((char)ch);
            }
        }
    }
    c('"');
    return *this;
}

}  // namespace tsb
