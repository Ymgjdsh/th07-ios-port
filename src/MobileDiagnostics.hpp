#pragma once

namespace MobileDiagnostics
{
void Initialize();
void Log(const char *tag, const char *format, ...);
void Shutdown();
} // namespace MobileDiagnostics
