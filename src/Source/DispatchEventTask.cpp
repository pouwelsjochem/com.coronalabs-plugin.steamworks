// --------------------------------------------------------------------------------
// 
// DispatchEventTask.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// --------------------------------------------------------------------------------

#include "DispatchEventTask.h"
#include "CoronaLua.h"
#include "SteamImageInfo.h"
#include <sstream>
#include <string>


//---------------------------------------------------------------------------------
// BaseDispatchEventTask Class Members
//---------------------------------------------------------------------------------

BaseDispatchEventTask::BaseDispatchEventTask()
{
}

BaseDispatchEventTask::~BaseDispatchEventTask()
{
}

std::shared_ptr<LuaEventDispatcher> BaseDispatchEventTask::GetLuaEventDispatcher() const
{
	return fLuaEventDispatcherPointer;
}

void BaseDispatchEventTask::SetLuaEventDispatcher(const std::shared_ptr<LuaEventDispatcher>& dispatcherPointer)
{
	fLuaEventDispatcherPointer = dispatcherPointer;
}

bool BaseDispatchEventTask::Execute()
{
	// Do not continue if not assigned a Lua event dispatcher.
	if (!fLuaEventDispatcherPointer)
	{
		return false;
	}

	// Fetch the Lua state the event dispatcher belongs to.
	auto luaStatePointer = fLuaEventDispatcherPointer->GetLuaState();
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the derived class' event table to the top of the Lua stack.
	bool wasPushed = PushLuaEventTableTo(luaStatePointer);
	if (!wasPushed)
	{
		return false;
	}

	// Dispatch the event to all subscribed Lua listeners.
	bool wasDispatched = fLuaEventDispatcherPointer->DispatchEventWithoutResult(luaStatePointer, -1);

	// Pop the event table pushed above from the Lua stack.
	// Note: The DispatchEventWithoutResult() method above does not pop off this table.
	lua_pop(luaStatePointer, 1);

	// Return true if the event was successfully dispatched to Lua.
	return wasDispatched;
}


//---------------------------------------------------------------------------------
// BaseDispatchCallResultEventTask Class Members
//---------------------------------------------------------------------------------

BaseDispatchCallResultEventTask::BaseDispatchCallResultEventTask()
:	fHadIOFailure(false)
{
}

BaseDispatchCallResultEventTask::~BaseDispatchCallResultEventTask()
{
}

bool BaseDispatchCallResultEventTask::HadIOFailure() const
{
	return fHadIOFailure;
}

void BaseDispatchCallResultEventTask::SetHadIOFailure(bool value)
{
	fHadIOFailure = value;
}


//---------------------------------------------------------------------------------
// BaseDispatchLeaderboardEventTask Class Members
//---------------------------------------------------------------------------------

BaseDispatchLeaderboardEventTask::BaseDispatchLeaderboardEventTask()
{
}

BaseDispatchLeaderboardEventTask::~BaseDispatchLeaderboardEventTask()
{
}

const char* BaseDispatchLeaderboardEventTask::GetLeaderboardName() const
{
	return fLeaderboardName.c_str();
}

void BaseDispatchLeaderboardEventTask::SetLeaderboardName(const char* name)
{
	if (name)
	{
		fLeaderboardName = name;
	}
	else
	{
		fLeaderboardName.clear();
	}
}


//---------------------------------------------------------------------------------
// DispatchGameOverlayActivatedEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchGameOverlayActivatedEventTask::kLuaEventName[] = "overlayStatus";

DispatchGameOverlayActivatedEventTask::DispatchGameOverlayActivatedEventTask()
:	fWasActivated(false)
{
}

DispatchGameOverlayActivatedEventTask::~DispatchGameOverlayActivatedEventTask()
{
}

void DispatchGameOverlayActivatedEventTask::AcquireEventDataFrom(const GameOverlayActivated_t& steamEventData)
{
	fWasActivated = steamEventData.m_bActive ? true : false;
}

const char* DispatchGameOverlayActivatedEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchGameOverlayActivatedEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	lua_pushstring(luaStatePointer, fWasActivated ? "shown" : "hidden");
	lua_setfield(luaStatePointer, -2, "phase");
	return true;
}


