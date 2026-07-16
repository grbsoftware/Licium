// licium_res.h
//
// Resource ids for the standalone-only licium.rc. Shared between the resource
// script and the host code (standalone_host.cpp) so the icon id has one home.
// Id 1 sorts ahead of the engine's IDI_PLUGIN_ICON (120) in the PE resource
// directory, which is what makes Explorer show the Licium icon on the exe.

#ifndef LICIUM_RES_H
#define LICIUM_RES_H

#define IDI_LICIUM 1

#endif // LICIUM_RES_H
