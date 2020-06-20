// ----------------------------------------------------------------------------
// 
// SteamImageInfo.h
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#pragma once

#include "PluginMacros.h"
PLUGIN_DISABLE_STEAM_WARNINGS_BEGIN
#	include "steam_api.h"
PLUGIN_DISABLE_STEAM_WARNINGS_END

// Forward declarations.
extern "C"
{
	struct lua_State;
}


class SteamImageInfo final
{
	public:
		SteamImageInfo();
		virtual ~SteamImageInfo();

		bool IsValid() const;
		bool IsNotValid() const;
		int GetImageHandle() const;
		uint32 GetPixelWidth() const;
		uint32 GetPixelHeight() const;
		bool PushToLua(lua_State* luaStatePointer) const;

		static SteamImageInfo FromImageHandle(int imageHandle);

	private:
		int fImageHandle;
		uint32 fPixelWidth;
		uint32 fPixelHeight;
};