//---------------------------------------------------------------------------------
// DispatchLeaderboardScoresDownloadedEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchLeaderboardScoresDownloadedEventTask::kLuaEventName[] = "leaderboardEntries";

DispatchLeaderboardScoresDownloadedEventTask::DispatchLeaderboardScoresDownloadedEventTask()
:	fLeaderboardHandle(0)
{
}

DispatchLeaderboardScoresDownloadedEventTask::~DispatchLeaderboardScoresDownloadedEventTask()
{
}

void DispatchLeaderboardScoresDownloadedEventTask::AcquireEventDataFrom(
	const LeaderboardScoresDownloaded_t& steamEventData)
{
	// Initialize member variables.
	fLeaderboardHandle = steamEventData.m_hSteamLeaderboard;
	fEntryCollection.clear();
	SetLeaderboardName(nullptr);

	// Fetch the Steam interface needed to read leaderboard info.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		return;
	}

	// Fetch the leaderboard's unique name.
	if (steamEventData.m_hSteamLeaderboard)
	{
		SetLeaderboardName(steamUserStatsPointer->GetLeaderboardName(steamEventData.m_hSteamLeaderboard));
	}

	// Copy the leaderboard entries from Steam's cache.
	// Note: Steam will unload these entries from its cache after the Steam CCallResult exits.
	if (steamEventData.m_cEntryCount > 0)
	{
		LeaderboardEntry_t entryData;
		fEntryCollection.reserve(steamEventData.m_cEntryCount);
		for (int index = 0; index < steamEventData.m_cEntryCount; index++)
		{
			bool wasSuccessful = steamUserStatsPointer->GetDownloadedLeaderboardEntry(
					steamEventData.m_hSteamLeaderboardEntries, index, &entryData, nullptr, 0);
			if (wasSuccessful)
			{
				fEntryCollection.push_back(entryData);
			}
		}
	}
}

const char* DispatchLeaderboardScoresDownloadedEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchLeaderboardScoresDownloadedEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		lua_pushboolean(luaStatePointer, HadIOFailure() ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "isError");
	}
	{
		auto name = GetLeaderboardName();
		lua_pushstring(luaStatePointer, name ? name : "");
		lua_setfield(luaStatePointer, -2, "leaderboardName");
	}
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << fLeaderboardHandle;
		auto stringResult = stringStream.str();
		lua_pushstring(luaStatePointer, stringResult.c_str());
		lua_setfield(luaStatePointer, -2, "leaderboardHandle");
	}
	{
		lua_createtable(luaStatePointer, (int)fEntryCollection.size(), 0);
		for (int index = 0; index < (int)fEntryCollection.size(); index++)
		{
			const LeaderboardEntry_t& entry = fEntryCollection.at(index);
			lua_newtable(luaStatePointer);
			{
				std::stringstream stringStream;
				stringStream.imbue(std::locale::classic());
				stringStream << entry.m_steamIDUser.ConvertToUint64();
				auto stringResult = stringStream.str();
				lua_pushstring(luaStatePointer, stringResult.c_str());
				lua_setfield(luaStatePointer, -2, "userSteamId");
			}
			{
				lua_pushinteger(luaStatePointer, entry.m_nGlobalRank);
				lua_setfield(luaStatePointer, -2, "globalRank");
			}
			{
				lua_pushinteger(luaStatePointer, entry.m_nScore);
				lua_setfield(luaStatePointer, -2, "score");
			}
			lua_rawseti(luaStatePointer, -2, index + 1);
		}
		lua_setfield(luaStatePointer, -2, "entries");
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchLeaderboardFindResultEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchLeaderboardFindResultEventTask::kLuaEventName[] = "leaderboardInfo";

DispatchLeaderboardFindResultEventTask::DispatchLeaderboardFindResultEventTask()
{
	ClearEventData();
}

DispatchLeaderboardFindResultEventTask::~DispatchLeaderboardFindResultEventTask()
{
}

void DispatchLeaderboardFindResultEventTask::AcquireEventDataFrom(const LeaderboardFindResult_t& steamEventData)
{
	// Re-initialize this object's member variables back to their defaults.
	ClearEventData();

	// Fetch the Steam interface needed to read leaderboard info.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		return;
	}

	// Copy the given Steam event data to this object.
	fIsError = steamEventData.m_bLeaderboardFound ? false : true;
	fLeaderboardHandle = steamEventData.m_hSteamLeaderboard;
	if (!fIsError)
	{
		SetLeaderboardName(steamUserStatsPointer->GetLeaderboardName(steamEventData.m_hSteamLeaderboard));
		fEntryCount = steamUserStatsPointer->GetLeaderboardEntryCount(steamEventData.m_hSteamLeaderboard);
		fSortMethod = steamUserStatsPointer->GetLeaderboardSortMethod(steamEventData.m_hSteamLeaderboard);
		fDisplayType = steamUserStatsPointer->GetLeaderboardDisplayType(steamEventData.m_hSteamLeaderboard);
	}
}

