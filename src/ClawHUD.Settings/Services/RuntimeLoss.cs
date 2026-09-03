using ClawHUD.Settings.Protocol;

namespace ClawHUD.Settings.Services;

/// <summary>
/// Classifies a Control-IPC result as terminal proof that this frontend can no
/// longer control its ClawHUD runtime instance (transport gone, or the runtime
/// reported it is unavailable / shutting down). A plain timeout is intentionally
/// not terminal — it can be transient and a later interaction may recover.
/// </summary>
public static class RuntimeLoss
{
    public static bool IsTerminal<T>(ControlClientResult<T> result) =>
        result.Outcome == ControlClientOutcome.TransportUnavailable ||
        (result.Outcome == ControlClientOutcome.ProtocolError &&
         result.Status is ControlStatus.RuntimeUnavailable or ControlStatus.ShuttingDown);
}
