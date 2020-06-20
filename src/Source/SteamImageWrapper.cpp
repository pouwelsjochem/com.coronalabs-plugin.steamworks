// --------------------------------------------------------------------------------
// 
// DispatchEventTask.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// --------------------------------------------------------------------------------

#include "SteamImageWrapper.h"
#include "CoronaLua.h"
#include "CoronaGraphics.h"
#include "PluginMacros.h"


PLUGIN_DISABLE_STEAM_WARNINGS_BEGIN
#	include "steam_api.h"
PLUGIN_DISABLE_STEAM_WARNINGS_END



int SteamImageWrapper::PushTexture(lua_State* L, int image)
{
	if(image <= 0)
	{
		lua_pushnil(L);
		return 1;
	}

	auto utils = SteamUtils();
	if(!utils)
	{
		lua_pushnil(L);
		return 1;
	}


	static CoronaExternalTextureCallbacks callbacks = {0};
	callbacks.size = sizeof(CoronaExternalTextureCallbacks);
	callbacks.getWidth = Self::getWidth;
	callbacks.getHeight = Self::getHeight;
	callbacks.onRequestBitmap = Self::onRequestBitmap;
	callbacks.onReleaseBitmap = Self::onReleaseBitmap;
	callbacks.onFinalize = Self::onFinalize;

	// it will destroy itself when Corona would ask for it
	return CoronaExternalPushTexture(L, &callbacks, new Self(image));
}


SteamImageWrapper::SteamImageWrapper(int image)
:  buff(nullptr)
,  image(image)
{
	auto utils = SteamUtils();
	utils->GetImageSize(image, &width, &height);
}

unsigned SteamImageWrapper::getWidth(void* userData)
{
	Self* self = (Self*)userData;
	return self->width;
}

unsigned SteamImageWrapper::getHeight(void* userData)
{
	Self* self = (Self*)userData;
	return self->height;
}

const void* SteamImageWrapper::onRequestBitmap(void* userData)
{
	Self* self = (Self*)userData;
	auto utils = SteamUtils();
	if(!self->buff && utils)
	{
		int sz = self->width*self->height*4;
		self->buff = new unsigned char[sz];
		utils->GetImageRGBA(self->image, self->buff, sz);
	}
	return self->buff;
}

void SteamImageWrapper::onReleaseBitmap(void* userData)
{
	Self* self = (Self*)userData;
	if(self->buff)
	{
		delete [] self->buff;
		self->buff = nullptr;
	}
}

void SteamImageWrapper::onFinalize(void *userData)
{
	Self* self = (Self*)userData;
	delete self;
}