void DispatchLeaderboardFindResultEventTask::ClearEventData()
{
	fIsError = true;
	fLeaderboardHandle = 0;
	fEntryCount = 0;
	fSortMethod = k_ELeaderboardSortMethodNone;
	fDisplayType = k_ELeaderboardDisplayTypeNone;
	SetLeaderboardName(nullptr);
}

const char* DispatchLeaderboardFindResultEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchLeaderboardFindResultEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Combine all Steam error flags into 1 overall Lua error flag.
	// Note: Steam provides a separate I/O failure flag for all CCallResult objects.
	bool isError = fIsError || HadIOFailure();

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		lua_pushboolean(luaStatePointer, isError ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "isError");
	}
	{
		auto name = GetLeaderboardName();
		lua_pushstring(luaStatePointer, name ? name : "");
		lua_setfield(luaStatePointer, -2, "leaderboardName");
	}
	if (!isError)
	{
		{
			std::stringstream stringStream;
			stringStream.imbue(std::locale::classic());
			stringStream << fLeaderboardHandle;
			auto stringResult = stringStream.str();
			lua_pushstring(luaStatePointer, stringResult.c_str());
			lua_setfield(luaStatePointer, -2, "leaderboardHandle");
		}
		{
			lua_pushinteger(luaStatePointer, fEntryCount > 0 ? fEntryCount : 0);
			lua_setfield(luaStatePointer, -2, "entryCount");
		}
		{
			const char* typeName;
			switch (fSortMethod)
			{
				case k_ELeaderboardSortMethodNone:
					typeName = "none";
					break;
				case k_ELeaderboardSortMethodAscending:
					typeName = "ascending";
					break;
				case k_ELeaderboardSortMethodDescending:
					typeName = "descending";
					break;
				default:
					typeName = "unknown";
					break;
			}
			lua_pushstring(luaStatePointer, typeName);
			lua_setfield(luaStatePointer, -2, "sortMethod");
		}
		{
			const char* typeName;
			switch (fDisplayType)
			{
				case k_ELeaderboardDisplayTypeNone:
					typeName = "none";
					break;
				case k_ELeaderboardDisplayTypeNumeric:
					typeName = "numeric";
					break;
				case k_ELeaderboardDisplayTypeTimeSeconds:
					typeName = "seconds";
					break;
				case k_ELeaderboardDisplayTypeTimeMilliSeconds:
					typeName = "milliseconds";
					break;
				default:
					typeName = "unknown";
					break;
			}
			lua_pushstring(luaStatePointer, typeName);
			lua_setfield(luaStatePointer, -2, "displayType");
		}
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchLeaderboardScoreUploadEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchLeaderboardScoreUploadEventTask::kLuaEventName[] = "setHighScore";

DispatchLeaderboardScoreUploadEventTask::DispatchLeaderboardScoreUploadEventTask()
{
	ClearEventData();
}

DispatchLeaderboardScoreUploadEventTask::~DispatchLeaderboardScoreUploadEventTask()
{
}

void DispatchLeaderboardScoreUploadEventTask::AcquireEventDataFrom(const LeaderboardScoreUploaded_t& steamEventData)
{
	// Re-initialize this object's member variables back to their defaults.
	ClearEventData();

	// Fetch the Steam interface needed to read leaderboard info.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		return;
	}

	// Copy the given Steam event data to this object.
	fIsError = steamEventData.m_bSuccess ? false : true;
	fLeaderboardHandle = steamEventData.m_hSteamLeaderboard;
	if (!fIsError)
	{
		SetLeaderboardName(steamUserStatsPointer->GetLeaderboardName(steamEventData.m_hSteamLeaderboard));
		fWasScoreChanged = steamEventData.m_bScoreChanged ? true : false;
		fCurrentGlobalRank = steamEventData.m_nGlobalRankNew;
		fPreviousGlobalRank = steamEventData.m_nGlobalRankPrevious;
	}
}

