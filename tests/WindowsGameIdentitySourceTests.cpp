#include "GameDetection/WindowsGameIdentitySource.h"

#include <iostream>

using namespace clawhud;

int main()
{
    bool ok = true;
    const auto check = [&](bool value, const char* name)
    {
        if (!value)
        {
            std::cerr << "FAILED: " << name << '\n';
            ok = false;
        }
    };
    const auto config = ParseMicrosoftGameConfig(LR"xml(
        <Game configVersion="1">
          <TitleId>12345</TitleId>
          <MSAAppId>msa-7</MSAAppId>
          <ExecutableList>
            <Executable Name="Game.exe" TargetDeviceFamily="PC" Architecture="x64" Id="game" />
            <Executable Name='Launcher.exe' TargetDeviceFamily='Windows.Desktop' Architecture='x86' Id='launcher' />
          </ExecutableList>
        </Game>)xml");
    check(config.recognizedGameRoot && config.storeId.empty() &&
        config.titleId == L"12345" && config.msaAppId == L"msa-7" &&
        config.executables[0].targetDeviceFamily == L"PC" &&
        config.executables[0].architecture == L"x64",
        "MicrosoftGame.config fields parse");
    check(config.executables.size() == 2 && config.executables[0].name == L"Game.exe" &&
        config.executables[1].id == L"launcher" &&
        config.executables[1].targetDeviceFamily == L"Windows.Desktop",
        "multiple executable entries parse");
    check(ParseMicrosoftGameConfig(L"").executables.empty() &&
        !ParseMicrosoftGameConfig(L"not xml").recognizedGameRoot, "missing config input");
    check(!ParseMicrosoftGameConfig(L"<NotGame><StoreId").recognizedGameRoot,
        "unrecognized config root");
    const auto optional = ParseMicrosoftGameConfig(L"<MicrosoftGame><StoreId>x</StoreId></MicrosoftGame>");
    check(optional.recognizedGameRoot && optional.titleId.empty() && optional.executables.empty(),
        "optional config fields may be absent");
    check(WindowsExecutableNamesMatch(L"C:\\Games\\GAME.EXE", L"game.exe") &&
        !WindowsExecutableNamesMatch(L"game.exe", L"launcher.exe"),
        "executable matching is case insensitive and basename based");
    check(PackageMetadataCacheKey(L"Game_1.0_x64") !=
        PackageMetadataCacheKey(L"Game_1.1_x64"),
        "package cache key preserves package full name version");
    return ok ? 0 : 1;
}
