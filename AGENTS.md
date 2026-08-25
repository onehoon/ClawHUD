# ClawHUD coding instructions

Before changing code, read the current `README.md` and every applicable document under `docs/`. These files are the project's architectural and scope references; do not treat them as optional background material.

When the working branch is behind `origin/main`, fetch and inspect the latest `origin/main` README and `docs` files before making architectural decisions. Resolve conflicts in favor of the latest documented project direction unless the task explicitly says otherwise.

Keep changes narrow and preserve the project's constraints:

- Windows 11 x64, MSI Claw, and Intel Arc are the supported product boundary.
- Keep `ClawHUD.VrrPoc` separate from the production application until the Phase 0 hardware GO decision.
- Preserve the non-injected presentation requirement; do not add game injection, Present/DXGI hooks, swapchain interception, or game-process memory access.
- Keep the production application tray-first and lightweight. Create Settings only when requested and destroy it when closed.
- Do not add telemetry, EC/IGCL/PresentMon integration, persistence, game detection, or extra architecture unless the task explicitly requests that scope.

After implementation, verify the relevant build/test behavior and re-read `README.md` plus the applicable `docs` files against the final diff.
