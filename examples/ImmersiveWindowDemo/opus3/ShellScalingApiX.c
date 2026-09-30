/*
 * ShellScalingApiX.c -- single instantiation TU for the ShellScalingApi.h thunks. No CRT.
 */

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

#pragma comment(lib, "kernel32.lib")

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "ShellScalingApiX.h"
#include "delayimpx.h"

/* Shared cached shcore.dll handle (process lifetime; never freed). */
static HMODULE g_hShcore;

#include "ShellScalingApiXThunks.inl"
