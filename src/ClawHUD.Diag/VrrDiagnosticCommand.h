#pragma once

class VrrDiagnosticCommand
{
public:
    void Run() noexcept;

private:
    void RunImpl();
};
