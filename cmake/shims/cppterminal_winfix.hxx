// Injected for Windows builds to fix missing declarations in cpp-terminal
#pragma once

#if defined(_WIN32)
  #include <windows.h>
  // Undefine the MessageBox macro ASAP to avoid collisions with
  // enum names like ExceptionDestination::MessageBox in cpp-terminal.
  #if defined(MessageBox)
    #undef MessageBox
  #endif
  #include <shellapi.h>  // for CommandLineToArgvW
  #include <winternl.h>  // for NTSTATUS, PRTL_OSVERSIONINFOW
#endif
