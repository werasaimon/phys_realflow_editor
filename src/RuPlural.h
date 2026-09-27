#pragma once
// A number with its noun in the right Russian form: 1 копия, 2 копии, 5 копий, 11 копий, 21 копия.
// The rule: the last digit 1 (not 11) takes `one`; 2, 3, 4 (not 12, 13, 14) take `few`; the rest `many`.
#include <QString>

inline QString ruPlural(long long n, const char* one, const char* few, const char* many) {
    const long long d = (n < 0 ? -n : n) % 10, h = (n < 0 ? -n : n) % 100;
    const char* word = d == 1 && h != 11 ? one : d >= 2 && d <= 4 && (h < 12 || h > 14) ? few : many;
    return QString("%1 %2").arg(n).arg(QString::fromUtf8(word));
}
