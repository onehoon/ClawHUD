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
        <MicrosoftGame>
          <StoreId>9NTEST</StoreId>
          <TitleId>12345</TitleId>
          <MSAAppId>msa-7</MSAAppId>
          <TargetDeviceFamily>Windows.Desktop</TargetDeviceFamily>
          <ExecutableList>
            <Executable Name="Game.exe" Id="game" />
            <Executable Name="Launcher.exe" Id="launcher" />
          </ExecutableList>
        </MicrosoftGame>)xml");
    check(config.wellFormed && config.storeId == L"9NTEST" &&
        config.titleId == L"12345" && config.msaAppId == L"msa-7" &&
        config.targetDeviceFamily == L"Windows.Desktop",
        "MicrosoftGame.config fields parse");
    check(config.executables.size() == 2 && config.executables[0].name == L"Game.exe" &&
        config.executables[1].id == L"launcher", "multiple executable entries parse");
    check(ParseMicrosoftGameConfig(L"").executables.empty() &&
        !ParseMicrosoftGameConfig(L"not xml").wellFormed, "missing config input");
    check(!ParseMicrosoftGameConfig(L"<MicrosoftGame><StoreId").wellFormed,
        "malformed config is not well formed");
    const auto optional = ParseMicrosoftGameConfig(L"<MicrosoftGame><StoreId>x</StoreId></MicrosoftGame>");
    check(optional.wellFormed && optional.titleId.empty() && optional.executables.empty(),
        "optional config fields may be absent");
    check(WindowsExecutableNamesMatch(L"C:\\Games\\GAME.EXE", L"game.exe") &&
        !WindowsExecutableNamesMatch(L"game.exe", L"launcher.exe"),
        "executable matching is case insensitive and basename based");
    check(PackageMetadataCacheKey(L"Game_1.0_x64") !=
        PackageMetadataCacheKey(L"Game_1.1_x64"),
        "package cache key preserves package full name version");
    return ok ? 0 : 1;
}
