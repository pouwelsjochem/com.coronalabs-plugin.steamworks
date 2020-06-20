// ----------------------------------------------------------------------------
// 
// SteamStatValueType.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "SteamStatValueType.h"
#include <string>
#include <unordered_map>


static std::unordered_map<std::string, SteamStatValueType*> sSteamStatValueTypeMap;

const SteamStatValueType SteamStatValueType::kUnknown;
const SteamStatValueType SteamStatValueType::kInteger("int");
const SteamStatValueType SteamStatValueType::kFloat("float");
const SteamStatValueType SteamStatValueType::kAverageRate("averageRate");


SteamStatValueType::SteamStatValueType()
:	fCoronaStringId(nullptr)
{
}

SteamStatValueType::SteamStatValueType(const char* coronaStringId)
:	fCoronaStringId(coronaStringId)
{
	if (fCoronaStringId)
	{
		sSteamStatValueTypeMap[std::string(fCoronaStringId)] = this;
	}
}

SteamStatValueType::~SteamStatValueType()
{
}

const char* SteamStatValueType::GetCoronaStringId() const
{
	return fCoronaStringId ? fCoronaStringId : "unknown";
}

bool SteamStatValueType::operator==(const SteamStatValueType& valueType) const
{
	return (fCoronaStringId == valueType.fCoronaStringId);
}

bool SteamStatValueType::operator!=(const SteamStatValueType& valueType) const
{
	return (fCoronaStringId != valueType.fCoronaStringId);
}

SteamStatValueType SteamStatValueType::FromCoronaStringId(const char* stringId)
{
	if (stringId)
	{
		auto iterator = sSteamStatValueTypeMap.find(std::string(stringId));
		if (iterator != sSteamStatValueTypeMap.end())
		{
			return *(iterator->second);
		}
	}
	return SteamStatValueType::kUnknown;
}
