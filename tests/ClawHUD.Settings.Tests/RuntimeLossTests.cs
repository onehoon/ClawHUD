using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;
using Xunit;

namespace ClawHUD.Settings.Tests;

public class RuntimeLossTests
{
    private static ControlClientResult<SettingsSnapshot> Result(ControlClientOutcome outcome,
        ControlStatus status = ControlStatus.Ok) =>
        new(outcome, Status: status);

    [Theory]
    [InlineData(ControlClientOutcome.TransportUnavailable, ControlStatus.Ok, true)]
    [InlineData(ControlClientOutcome.ProtocolError, ControlStatus.RuntimeUnavailable, true)]
    [InlineData(ControlClientOutcome.ProtocolError, ControlStatus.ShuttingDown, true)]
    [InlineData(ControlClientOutcome.ProtocolError, ControlStatus.OperationFailed, false)]
    [InlineData(ControlClientOutcome.ProtocolError, ControlStatus.InvalidValue, false)]
    [InlineData(ControlClientOutcome.TimedOut, ControlStatus.Ok, false)]
    [InlineData(ControlClientOutcome.MalformedResponse, ControlStatus.Ok, false)]
    [InlineData(ControlClientOutcome.Success, ControlStatus.Ok, false)]
    public void IsTerminal_ClassifiesRuntimeLoss(ControlClientOutcome outcome, ControlStatus status, bool expected) =>
        Assert.Equal(expected, RuntimeLoss.IsTerminal(Result(outcome, status)));
}