void DispatchLeaderboardScoreUploadEventTask::ClearEventData()
{
	fIsError = true;
	fLeaderboardHandle = 0;
	fWasScoreChanged = false;
	fCurrentGlobalRank = 0;
	fPreviousGlobalRank = 0;
	SetLeaderboardName(nullptr);
}

const char* DispatchLeaderboardScoreUploadEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchLeaderboardScoreUploadEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Combine all Steam error flags into 1 overall Lua error flag.
	// Note: Steam provides a separate I/O failure flag for all CCallResult objects.
	bool isError = fIsError || HadIOFailure();

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		lua_pushboolean(luaStatePointer, isError ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "isError");
	}
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << fLeaderboardHandle;
		auto stringResult = stringStream.str();
		lua_pushstring(luaStatePointer, stringResult.c_str());
		lua_setfield(luaStatePointer, -2, "leaderboardHandle");
	}
	{
		auto name = GetLeaderboardName();
		lua_pushstring(luaStatePointer, name ? name : "");
		lua_setfield(luaStatePointer, -2, "leaderboardName");
	}
	{
		lua_pushboolean(luaStatePointer, fWasScoreChanged ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "scoreChanged");
	}
	if (fWasScoreChanged)
	{
		if (fCurrentGlobalRank > 0)
		{
			lua_pushinteger(luaStatePointer, fCurrentGlobalRank);
			lua_setfield(luaStatePointer, -2, "currentGlobalRank");
		}
		if (fPreviousGlobalRank > 0)
		{
			lua_pushinteger(luaStatePointer, fPreviousGlobalRank);
			lua_setfield(luaStatePointer, -2, "previousGlobalRank");
		}
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchMicrotransactionAuthorizationResponseEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchMicrotransactionAuthorizationResponseEventTask::kLuaEventName[] = "microtransactionAuthorization";

DispatchMicrotransactionAuthorizationResponseEventTask::DispatchMicrotransactionAuthorizationResponseEventTask()
:	fWasAuthorized(false),
	fOrderId(0)
{
}

DispatchMicrotransactionAuthorizationResponseEventTask::~DispatchMicrotransactionAuthorizationResponseEventTask()
{
}

void DispatchMicrotransactionAuthorizationResponseEventTask::AcquireEventDataFrom(
	const MicroTxnAuthorizationResponse_t& steamEventData)
{
	fWasAuthorized = steamEventData.m_bAuthorized ? true : false;
	fOrderId = steamEventData.m_ulOrderID;
}

const char* DispatchMicrotransactionAuthorizationResponseEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchMicrotransactionAuthorizationResponseEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		lua_pushboolean(luaStatePointer, fWasAuthorized ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "authorized");
	}
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << fOrderId;
		auto stringResult = stringStream.str();
		lua_pushstring(luaStatePointer, stringResult.c_str());
		lua_setfield(luaStatePointer, -2, "orderId");
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchNumberOfCurrentPlayersEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchNumberOfCurrentPlayersEventTask::kLuaEventName[] = "activePlayerCount";

DispatchNumberOfCurrentPlayersEventTask::DispatchNumberOfCurrentPlayersEventTask()
:	fIsError(true),
	fPlayerCount(0)
{
}

