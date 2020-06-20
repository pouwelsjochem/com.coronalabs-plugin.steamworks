// ----------------------------------------------------------------------------
// 
// SteamUserImageType.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "SteamUserImageType.h"
#include <string>
#include <unordered_map>


static std::unordered_map<std::string, SteamUserImageType*> sSteamUserImageTypeMap;

const SteamUserImageType SteamUserImageType::kUnknown;
const SteamUserImageType SteamUserImageType::kAvatarSmall("smallAvatar", 32, 32);
const SteamUserImageType SteamUserImageType::kAvatarMedium("mediumAvatar", 64, 64);
const SteamUserImageType SteamUserImageType::kAvatarLarge("largeAvatar", 184, 184);


SteamUserImageType::SteamUserImageType()
:	SteamUserImageType(nullptr, 0, 0)
{
}

SteamUserImageType::SteamUserImageType(const char* coronaStringId, int pixelWidth, int pixelHeight)
:	fCoronaStringId(coronaStringId),
	fDefaultPixelWidth(pixelWidth),
	fDefaultPixelHeight(pixelHeight)
{
	if (fCoronaStringId)
	{
		sSteamUserImageTypeMap[std::string(fCoronaStringId)] = this;
	}
}

SteamUserImageType::~SteamUserImageType()
{
}

const char* SteamUserImageType::GetCoronaStringId() const
{
	return fCoronaStringId ? fCoronaStringId : "unknown";
}

int SteamUserImageType::GetDefaultPixelWidth() const
{
	return fDefaultPixelWidth;
}

int SteamUserImageType::GetDefaultPixelHeight() const
{
	return fDefaultPixelHeight;
}

bool SteamUserImageType::operator==(const SteamUserImageType& valueType) const
{
	return (fCoronaStringId == valueType.fCoronaStringId);
}

bool SteamUserImageType::operator!=(const SteamUserImageType& valueType) const
{
	return (fCoronaStringId != valueType.fCoronaStringId);
}

SteamUserImageType SteamUserImageType::FromCoronaStringId(const char* stringId)
{
	if (stringId)
	{
		auto iterator = sSteamUserImageTypeMap.find(std::string(stringId));
		if (iterator != sSteamUserImageTypeMap.end())
		{
			return *(iterator->second);
		}
	}
	return SteamUserImageType::kUnknown;
}
