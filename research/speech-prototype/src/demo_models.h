#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

// Pure logic shared with the host tests; it has no Arduino dependencies.
inline const char *evaluateExpression(const char *input, char *result, size_t capacity) {
    char *next;
    double left = strtod(input, &next);
    if (next == input || !*next || !strchr("+-*/", *next)) return "Invalid expression";
    char op = *next++;
    const char *rightStart = next;
    double right = strtod(rightStart, &next);
    if (next == rightStart || *next != '\0') return "Invalid expression";
    if (op == '/' && right == 0) return "Cannot divide by zero";
    double value = op == '+' ? left + right : op == '-' ? left - right : op == '*' ? left * right : left / right;
    if (!std::isfinite(value)) return "Result out of range";
    snprintf(result, capacity, "%.7g", value);
    return nullptr;
}

struct DemoTimer {
    bool running = false;
    uint32_t deadline = 0;
    bool start(uint32_t now, const char *value, uint32_t unitSeconds) {
        char *end;
        double amount = strtod(value, &end);
        double milliseconds = amount * unitSeconds * 1000.0;
        // Bound duration to 24 hours so signed wraparound comparisons remain safe.
        if (end == value || *end || !std::isfinite(milliseconds) || milliseconds < 1 || milliseconds > 86400000)
            return false;
        deadline = now + static_cast<uint32_t>(milliseconds);
        running = true;
        return true;
    }
    void stop() { running = false; }
    bool elapsed(uint32_t now) {
        if (running && static_cast<int32_t>(now - deadline) >= 0) { running = false; return true; }
        return false;
    }
    uint32_t remaining(uint32_t now) const {
        if (!running || static_cast<int32_t>(now - deadline) >= 0) return 0;
        return (deadline - now + 999) / 1000;
    }
};
