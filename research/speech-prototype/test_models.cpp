#include "src/demo_models.h"
#include "src/launcher_return.h"
#include <cassert>

int main() {
    char result[48];
    assert(!evaluateExpression("2+3", result, sizeof result) && !strcmp(result, "5"));
    assert(!evaluateExpression("-9/3", result, sizeof result) && !strcmp(result, "-3"));
    assert(!strcmp(evaluateExpression("1/0", result, sizeof result), "Cannot divide by zero"));
    assert(!strcmp(evaluateExpression("4", result, sizeof result), "Invalid expression"));
    assert(!strcmp(evaluateExpression("4+2x", result, sizeof result), "Invalid expression"));
    DemoTimer timer;
    assert(timer.start(0xfffffff0u, "10", 1));
    assert(timer.remaining(0xfffffff0u) == 10);
    assert(!timer.elapsed(0xfffffff1u));
    assert(timer.remaining(0xfffffff0u + 9000u) == 1);
    assert(timer.elapsed(0xfffffff0u + 10000u));
    assert(!timer.elapsed(0xfffffff0u + 10001u));
    assert(timer.start(100, "0.5", 60));
    assert(timer.remaining(100) == 30);
    timer.stop();
    assert(!timer.elapsed(20000));
    assert(!timer.start(100, "0", 1));
    assert(!timer.start(100, "25", 3600));
    assert(!timer.start(100, "12x", 1));
    assert(launcherReturnPolicy(false, true, false, -1) == LauncherReturn::NoLauncher);
    assert(launcherReturnPolicy(true, false, false, -1) == LauncherReturn::SettingsUnavailable);
    assert(launcherReturnPolicy(true, true, true, -1) == LauncherReturn::Disabled);
    assert(launcherReturnPolicy(true, true, false, 3) == LauncherReturn::Disabled);
    assert(launcherReturnPolicy(true, true, false, -1) == LauncherReturn::Ready);
    assert(launcherReturnPolicy(true, true, false, 0) == LauncherReturn::Ready);
}