DispatchNumberOfCurrentPlayersEventTask::~DispatchNumberOfCurrentPlayersEventTask()
{
}

void DispatchNumberOfCurrentPlayersEventTask::AcquireEventDataFrom(const NumberOfCurrentPlayers_t& steamEventData)
{
	fIsError = steamEventData.m_bSuccess ? false : true;
	fPlayerCount = steamEventData.m_cPlayers;
}

const char* DispatchNumberOfCurrentPlayersEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchNumberOfCurrentPlayersEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Combine all Steam error flags into 1 overall Lua error flag.
	// Note: Steam provides a separate I/O failure flag for all CCallResult objects.
	bool isError = fIsError || HadIOFailure();

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		lua_pushboolean(luaStatePointer, isError ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "isError");
	}
	if (!isError)
	{
		lua_pushinteger(luaStatePointer, fPlayerCount);
		lua_setfield(luaStatePointer, -2, "count");
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchPersonaStateChangeEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchPersonaStateChangedEventTask::kLuaEventName[] = "userInfoUpdate";

DispatchPersonaStateChangedEventTask::DispatchPersonaStateChangedEventTask()
:	fUserIntegerId(0),
	fFlags(0),
	fHasLargeAvatarChanged(false)
{
}

DispatchPersonaStateChangedEventTask::~DispatchPersonaStateChangedEventTask()
{
}

void DispatchPersonaStateChangedEventTask::AcquireEventDataFrom(const PersonaStateChange_t& steamEventData)
{
	// Copy event data.
	fUserIntegerId = steamEventData.m_ulSteamID;
	fFlags = steamEventData.m_nChangeFlags;
	fHasLargeAvatarChanged = false;

	// If the small/medium avatar images have been unloaded, then flag the large avatar as unloaded/changed too.
	if (fFlags & k_EPersonaChangeAvatar)
	{
		auto steamFriendsPointer = SteamFriends();
		if (steamFriendsPointer)
		{
			int imageHandle = steamFriendsPointer->GetSmallFriendAvatar(CSteamID(fUserIntegerId));
			if (0 == imageHandle)
			{
				fHasLargeAvatarChanged = true;
			}
		}
	}
}

void DispatchPersonaStateChangedEventTask::AcquireEventDataFrom(const AvatarImageLoaded_t& steamEventData)
{
	fUserIntegerId = steamEventData.m_steamID.ConvertToUint64();
	fFlags = 0;
	fHasLargeAvatarChanged = false;
	auto imageInfo = SteamImageInfo::FromImageHandle(steamEventData.m_iImage);
	if (imageInfo.IsValid())
	{
		if (imageInfo.GetPixelWidth() >= 184)
		{
			fHasLargeAvatarChanged = true;
		}
		else
		{
			fFlags |= k_EPersonaChangeAvatar;
		}
	}
}

void DispatchPersonaStateChangedEventTask::SetHasLargeAvatarChanged(bool value)
{
	fHasLargeAvatarChanged = value;
}

const char* DispatchPersonaStateChangedEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchPersonaStateChangedEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << fUserIntegerId;
		auto stringResult = stringStream.str();
		lua_pushstring(luaStatePointer, stringResult.c_str());
		lua_setfield(luaStatePointer, -2, "userSteamId");
	}
	{
		lua_pushboolean(luaStatePointer, (fFlags & (k_EPersonaChangeName | k_EPersonaChangeNameFirstSet)) ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "nameChanged");
	}
	{
		bool hasStatusChanged = false;
		if (fFlags & (k_EPersonaChangeStatus | k_EPersonaChangeComeOnline | k_EPersonaChangeGoneOffline))
		{
			hasStatusChanged = true;
		}
		lua_pushboolean(luaStatePointer, hasStatusChanged ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "statusChanged");
	}
	{
		bool hasAvatarChanged = (fFlags & k_EPersonaChangeAvatar) ? true : false;
		lua_pushboolean(luaStatePointer, hasAvatarChanged ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "smallAvatarChanged");
		lua_pushboolean(luaStatePointer, hasAvatarChanged ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "mediumAvatarChanged");
	}
	{
		lua_pushboolean(luaStatePointer, fHasLargeAvatarChanged ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "largeAvatarChanged");
	}
	{
		lua_pushboolean(luaStatePointer, (fFlags & k_EPersonaChangeRelationshipChanged) ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "relationshipChanged");
	}
	{
		// lua_pushboolean(luaStatePointer, (fFlags & k_EPersonaChangeFacebookInfo) ? 1 : 0);
		lua_pushboolean(luaStatePointer, 0);
		lua_setfield(luaStatePointer, -2, "facebookInfoChanged");
	}
	{
		lua_pushboolean(luaStatePointer, (fFlags & k_EPersonaChangeNickname) ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "nicknameChanged");
	}
	{
		lua_pushboolean(luaStatePointer, (fFlags & k_EPersonaChangeSteamLevel) ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "steamLevelChanged");
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchUserAchievementIconFetchedEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchUserAchievementIconFetchedEventTask::kLuaEventName[] = "achievementImageUpdate";

DispatchUserAchievementIconFetchedEventTask::DispatchUserAchievementIconFetchedEventTask()
:	fIsUnlocked(false),
	fImageHandle(0)
{
}

DispatchUserAchievementIconFetchedEventTask::~DispatchUserAchievementIconFetchedEventTask()
{
}

void DispatchUserAchievementIconFetchedEventTask::AcquireEventDataFrom(
	const UserAchievementIconFetched_t& steamEventData)
{
	fAchievementName = steamEventData.m_rgchAchievementName;
	fIsUnlocked = steamEventData.m_bAchieved;
	fImageHandle = steamEventData.m_nIconHandle;
}

const char* DispatchUserAchievementIconFetchedEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchUserAchievementIconFetchedEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Do not continue if unable to fetch image information.
	// Note: This can only happen if we're pushing this event to Lua long after the event was received
	//       from Steam and Steam has already unloaded the image from the cache.
	auto imageInfo = SteamImageInfo::FromImageHandle(fImageHandle);
	if (imageInfo.IsNotValid())
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		lua_pushstring(luaStatePointer, fAchievementName.empty() ? "" : fAchievementName.c_str());
		lua_setfield(luaStatePointer, -2, "achievementName");
	}
	{
		bool wasPushed = imageInfo.PushToLua(luaStatePointer);
		if (!wasPushed)
		{
			lua_createtable(luaStatePointer, 0, 0);
		}
		lua_setfield(luaStatePointer, -2, "imageInfo");
	}
	{
		lua_pushboolean(luaStatePointer, fIsUnlocked ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "unlocked");
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchUserAchievementStoredEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchUserAchievementStoredEventTask::kLuaEventName[] = "achievementInfoUpdate";

DispatchUserAchievementStoredEventTask::DispatchUserAchievementStoredEventTask()
:	fIsGroup(false),
	fCurrentProgress(0),
	fMaxProgress(0)
{
}

DispatchUserAchievementStoredEventTask::~DispatchUserAchievementStoredEventTask()
{
}

void DispatchUserAchievementStoredEventTask::AcquireEventDataFrom(const UserAchievementStored_t& steamEventData)
{
	fAchievementName = steamEventData.m_rgchAchievementName;
	fIsGroup = steamEventData.m_bGroupAchievement;
	fCurrentProgress = steamEventData.m_nCurProgress;
	fMaxProgress = steamEventData.m_nMaxProgress;
}

const char* DispatchUserAchievementStoredEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchUserAchievementStoredEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		lua_pushstring(luaStatePointer, fAchievementName.empty() ? "" : fAchievementName.c_str());
		lua_setfield(luaStatePointer, -2, "achievementName");
	}
	{
		lua_pushboolean(luaStatePointer, fIsGroup ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "isGroup");
	}
	if (fMaxProgress > 0)
	{
		{
			lua_pushinteger(luaStatePointer, fCurrentProgress);
			lua_setfield(luaStatePointer, -2, "currentProgress");
		}
		{
			lua_pushinteger(luaStatePointer, fMaxProgress);
			lua_setfield(luaStatePointer, -2, "maxProgress");
		}
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchUserStatsReceivedEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchUserStatsReceivedEventTask::kLuaEventName[] = "userProgressUpdate";

DispatchUserStatsReceivedEventTask::DispatchUserStatsReceivedEventTask()
:	fUserIntegerId(0),
	fSteamResultCode(k_EResultFail)
{
}

DispatchUserStatsReceivedEventTask::~DispatchUserStatsReceivedEventTask()
{
}

void DispatchUserStatsReceivedEventTask::AcquireEventDataFrom(const UserStatsReceived_t& steamEventData)
{
	fUserIntegerId = steamEventData.m_steamIDUser.ConvertToUint64();
	fSteamResultCode = steamEventData.m_eResult;
}

const char* DispatchUserStatsReceivedEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchUserStatsReceivedEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << fUserIntegerId;
		auto stringResult = stringStream.str();
		lua_pushstring(luaStatePointer, stringResult.c_str());
		lua_setfield(luaStatePointer, -2, "userSteamId");
	}
	{
		lua_pushboolean(luaStatePointer, (fSteamResultCode != k_EResultOK) ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "isError");
	}
	{
		lua_pushinteger(luaStatePointer, fSteamResultCode);
		lua_setfield(luaStatePointer, -2, "resultCode");
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchUserStatsStoredEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchUserStatsStoredEventTask::kLuaEventName[] = "userProgressSave";

DispatchUserStatsStoredEventTask::DispatchUserStatsStoredEventTask()
:	fUserIntegerId(0),
	fSteamResultCode(k_EResultFail)
{
}

DispatchUserStatsStoredEventTask::~DispatchUserStatsStoredEventTask()
{
}

void DispatchUserStatsStoredEventTask::AcquireEventDataFrom(const UserStatsStored_t& steamEventData)
{
	// Copy the given Steam event data to this object.
	fSteamResultCode = steamEventData.m_eResult;

	// Fetch the current user's ID.
	fUserIntegerId = 0;
	auto steamUserPointer = SteamUser();
	if (steamUserPointer)
	{
		fUserIntegerId = steamUserPointer->GetSteamID().ConvertToUint64();
	}
}

const char* DispatchUserStatsStoredEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchUserStatsStoredEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << fUserIntegerId;
		auto stringResult = stringStream.str();
		lua_pushstring(luaStatePointer, stringResult.c_str());
		lua_setfield(luaStatePointer, -2, "userSteamId");
	}
	{
		lua_pushboolean(luaStatePointer, (fSteamResultCode != k_EResultOK) ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "isError");
	}
	{
		lua_pushinteger(luaStatePointer, fSteamResultCode);
		lua_setfield(luaStatePointer, -2, "resultCode");
	}
	return true;
}


//---------------------------------------------------------------------------------
// DispatchUserStatsUnloadedEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchUserStatsUnloadedEventTask::kLuaEventName[] = "userProgressUnload";

DispatchUserStatsUnloadedEventTask::DispatchUserStatsUnloadedEventTask()
:	fUserIntegerId(0)
{
}

DispatchUserStatsUnloadedEventTask::~DispatchUserStatsUnloadedEventTask()
{
}

void DispatchUserStatsUnloadedEventTask::AcquireEventDataFrom(const UserStatsUnloaded_t& steamEventData)
{
	fUserIntegerId = steamEventData.m_steamIDUser.ConvertToUint64();
}

const char* DispatchUserStatsUnloadedEventTask::GetLuaEventName() const
{
	return kLuaEventName;
}

bool DispatchUserStatsUnloadedEventTask::PushLuaEventTableTo(lua_State* luaStatePointer) const
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Push the event data to Lua.
	CoronaLuaNewEvent(luaStatePointer, kLuaEventName);
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << fUserIntegerId;
		auto stringResult = stringStream.str();
		lua_pushstring(luaStatePointer, stringResult.c_str());
		lua_setfield(luaStatePointer, -2, "userSteamId");
	}
	return true;
}
