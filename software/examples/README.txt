AFS 8CH SDK simulation example

Run afs_8ch_simulation_demo.exe from the application folder:
    .\afs_8ch_simulation_demo.exe .\simulation_demo.csv

No board or installed USB system driver is required. Keep the bundled
ftd2xx.dll beside the executable. The example never searches or opens USB.

Source: simulation_demo.cpp
Public SDK header: include/afs/afs_driver.h in the source distribution.
Build: afs_8ch_simulation_demo.vcxproj, or the afs_8ch.sln solution.
The project compiles the SDK source; no separate SDK DLL is distributed.

For GUI steps and the SDK API, see docs/simulation.md.
