#pragma once

#include <cmath>

namespace signals::core {

// std::complex is only specified for float, double and long double, so 256-bit roots need their own type.
template <class T>
struct Complex {
    T re{0};
    T im{0};
};

template <class T>
Complex<T> operator+(const Complex<T>& a, const Complex<T>& b) { return {a.re + b.re, a.im + b.im}; }
template <class T>
Complex<T> operator-(const Complex<T>& a, const Complex<T>& b) { return {a.re - b.re, a.im - b.im}; }
template <class T>
Complex<T> operator*(const Complex<T>& a, const Complex<T>& b) {
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}
template <class T>
Complex<T> operator/(const Complex<T>& a, const Complex<T>& b) {
    const T d = b.re * b.re + b.im * b.im;
    return {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
}
template <class T>
Complex<T> conj(const Complex<T>& a) { return {a.re, -a.im}; }
template <class T>
T modulus(const Complex<T>& a) {
    using std::sqrt;
    return sqrt(a.re * a.re + a.im * a.im);
}
template <class T>
Complex<T> complex_sqrt(const Complex<T>& a) {
    using std::sqrt;
    const T m = modulus(a);
    const T re = sqrt((m + a.re) / 2);
    T im = sqrt((m - a.re) / 2);
    if (a.im < 0) im = -im;
    return {re, im};
}

}  // namespace signals::core
