#pragma once

#include <string>
#include <vector>

namespace clawhud
{
struct PanelIdentity
{
    std::string manufacturer;
    std::string productCode;
    std::string panelName;
    bool active{};
    std::string instanceName;
};

bool IsAffectedPanel(const PanelIdentity& identity);
std::vector<PanelIdentity> EnumeratePanelIdentities();
std::string DecodeWmiUShortString(const std::vector<unsigned short>& values);
}
