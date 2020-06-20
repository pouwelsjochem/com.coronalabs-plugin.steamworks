// ----------------------------------------------------------------------------
// 
// PluginMacros.h
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#pragma once


#ifdef _WIN32
	/**
	  Disables compiler warnings regarding the usage of deprecated Win32 APIs used by Steam's header files.
	  This macro is expected to be used before #including Steam's header files.
	  You must use the PLUGIN_DISABLE_STEAM_WARNINGS_END macro after using this macro.
	 */
#	define PLUGIN_DISABLE_STEAM_WARNINGS_BEGIN \
		__pragma(warning(push)) \
		__pragma(warning(disable: 4996))
#else
	/** This macro is only applicable to Win32. It is ignored on all other platforms. */
#	define PLUGIN_DISABLE_STEAM_WARNINGS_BEGIN
#endif


#ifdef _WIN32
	/** Re-enables compiler warnings that were disabled by the PLUGIN_DISABLE_STEAM_WARNINGS_BEGIN macro. */
#	define PLUGIN_DISABLE_STEAM_WARNINGS_END __pragma(warning(pop))
#else
	/** This macro is only applicable to Win32. It is ignored on all other platforms. */
#	define PLUGIN_DISABLE_STEAM_WARNINGS_END
#endif
