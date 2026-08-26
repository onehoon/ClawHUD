#include "HudPresentationLifecycle.h"

#include <iostream>

int main()
{
    bool ok = true;
    const auto expect = [&](bool condition, const char* name)
    {
        if (!condition) std::cerr << "FAILED: " << name << '\n';
        ok &= condition;
    };
    const auto hidden = clawhud::BuildHudPresentationRefreshPlan(false, true);
    expect(!hidden.recreate && !hidden.restoreVisibility, "no pending change");
    const auto visible = clawhud::BuildHudPresentationRefreshPlan(true, true);
    expect(visible.recreate && visible.restoreVisibility, "visible recreation restores visibility");
    const auto wasHidden = clawhud::BuildHudPresentationRefreshPlan(true, false);
    expect(wasHidden.recreate && !wasHidden.restoreVisibility, "hidden recreation stays hidden");
    return ok ? 0 : 1;
}
