// ----------------------------------------------------------------------------
// 
// SteamImageInfo.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "SteamImageInfo.h"
extern "C"
{
#	include "lua.h"
}


SteamImageInfo::SteamImageInfo()
:	fImageHandle(0),
	fPixelWidth(0),
	fPixelHeight(0)
{
}

SteamImageInfo::~SteamImageInfo()
{
}

bool SteamImageInfo::IsValid() const
{
	if ((fImageHandle != 0) && (fImageHandle != -1))
	{
		if ((fPixelWidth > 0) && (fPixelHeight > 0))
		{
			return true;
		}
	}
	return false;
}

bool SteamImageInfo::IsNotValid() const
{
	return !IsValid();
}

int SteamImageInfo::GetImageHandle() const
{
	return fImageHandle;
}

uint32 SteamImageInfo::GetPixelWidth() const
{
	return fPixelWidth;
}

uint32 SteamImageInfo::GetPixelHeight() const
{
	return fPixelHeight;
}

bool SteamImageInfo::PushToLua(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push this object's information to Lua.
	if (IsValid())
	{
		// Push a table of image information to Lua.
		lua_createtable(luaStatePointer, 0, 3);
		lua_pushinteger(luaStatePointer, fImageHandle);
		lua_setfield(luaStatePointer, -2, "imageHandle");
		lua_pushnumber(luaStatePointer, (double)fPixelWidth);
		lua_setfield(luaStatePointer, -2, "pixelWidth");
		lua_pushnumber(luaStatePointer, (double)fPixelHeight);
		lua_setfield(luaStatePointer, -2, "pixelHeight");
	}
	else
	{
		// Push nil to Lua since Steam image reference is invalid.
		lua_pushnil(luaStatePointer);
	}

	// Returning true indicates we pushed something (table or nil) to Lua.
	return true;
}

SteamImageInfo SteamImageInfo::FromImageHandle(int imageHandle)
{
	// Create an invalid image info object.
	SteamImageInfo imageInfo;
	
	// Fetch referenced image's info from Steam to be copied to above object if successful.
	auto steamUtilsPointer = SteamUtils();
	if (steamUtilsPointer)
	{
		uint32 pixelWidth = 0;
		uint32 pixelHeight = 0;
		bool wasSuccessful = steamUtilsPointer->GetImageSize(imageHandle, &pixelWidth, &pixelHeight);
		if (wasSuccessful)
		{
			imageInfo.fImageHandle = imageHandle;
			imageInfo.fPixelWidth = pixelWidth;
			imageInfo.fPixelHeight = pixelHeight;
		}
	}

	// Return a new image info object by value.
	return imageInfo;
}
