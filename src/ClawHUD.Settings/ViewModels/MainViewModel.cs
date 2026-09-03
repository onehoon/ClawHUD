using ClawHUD.Settings.Protocol;

namespace ClawHUD.Settings.ViewModels;

/// <summary>
/// One-way projection of the runtime's authoritative settings snapshot onto the
/// existing read-only page. Not a source of truth and has no setter commands —
/// PR2 keeps every control non-interactive.
/// </summary>
internal sealed class MainViewModel
{
    private MainViewModel(SettingsSnapshot s)
    {
        HudEnabled = s.HudEnabled;
        IsVisibilityAlways = s.VisibilityMode == WireVisibilityMode.Always;
        IsVisibilityInGameOnly = s.VisibilityMode == WireVisibilityMode.InGameOnly;
        HudSizeLabel = FormatSizeOffset(s.HudSizeOffset);
        IsFontUnispace = s.HudFont == WireFont.Unispace;
        IsFontSegoeUiVariable = s.HudFont == WireFont.SegoeUiVariable;
        IsAlignmentLeft = s.Alignment == WireAlignment.Left;
        IsAlignmentCenter = s.Alignment == WireAlignment.Center;
        IsAlignmentRight = s.Alignment == WireAlignment.Right;
        IsBackgroundFullWidth = s.BackgroundMode == WireBackgroundMode.FullWidth;
        IsBackgroundContentWidth = s.BackgroundMode == WireBackgroundMode.ContentWidth;
        BackgroundOpacityPercent = s.BackgroundOpacityPercent;
        IntelVrrRangeFixEnabled = s.IntelVrrRangeFixEnabled;
        StartWithWindows = s.StartWithWindows;
    }

    internal static MainViewModel FromSnapshot(SettingsSnapshot snapshot) => new(snapshot);

    public bool HudEnabled { get; }
    public bool IsVisibilityAlways { get; }
    public bool IsVisibilityInGameOnly { get; }
    public string HudSizeLabel { get; }
    public bool IsFontUnispace { get; }
    public bool IsFontSegoeUiVariable { get; }
    public bool IsAlignmentLeft { get; }
    public bool IsAlignmentCenter { get; }
    public bool IsAlignmentRight { get; }
    public bool IsBackgroundFullWidth { get; }
    public bool IsBackgroundContentWidth { get; }
    public ushort BackgroundOpacityPercent { get; }
    public string BackgroundOpacityText => $"{BackgroundOpacityPercent}%";
    public bool IntelVrrRangeFixEnabled { get; }
    public bool StartWithWindows { get; }

    private static string FormatSizeOffset(int offset) => offset switch
    {
        0 => "Default",
        > 0 => $"+{offset}",
        _ => offset.ToString(),
    };
}
