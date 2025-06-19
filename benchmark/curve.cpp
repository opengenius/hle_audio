#include <iostream>
#include <cmath>
#include <chrono>
#include <array>
#include <vector>
#include <cassert>
#include <algorithm>
#include <utility> // for std::forward
#include <span>
#include <numbers>

struct Coeffs {
    double a, b, c, d;
};

struct Vec2 {
    double x, y;
};

void bezier_to_power_2d(Vec2 p1, Vec2 p2, Coeffs& coeffs_x, Coeffs& coeffs_y) {
    coeffs_x.a = 3 * p1.x - 3 * p2.x + 1.0;
    coeffs_x.b = -6 * p1.x + 3 * p2.x;
    coeffs_x.c = 3 * p1.x;
    coeffs_x.d = 0.0;

    coeffs_y.a = 3 * p1.y - 3 * p2.y + 1.0;
    coeffs_y.b = -6 * p1.y + 3 * p2.y;
    coeffs_y.c = 3 * p1.y;
    coeffs_y.d = 0.0;
}

double evaluate_polynomial(double t, const Coeffs& c) {
    return ((c.a * t + c.b) * t + c.c) * t + c.d;
}

double evaluate_polynomial_derivative(double t, const Coeffs& c) {
    return (3 * c.a * t + 2 * c.b) * t + c.c;
}

double solve_cubic_equation(const Coeffs& coeffs, double x_value) {
    double a = coeffs.a;
    double b = coeffs.b;
    double c = coeffs.c;
    double d = coeffs.d - x_value;  // Adjust constant term

    if (std::abs(a) < 1e-8) {
        throw std::runtime_error("Coefficient 'a' is zero — not a cubic equation.");
    }

    double a2 = b / a;
    double a1 = c / a;
    double a0 = d / a;

    double q = (3.0 * a1 - a2 * a2) / 9.0;
    double r = (9.0 * a2 * a1 - 27.0 * a0 - 2.0 * std::pow(a2, 3)) / 54.0;
    double D = std::pow(q, 3) + std::pow(r, 2);

    std::array<double, 3> roots{};
    int root_count = 0;

    if (D >= 0.0) {
        double sqrt_D = std::sqrt(D);
        double S = std::cbrt(r + sqrt_D);
        double T = std::cbrt(r - sqrt_D);
        double root = -a2 / 3.0 + (S + T);
        roots[root_count++] = root;
    } else {
        double sqrt_neg_q = std::sqrt(-q);
        double theta = std::acos(r / std::pow(sqrt_neg_q, 3));
        for (int k = 0; k < 3; ++k) {
            double angle = (theta + 2.0 * std::numbers::pi * k) / 3.0;
            double root = 2.0 * sqrt_neg_q * std::cos(angle) - a2 / 3.0;
            roots[root_count++] = root;
        }
    }

    // Return first root in [0, 1]
    for (int i = 0; i < root_count; ++i) {
        if (roots[i] >= 0.0 && roots[i] <= 1.0) {
            return roots[i];
        }
    }

    throw std::runtime_error("No real root in [0, 1] found.");
}

double find_t_for_x_newton(double x, const Coeffs& coeffs_x, double initial_t = 0.5,
                           double tolerance = 1e-6, int max_iter = 100) {
    double t = initial_t;
    for (int i = 0; i < max_iter; ++i) {
        double f = evaluate_polynomial(t, coeffs_x) - x;
        double df = evaluate_polynomial_derivative(t, coeffs_x);
        if (std::abs(df) < 1e-10) break;
        double t_next = t - f / df;
        if (std::abs(t_next - t) < tolerance)
            return std::clamp(t_next, 0.0, 1.0);
        t = std::clamp(t_next, 0.0, 1.0);
    }
    return t; // fallback
}

double find_y_for_x(double x, const Coeffs& coeffs_x, const Coeffs& coeffs_y, double& t_guess) {
    double t = find_t_for_x_newton(x, coeffs_x, t_guess);
    t_guess = t;
    return evaluate_polynomial(t, coeffs_y);
}

void find_y_for_x_range(std::span<std::pair<double, double>> results, const Coeffs& coeffs_x, const Coeffs& coeffs_y, double step = 0.01) {
    double t_guess = 0.0;

    for (size_t i = 0; i < results.size(); ++i) {
        double x = step * static_cast<double>(i);

        double y = find_y_for_x(x, coeffs_x, coeffs_y, t_guess);
        results[i] = {x, y};
    }
}

void find_y_for_x_range_cubic(std::span<std::pair<double, double>> results, const Coeffs& coeffs_x, const Coeffs& coeffs_y, double step = 0.01) {
    double t_guess = 0.0;

    for (size_t i = 0; i < results.size(); ++i) {
        double x = step * static_cast<double>(i);

        auto t = solve_cubic_equation(coeffs_x, x);
        double y = evaluate_polynomial(t, coeffs_y);
        results[i] = {x, y};
    }
}


void find_y_for_x_range_linear_stub(std::span<std::pair<double, double>> results, double step = 0.01) {
    const double y_from = 0.1;
    const double y_to = 0.9;
    for (size_t i = 0; i < results.size(); ++i) {
        double x = step * static_cast<double>(i);

        //double y = y_from * (1 - x) + y_to * x;
        double y = exp(-x);
        results[i] = {x, y};
    }
}

template <typename Func, typename... Args>
double benchmark(Func&& func, int iterations, Args&&... args) {
    using namespace std::chrono;

    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        std::forward<Func>(func)(std::forward<Args>(args)...);
    }
    auto end = high_resolution_clock::now();

    double total_us = duration_cast<duration<double, std::micro>>(end - start).count();
    return total_us / iterations;
}

int main() {
    Vec2 p1 = {1.0, 0.1};
    Vec2 p2 = {0.0, 0.9};

    Coeffs coeffs_x, coeffs_y;
    bezier_to_power_2d(p1, p2, coeffs_x, coeffs_y);

    // Benchmark single evaluation
    const int N = 1000;
    double total_time = 0.0;
    double t_guess = 0.3;
    double result_y = 0.0;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        result_y = find_y_for_x(0.3, coeffs_x, coeffs_y, t_guess);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double us = std::chrono::duration<double, std::micro>(end - start).count() / N;

    std::cout << "Result y for x=0.3: " << result_y << "\n";
    std::cout << "Average time per call: " << us << " us\n";

    const double step = 0.001;
    std::vector<std::pair<double, double>> results(1 / step + 1);
    
    double range_time_us = benchmark(find_y_for_x_range, N, results, coeffs_x, coeffs_y, step);
    double range_cubic_us = benchmark(find_y_for_x_range_cubic, N, results, coeffs_x, coeffs_y, step);
    double range_stub_time_us = benchmark(find_y_for_x_range_linear_stub, N, results, step);

    std::cout << "find_y_for_x_range took: " << range_time_us << " us\n";
    std::cout << "find_y_for_x_range_cubic took: " << range_cubic_us << " us\n";
    std::cout << "find_y_for_x_range_linear_stub took: " << range_stub_time_us << " us\n";

    return 0;
}
