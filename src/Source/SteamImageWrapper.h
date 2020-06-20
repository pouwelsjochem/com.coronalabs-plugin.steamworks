// ----------------------------------------------------------------------------
// 
// SteamImageWrapper.h
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#pragma once

// Forward declarations.
extern "C"
{
	struct lua_State;
}

class SteamImageWrapper
{
	typedef SteamImageWrapper Self;
	public:
		static int PushTexture(lua_State* L, int image);

	private:
		SteamImageWrapper(int image);

	private:
		static unsigned getWidth(void* userData);
		static unsigned getHeight(void* userData);
		static const void* onRequestBitmap(void* userData);
		static void onReleaseBitmap(void* userData);
		static void onFinalize(void *userData);

	private:
		int image;
		unsigned width;
		unsigned height;
		unsigned char *buff;
};
