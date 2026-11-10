#pragma once
// Append-only diagnostic log at %ProgramData%\openeq\apo.log.
// Never call from the real-time thread (APOProcess).

#include <string>

namespace openeq::apo {

void log(const wchar_t* fmt, ...);

// %ProgramData%\openeq (no trailing slash).
std::wstring dataDir();

} // namespace openeq::apo
