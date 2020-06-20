// --------------------------------------------------------------------------------
// 
// SteamImageWrapper.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// --------------------------------------------------------------------------------

#include "CoronaLua.h"
#include "CoronaMacros.h"
#include "DispatchEventTask.h"
#include "LuaEventDispatcher.h"
#include "PluginConfigLuaSettings.h"
#include "PluginMacros.h"
#include "RuntimeContext.h"
#include "SteamImageInfo.h"
#include "SteamImageWrapper.h"
#include "SteamStatValueType.h"
#include "SteamUserImageType.h"
#include <cmath>
#include <sstream>
#include <stdint.h>
#include <string>
#include <thread>
extern "C"
{
#	include "lua.h"
#	include "lauxlib.h"
}
PLUGIN_DISABLE_STEAM_WARNINGS_BEGIN
#	include "steam_api.h"
PLUGIN_DISABLE_STEAM_WARNINGS_END


//---------------------------------------------------------------------------------
// Constants
//---------------------------------------------------------------------------------

/**
  Name of the environment variable used by Steam to store the app's unique ID.

  This name is defined by Valve, not Corona Labs. This environment variable is typically
  set on startup when the app is launched via the Steam client.
 */
static const char kSteamAppIdEnvironmentVariableName[] = "SteamAppId";


//---------------------------------------------------------------------------------
// Private Static Variables
//---------------------------------------------------------------------------------

/**
  Gets the thread ID that all plugin instances are currently running in.
  This member is only applicable if at least 1 plugin instance exists.
  Intended to avoid multiple plugin instances from being loaded at the same time on different threads.
 */
static std::thread::id sMainThreadId;


//---------------------------------------------------------------------------------
// Private Static Functions
//---------------------------------------------------------------------------------

/**
  Fetch this application's Steam AppId in string form and copies it to the given argument
  @param stringId The string to copy the ID to.
  @return Returns true if the app ID was successfully fetched and copied to the given argument.
  
          Returns false if failed the Steam app ID was not copied to the given argument.
          This typically happens if the app ID is not set in the "config.lua" file.
 */
bool CopySteamAppIdTo(std::string& stringId)
{
	bool wasCopied = false;
	auto steamUtilsPointer = SteamUtils();
	if (steamUtilsPointer)
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << steamUtilsPointer->GetAppID();
		stringId = stringStream.str();
		wasCopied = true;
	}
	else
	{
#ifdef _WIN32
		size_t characterCount = 0;
		char stringValue[256];
		stringValue[0] = '\0';
		getenv_s(&characterCount, stringValue, sizeof(stringValue), kSteamAppIdEnvironmentVariableName);
#else
		auto stringValue = getenv(kSteamAppIdEnvironmentVariableName);
#endif
		if (stringValue && (stringValue[0] != '\0'))
		{
			stringId = stringValue;
			wasCopied = true;
		}
	}
	return wasCopied;
}

/**
  Determines if the steam overlay can be shown or not.

  Cannot be shown if:
  * User disabled overlays in Steam client's user settings.
  * Steam hasn't finished initializing overlay support on app startup. (Requires a few seconds.)
  * Failed to connect to the steam client.
  @return Returns true if the overlay can be shown. Returns false if not.
 */
bool CanShowSteamOverlay()
{
	bool canShow = false;
	auto steamUtilsPointer = SteamUtils();
	if (steamUtilsPointer)
	{
		canShow = steamUtilsPointer->IsOverlayEnabled();
	}
	return canShow;
}

/**
  Pushes the Steamworks plugin table to the top of the Lua stack.
  @param luaStatePointer Pointer to the Lua state to push the plugin table to.
  @return Returns true if the plugin table was successfully pushed to the top of the Lua stack.

          Returns false if failed to load the plugin table. In this case, nil will be pushed to
		  the top of the Lua stack, unless given a null Lua state pointer argument.
 */
bool PushPluginTableTo(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Fetch the current Lua stack count.
	int previousLuaStackCount = lua_gettop(luaStatePointer);

	// Call the Lua require() function to push this plugin's table to the top of the stack.
	bool wasSuccessful = false;
	lua_getglobal(luaStatePointer, "require");
	if (lua_isfunction(luaStatePointer, -1))
	{
		lua_pushstring(luaStatePointer, "plugin.steamworks");
		CoronaLuaDoCall(luaStatePointer, 1, 1);
		if (lua_istable(luaStatePointer, -1))
		{
			wasSuccessful = true;
		}
		else
		{
			lua_pop(luaStatePointer, 1);
		}
	}
	else
	{
		lua_pop(luaStatePointer, 1);
	}

	// Pop off remaing items above, leaving the plugin's table at the top of the stack.
	// If we've failed to load the plugin, then push nil instead.
	if (wasSuccessful)
	{
		lua_insert(luaStatePointer, previousLuaStackCount + 1);
		lua_settop(luaStatePointer, previousLuaStackCount + 1);
	}
	else
	{
		lua_settop(luaStatePointer, previousLuaStackCount);
		lua_pushnil(luaStatePointer);
	}
	return wasSuccessful;
}

/**
  Determines if the given Lua state is running under the Corona Simulator.
  @param luaStatePointer Pointer to the Lua state to check.
  @return Returns true if the given Lua state is running under the Corona Simulator.

          Returns false if running under a real device/desktop application or if given a null pointer.
 */
bool IsRunningInCoronaSimulator(lua_State* luaStatePointer)
{
	bool isSimulator = false;
	lua_getglobal(luaStatePointer, "system");
	if (lua_istable(luaStatePointer, -1))
	{
		lua_getfield(luaStatePointer, -1, "getInfo");
		if (lua_isfunction(luaStatePointer, -1))
		{
			lua_pushstring(luaStatePointer, "environment");
			int callResultCode = CoronaLuaDoCall(luaStatePointer, 1, 1);
			if (!callResultCode && (lua_type(luaStatePointer, -1) == LUA_TSTRING))
			{
				isSimulator = (strcmp(lua_tostring(luaStatePointer, -1), "simulator") == 0);
			}
		}
		lua_pop(luaStatePointer, 1);
	}
	lua_pop(luaStatePointer, 1);
	return isSimulator;
}

/**
  Creates and returns a lambda to be invoked by the RuntimeContext::AddEventHandlerFor() method when
  a task is about to be queued for dispatching a Lua event.

  Intended to be used for leaderboard related events. Copies the leaderboard's unique name to the event
  since this name is not received by Steam's CCallResult data.
  @param leaderboardName Unique name of the leaderboard used by the Steam request API.
                         Will be copied to the Lua dispatching event table.
  @return Returns a lambda for handling the leaderboard event dispatching task. Expected to be assigned
          to the "RuntimeContext::EventHandlerSettings::QueuingEventTaskCallback" field.

          Returns an empty lambda if given a null or empty string.
 */
std::function<void(RuntimeContext::QueuingEventTaskCallbackArguments&)>
CreateQueueingLeaderboardEventTaskCallbackWith(const char* leaderboardName)
{
	// Return a callback that no-ops if given an invalid leaderboard name.
	if (!leaderboardName || ('\0' == leaderboardName[0]))
	{
		return std::function<void(RuntimeContext::QueuingEventTaskCallbackArguments&)>();
	}

	// Return a lambda used to finish setting up a leaderboard event dispatcher.
	std::string capturedLeaderboardName(leaderboardName);
	return [capturedLeaderboardName](RuntimeContext::QueuingEventTaskCallbackArguments& arguments)->void
	{
		auto leaderboardTaskPointer = dynamic_cast<BaseDispatchLeaderboardEventTask*>(arguments.TaskPointer);
		if (leaderboardTaskPointer)
		{
			leaderboardTaskPointer->SetLeaderboardName(capturedLeaderboardName.c_str());
		}
	};
}


//---------------------------------------------------------------------------------
// Steam Event Handlers
//---------------------------------------------------------------------------------

/**
  Called by Steam when it wants to log a message.

  Overriden by this plugin to log Steam messages to stdout/stderr and to properly prefix Steam warnings
  so that they'll be highlighted in Corona's logging window.

  Note that this function will only be called when:
  * Running under the Visual Studio or Xcode debugger.
  * If the application was launched with a "-debug_steamapi" command line argument.
  @param severityLevel Set to 0 for normal message. Set to 1 for warning messages.
  @param message The Steam logging message received.
 */
void OnSteamWarningMessageReceived(int severityLevel, const char* message)
{
	// Do not continue if given a null/empty message.
	if (!message || ('\0' == message[0]))
	{
		return;
	}

	// Log the message based on its severity level.
	// Note: Don't use the CORONA_LOG_WARNING() macro since it adds an extra '\n' that we don't want.
	if (severityLevel < 1)
	{
		CoronaLog("[Steam] %s", message);
	}
	else
	{
		CoronaLog("WARNING: [Steam] %s", message);
	}
}


//---------------------------------------------------------------------------------
// Lua API Handlers
//---------------------------------------------------------------------------------

/** ImageInfo steamworks.getAchievementImageInfo(achievementName) */
int OnGetAchievementImageInfo(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the required achievement name argument.
	const char* achievementName = nullptr;
	if (lua_type(luaStatePointer, 1) == LUA_TSTRING)
	{
		achievementName = lua_tostring(luaStatePointer, 1);
	}
	if (!achievementName)
	{
		CoronaLuaError(luaStatePointer, "1st argument must be set to the achievement's unique name.");
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Fetch a handle to the requested image.
	int imageHandle = steamUserStatsPointer->GetAchievementIcon(achievementName);

	// Push the requested image information to Lua.
	// Note: Will return nil if image is not available.
	auto imageInfo = SteamImageInfo::FromImageHandle(imageHandle);
	bool wasPushed = imageInfo.PushToLua(luaStatePointer);
	if (!wasPushed)
	{
		lua_pushnil(luaStatePointer);
	}
	return 1;
}

/** AchievementInfo steamworks.getAchievementInfo(achievementName, [userSteamId]) */
int OnGetAchievementInfo(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the required achievement name argument.
	const char* achievementName = nullptr;
	if (lua_type(luaStatePointer, 1) == LUA_TSTRING)
	{
		achievementName = lua_tostring(luaStatePointer, 1);
	}
	if (!achievementName)
	{
		CoronaLuaError(luaStatePointer, "1st argument must be set to the achievement's unique name.");
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Fetch the optional steam ID of the user.
	CSteamID userSteamId;
	{
		const char* userStringId = nullptr;
		const auto luaArgumentType = lua_type(luaStatePointer, 2);
		if (luaArgumentType == LUA_TSTRING)
		{
			userStringId = lua_tostring(luaStatePointer, 2);
		}
		else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
		{
			CoronaLuaError(luaStatePointer, "2nd argument (userSteamId) is not of type string.");
			lua_pushnil(luaStatePointer);
			return 1;
		}
		if (userStringId)
		{
			uint64 integerId = 0;
			std::stringstream stringStream;
			stringStream.imbue(std::locale::classic());
			stringStream << userStringId;
			stringStream >> integerId;
			if (!stringStream.fail())
			{
				userSteamId.SetFromUint64(integerId);
			}
			if (userSteamId.IsValid() == false)
			{
				CoronaLuaError(luaStatePointer, "Given user ID is invalid: '%s'", userStringId);
				lua_pushnil(luaStatePointer);
				return 1;
			}
		}
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushnil(luaStatePointer);
		return 1;
	}
	
	// Fetch the achievement's locked/unlocked status.
	// Note: "unlockTime" is in Unix time. Can be compared with Lua's os.time() function.
	bool wasSuccessful;
	bool wasUnlocked = false;
	uint32 unlockTime = 0;
	if (userSteamId.IsValid())
	{
		wasSuccessful = steamUserStatsPointer->GetUserAchievementAndUnlockTime(
				userSteamId, achievementName, &wasUnlocked, &unlockTime);
	}
	else
	{
		wasSuccessful = steamUserStatsPointer->GetAchievementAndUnlockTime(
				achievementName, &wasUnlocked, &unlockTime);
	}
	if (!wasSuccessful)
	{
		// The above fetch failed. Likely means that the achievement name is invalid.
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Return the requested achievement information as a Lua table.
	lua_newtable(luaStatePointer);
	{
		lua_pushboolean(luaStatePointer, wasUnlocked ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "unlocked");
	}
	if (wasUnlocked)
	{
		lua_pushnumber(luaStatePointer, unlockTime);
		lua_setfield(luaStatePointer, -2, "unlockTime");
	}
	{
		auto stringResult =
				steamUserStatsPointer->GetAchievementDisplayAttribute(achievementName, "name");
		if (!stringResult || ('\0' == stringResult[0]))
		{
			stringResult = "Unknown";
		}
		lua_pushstring(luaStatePointer, stringResult);
		lua_setfield(luaStatePointer, -2, "localizedName");
	}
	{
		auto stringResult =
				steamUserStatsPointer->GetAchievementDisplayAttribute(achievementName, "desc");
		if (!stringResult || ('\0' == stringResult[0]))
		{
			stringResult = "Unknown";
		}
		lua_pushstring(luaStatePointer, stringResult);
		lua_setfield(luaStatePointer, -2, "localizedDescription");
	}
	{
		bool isHidden = false;
		auto stringResult =
				steamUserStatsPointer->GetAchievementDisplayAttribute(achievementName, "hidden");
		if (stringResult && !strcmp(stringResult, "1"))
		{
			isHidden = true;
		}
		lua_pushboolean(luaStatePointer, isHidden ? 1 : 0);
		lua_setfield(luaStatePointer, -2, "hidden");
	}
	return 1;
}

/** ImageInfo steamworks.getUserImageInfo(type, [userSteamId]) */
int OnGetUserImageInfo(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the required image type argument.
	if (lua_type(luaStatePointer, 1) != LUA_TSTRING)
	{
		CoronaLuaError(luaStatePointer, "1st argument must be set to image type's unique name.");
		lua_pushnil(luaStatePointer);
		return 1;
	}
	const char* imageTypeName = lua_tostring(luaStatePointer, 1);
	if (!imageTypeName)
	{
		imageTypeName = "";
	}
	const auto imageType = SteamUserImageType::FromCoronaStringId(imageTypeName);
	if (imageType == SteamUserImageType::kUnknown)
	{
		CoronaLuaError(luaStatePointer, "Given unknown image type name: \"%s\"", imageTypeName);
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Fetch the optional steam ID of the user.
	CSteamID userSteamId;
	{
		const char* userStringId = nullptr;
		const auto luaArgumentType = lua_type(luaStatePointer, 2);
		if (luaArgumentType == LUA_TSTRING)
		{
			userStringId = lua_tostring(luaStatePointer, 2);
		}
		else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
		{
			CoronaLuaError(luaStatePointer, "Argument (userSteamId) is not of type string.");
			lua_pushnil(luaStatePointer);
			return 1;
		}
		if (userStringId)
		{
			uint64 integerId = 0;
			std::stringstream stringStream;
			stringStream.imbue(std::locale::classic());
			stringStream << userStringId;
			stringStream >> integerId;
			if (!stringStream.fail())
			{
				userSteamId.SetFromUint64(integerId);
			}
			if (userSteamId.IsValid() == false)
			{
				CoronaLuaError(luaStatePointer, "Given user ID is invalid: '%s'", userStringId);
				lua_pushnil(luaStatePointer);
				return 1;
			}
		}
	}

	// Fetch this plugin's runtime context associated with the calling Lua state.
	auto contextPointer = (RuntimeContext*)lua_touserdata(luaStatePointer, lua_upvalueindex(1));
	if (!contextPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the Steam interfaces needed by this API call.
	auto steamUserPointer = SteamUser();
	auto steamFriendsPointer = SteamFriends();
	if (!steamUserPointer || !steamFriendsPointer)
	{
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// If we were not given a user ID, then default to the logged in user's ID.
	if (userSteamId.IsValid() == false)
	{
		userSteamId = steamUserPointer->GetSteamID();
	}

	// Fetch information about the requested user image.
	auto imageInfo = contextPointer->GetUserImageInfoFor(userSteamId, imageType);

	// Push the requested image information to Lua.
	// Note: Will return nil if image is not available.
	bool wasPushed = imageInfo.PushToLua(luaStatePointer);
	if (!wasPushed)
	{
		lua_pushnil(luaStatePointer);
	}
	return 1;
}

/** UserInfo steamworks.getUserInfo([userSteamId]) */
int OnGetUserInfo(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the optional steam ID of the user.
	CSteamID userSteamId;
	{
		const char* userStringId = nullptr;
		const auto luaArgumentType = lua_type(luaStatePointer, 1);
		if (luaArgumentType == LUA_TSTRING)
		{
			userStringId = lua_tostring(luaStatePointer, 1);
		}
		else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
		{
			CoronaLuaError(luaStatePointer, "Argument (userSteamId) is not of type string.");
			lua_pushnil(luaStatePointer);
			return 1;
		}
		if (userStringId)
		{
			uint64 integerId = 0;
			std::stringstream stringStream;
			stringStream.imbue(std::locale::classic());
			stringStream << userStringId;
			stringStream >> integerId;
			if (!stringStream.fail())
			{
				userSteamId.SetFromUint64(integerId);
			}
			if (userSteamId.IsValid() == false)
			{
				CoronaLuaError(luaStatePointer, "Given user ID is invalid: '%s'", userStringId);
				lua_pushnil(luaStatePointer);
				return 1;
			}
		}
	}

	// Fetch the Steam interfaces needed by this API call.
	auto steamUserPointer = SteamUser();
	auto steamFriendsPointer = SteamFriends();
	if (!steamUserPointer || !steamFriendsPointer)
	{
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// If we were given a user ID that matches the currently logged in user's ID, then clear it.
	if (userSteamId.IsValid())
	{
		if (userSteamId.ConvertToUint64() == steamUserPointer->GetSteamID().ConvertToUint64())
		{
			userSteamId.Clear();
		}
	}

	// If given a user ID, check if that user's info is currently cached.
	if (userSteamId.IsValid())
	{
		bool wasRequested = steamFriendsPointer->RequestUserInformation(userSteamId, true);
		if (wasRequested)
		{
			// A request was sent. This means user info is not cached.
			lua_pushnil(luaStatePointer);
			return 1;
		}
		else
		{
			// A request was not sent, meaning the user's info is already cached.
		}
	}

	// Return the request user information as a Lua table.
	lua_newtable(luaStatePointer);
	{
		// Add the user's name to the table.
		const char* userName;
		if (userSteamId.IsValid())
		{
			userName = steamFriendsPointer->GetFriendPersonaName(userSteamId);
		}
		else
		{
			userName = steamFriendsPointer->GetPersonaName();
		}
		if (!userName || ('\0' == userName[0]))
		{
			userName = "[unknown]";
		}
		lua_pushstring(luaStatePointer, userName);
		lua_setfield(luaStatePointer, -2, "name");
	}
	{
		// Add the nickname the logged in user assigned to the given user.
		// Will default to the user's name if:
		// - The logged in user has not assigned a nickname to the friend.
		// - We're fetching info for the currently logged in user.
		const char* nickname = nullptr;
		if (userSteamId.IsValid())
		{
			nickname = steamFriendsPointer->GetPlayerNickname(userSteamId);
		}
		if (nickname && (nickname[0] != '\0'))
		{
			lua_pushstring(luaStatePointer, nickname);
		}
		else
		{
			lua_pushstring(luaStatePointer, "");
		}
		lua_setfield(luaStatePointer, -2, "nickname");
	}
	{
		// Add the user's steam level to the table.
		int level;
		if (userSteamId.IsValid())
		{
			level = steamFriendsPointer->GetFriendSteamLevel(userSteamId);
		}
		else
		{
			level = steamUserPointer->GetPlayerSteamLevel();
		}
		lua_pushinteger(luaStatePointer, level);
		lua_setfield(luaStatePointer, -2, "steamLevel");
	}
	{
		// Add the user's current state/status to the table.
		EPersonaState stateIntegerId;
		if (userSteamId.IsValid())
		{
			stateIntegerId = steamFriendsPointer->GetFriendPersonaState(userSteamId);
		}
		else
		{
			stateIntegerId = steamFriendsPointer->GetPersonaState();
		}
		const char* stateName;
		switch (stateIntegerId)
		{
			case k_EPersonaStateOffline:
				stateName = "offline";
				break;
			case k_EPersonaStateOnline:
				stateName = "online";
				break;
			case k_EPersonaStateBusy:
				stateName = "busy";
				break;
			case k_EPersonaStateAway:
				stateName = "away";
				break;
			case k_EPersonaStateSnooze:
				stateName = "snooze";
				break;
			case k_EPersonaStateLookingToTrade:
				stateName = "lookingToTrade";
				break;
			case k_EPersonaStateLookingToPlay:
				stateName = "lookingToPlay";
				break;
			default:
				stateName = "unknown";
				break;
		}
		lua_pushstring(luaStatePointer, stateName);
		lua_setfield(luaStatePointer, -2, "status");
	}
	if (userSteamId.IsValid())
	{
		// Add the relationship status with the current user to the table.
		// Note: This field will be nil for the current user.
//TODO: If this is the logged-in user, should we set this to a custom "self" string instead?
		auto relationshipId = steamFriendsPointer->GetFriendRelationship(userSteamId);
		const char* relationshipName;
		switch (relationshipId)
		{
			case k_EFriendRelationshipNone:
				relationshipName = "none";
				break;
			case k_EFriendRelationshipBlocked:
				relationshipName = "blocked";
				break;
			case k_EFriendRelationshipRequestRecipient:
				relationshipName = "requestRecipient";
				break;
			case k_EFriendRelationshipFriend:
				relationshipName = "friend";
				break;
			case k_EFriendRelationshipRequestInitiator:
				relationshipName = "requestInitiator";
				break;
			case k_EFriendRelationshipIgnored:
				relationshipName = "ignored";
				break;
			case k_EFriendRelationshipIgnoredFriend:
				relationshipName = "ignoredFriend";
				break;
			case k_EFriendRelationshipSuggested_DEPRECATED:
				relationshipName = "suggested";
				break;
			default:
				relationshipName = "unknown";
				break;
		}
		lua_pushstring(luaStatePointer, relationshipName);
		lua_setfield(luaStatePointer, -2, "relationship");
	}
	return 1;
}

/** number/nil steamworks.getUserStatValue({statName="", type="", [userSteamId=""]) */
int OnGetUserStatValue(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Do not continue if the 1st argument is not a Lua table.
	if (!lua_istable(luaStatePointer, 1))
	{
		CoronaLuaError(luaStatePointer, "1st argument must be a table.");
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Fetch the required stat name.
	const char* statName = nullptr;
	const char kStatNameFieldName[] = "statName";
	lua_getfield(luaStatePointer, 1, kStatNameFieldName);
	if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
	{
		statName = lua_tostring(luaStatePointer, -1);
		if (!statName || ('\0' == statName[0]))
		{
			CoronaLuaError(luaStatePointer, "The '%s' field cannot be set to an empty string.", kStatNameFieldName);
			statName = nullptr;
		}
	}
	else
	{
		CoronaLuaError(luaStatePointer, "Table must contain a '%s' field of type string.", kStatNameFieldName);
	}
	lua_pop(luaStatePointer, 1);
	if (!statName)
	{
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Fetch the required stat type.
	SteamStatValueType valueType;
	const char kTypeFieldName[] = "type";
	lua_getfield(luaStatePointer, 1, kTypeFieldName);
	if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
	{
		valueType = SteamStatValueType::FromCoronaStringId(lua_tostring(luaStatePointer, -1));
	}
	lua_pop(luaStatePointer, 1);
	if (SteamStatValueType::kUnknown == valueType)
	{
		const char kMessage[] =
				"Table must contain a '%s' field set to either 'int', 'float', or 'averageRate'.";
		CoronaLuaError(luaStatePointer, kMessage, kTypeFieldName);
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Fetch the optional steam ID of the user.
	CSteamID userSteamId;
	{
		const char kUserSteamIdFieldName[] = "userSteamId";
		lua_getfield(luaStatePointer, 1, kUserSteamIdFieldName);
		const char* userStringId = nullptr;
		const auto luaArgumentType = lua_type(luaStatePointer, -1);
		if (luaArgumentType == LUA_TSTRING)
		{
			userStringId = lua_tostring(luaStatePointer, -1);
		}
		else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
		{
			CoronaLuaError(luaStatePointer, "The '%s' field is not of type string.", kUserSteamIdFieldName);
		}
		if (userStringId)
		{
			uint64 integerId = 0;
			std::stringstream stringStream;
			stringStream.imbue(std::locale::classic());
			stringStream << userStringId;
			stringStream >> integerId;
			if (!stringStream.fail())
			{
				userSteamId.SetFromUint64(integerId);
			}
			if (userSteamId.IsValid() == false)
			{
				CoronaLuaError(
						luaStatePointer, "Given '%s' value is invalid: '%s'", kUserSteamIdFieldName, userStringId);
			}
		}
		lua_pop(luaStatePointer, 1);
		if (userStringId && !userSteamId.IsValid())
		{
			lua_pushnil(luaStatePointer);
			return 1;
		}
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushnil(luaStatePointer);
		return 1;
	}
	
	// Fetch the user's stat value.
	bool hasIntValue = false;
	bool hasFloatValue = false;
	int32 intValue = 0;
	float floatValue = 0;
	if (SteamStatValueType::kInteger == valueType)
	{
		if (userSteamId.IsValid())
		{
			hasIntValue = steamUserStatsPointer->GetUserStat(userSteamId, statName, &intValue);
		}
		else
		{
			hasIntValue = steamUserStatsPointer->GetStat(statName, &intValue);
		}
	}
	else if ((SteamStatValueType::kFloat == valueType) || (SteamStatValueType::kAverageRate == valueType))
	{
		if (userSteamId.IsValid())
		{
			hasFloatValue = steamUserStatsPointer->GetUserStat(userSteamId, statName, &floatValue);
		}
		else
		{
			hasFloatValue = steamUserStatsPointer->GetStat(statName, &floatValue);
		}
	}
	else
	{
		const char* coronaStringId = valueType.GetCoronaStringId() ? valueType.GetCoronaStringId() : "";
		CoronaLuaError(luaStatePointer, "Unable to fetch stat value of type '%s'.", coronaStringId);
	}

	// Push the stat value as a numeric return value to Lua.
	// Note: Will return nil if stat value not found.
	if (hasIntValue)
	{
		lua_pushinteger(luaStatePointer, intValue);
	}
	else if (hasFloatValue)
	{
		lua_pushnumber(luaStatePointer, floatValue);
	}
	else
	{
		lua_pushnil(luaStatePointer);
	}
	return 1;
}

/** DisplayObject steamworks.newImageRect([parent,] imageHandle, width, height) */
int OnNewImageRect(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the optional parent GroupObject argument.
	// Note: We assume it's a GroupObject if argument is a table that has an insert() function.
	bool wasGivenParent = false;
	int luaArgumentIndex = 1;
	{
		bool isInvalidArgument = false;
		const auto luaArgumentType = lua_type(luaStatePointer, luaArgumentIndex);
		if (luaArgumentType == LUA_TTABLE)
		{
			lua_getfield(luaStatePointer, luaArgumentIndex, "insert");
			if (lua_type(luaStatePointer, -1) == LUA_TFUNCTION)
			{
				wasGivenParent = true;
				luaArgumentIndex++;
			}
			else
			{
				isInvalidArgument = true;
			}
			lua_pop(luaStatePointer, 1);
		}
		else if (luaArgumentType != LUA_TNUMBER)
		{
			isInvalidArgument = true;
		}
		if (isInvalidArgument)
		{
			CoronaLuaError(luaStatePointer, "1st argument must be an 'imageHandle' or a parent 'GroupObject'.");
			lua_pushnil(luaStatePointer);
			return 1;
		}
	}

	// Fetch the required image handle argument.
	int imageHandle = 0;
	if (lua_type(luaStatePointer, luaArgumentIndex) == LUA_TNUMBER)
	{
		imageHandle = (int)lua_tointeger(luaStatePointer, luaArgumentIndex);
	}
	else
	{
		CoronaLuaError(luaStatePointer, "Argument %d must be a numeric 'imageHandle'.", luaArgumentIndex);
		lua_pushnil(luaStatePointer);
		return 1;
	}
	luaArgumentIndex++;

	// Fetch the required content width argument.
	double contentWidth = 0;
	if (lua_type(luaStatePointer, luaArgumentIndex) == LUA_TNUMBER)
	{
		contentWidth = lua_tonumber(luaStatePointer, luaArgumentIndex);
	}
	else
	{
		CoronaLuaError(luaStatePointer, "Argument %d must be a numeric content width.", luaArgumentIndex);
		lua_pushnil(luaStatePointer);
		return 1;
	}
	luaArgumentIndex++;

	// Fetch the required content height argument.
	double contentHeight = 0;
	if (lua_type(luaStatePointer, luaArgumentIndex) == LUA_TNUMBER)
	{
		contentHeight = lua_tonumber(luaStatePointer, luaArgumentIndex);
	}
	else
	{
		CoronaLuaError(luaStatePointer, "Argument %d must be a numeric content height.", luaArgumentIndex);
		lua_pushnil(luaStatePointer);
		return 1;
	}
	luaArgumentIndex++;

	// Do not continue if given an invalid image handle.
	auto imageInfo = SteamImageInfo::FromImageHandle(imageHandle);
	if (imageInfo.IsNotValid())
	{
		CoronaLuaWarning(luaStatePointer, "Given invalid image handle: %d", imageHandle);
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Copy the Steam image to a new Corona "TextureResourceExternal" object.
	int textureCount = SteamImageWrapper::PushTexture(luaStatePointer, imageHandle);
	if (textureCount != 1)
	{
		CoronaLuaWarning(luaStatePointer, "Failed to generate texture for image handle: %d", imageHandle);
		if (textureCount > 0)
		{
			lua_pop(luaStatePointer, textureCount);
		}
		lua_pushnil(luaStatePointer);
		return 1;
	}
	int textureIndex = lua_gettop(luaStatePointer);

	// Create a new DisplayObject filled with texture loaded above.
	bool wasDisplayObjectCreated = false;
	lua_getglobal(luaStatePointer, "display");
	if (lua_istable(luaStatePointer, -1))
	{
		lua_getfield(luaStatePointer, -1, "newImageRect");
		if (lua_isfunction(luaStatePointer, -1))
		{
			if (wasGivenParent)
			{
				lua_pushvalue(luaStatePointer, 1);
			}
			lua_getfield(luaStatePointer, textureIndex, "filename");
			lua_getfield(luaStatePointer, textureIndex, "baseDir");
			lua_pushnumber(luaStatePointer, contentWidth);
			lua_pushnumber(luaStatePointer, contentHeight);
			int argumentCount = (wasGivenParent ? 1 : 0) + 4;
			CoronaLuaDoCall(luaStatePointer, argumentCount, 1);
			if (lua_type(luaStatePointer, -1) == LUA_TTABLE)
			{
				// Display object was successfully created.
				// Move the display object to where the texture object is currently located on the Lua stack.
				// This moves the texture object (and everything above it) up on the Lua stack.
				wasDisplayObjectCreated = true;
				lua_insert(luaStatePointer, textureIndex);
				textureIndex++;
			}
			else
			{
				lua_pop(luaStatePointer, 1);
			}
		}
		else
		{
			lua_pop(luaStatePointer, 1);
		}
	}
	lua_pop(luaStatePointer, 1);

	// Release the texture object's reference to the loaded image.
	lua_getfield(luaStatePointer, textureIndex, "releaseSelf");
	if (lua_type(luaStatePointer, -1) == LUA_TFUNCTION)
	{
		lua_pushvalue(luaStatePointer, textureIndex);
		int callResultCode = CoronaLuaDoCall(luaStatePointer, 1, 0);
		if (callResultCode)
		{
			// Failed to call function. Pop off the Lua error message.
			lua_pop(luaStatePointer, 1);
		}
	}
	else
	{
		lua_pop(luaStatePointer, 1);
	}

	// Pop the texture object off of the stack. We're done with it.
	lua_pop(luaStatePointer, 1);

	// At this point, the created DisplayObject should be at the top of the stack, to be returnd to Lua.
	// If not created, then push and return nil.
	if (!wasDisplayObjectCreated)
	{
		CoronaLuaWarning(luaStatePointer, "Failed to generate DisplayObject for image handle: %d", imageHandle);
		lua_pushnil(luaStatePointer);
	}
	return 1;
}

/** TextureResourceExternal steamworks.newTexture(imageHandle) */
int OnNewTexture(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the required image handle argument.
	int imageHandle = 0;
	if (lua_type(luaStatePointer, 1) == LUA_TNUMBER)
	{
		imageHandle = (int)lua_tointeger(luaStatePointer, 1);
	}
	else
	{
		CoronaLuaError(luaStatePointer, "1st argument must be a numeric 'imageHandle'.");
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Do not continue if given an invalid handle.
	auto imageInfo = SteamImageInfo::FromImageHandle(imageHandle);
	if (imageInfo.IsNotValid())
	{
		lua_pushnil(luaStatePointer);
		return 1;
	}

	// Copy the Steam image to a new Corona "TextureResourceExternal" object and return it.
	return SteamImageWrapper::PushTexture(luaStatePointer, imageHandle);
}

/** bool steamworks.requestActivePlayerCount(listener) */
int OnRequestActivePlayerCount(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Do not continue if the 1st argument is not a Lua function.
	if (!lua_isfunction(luaStatePointer, 1))
	{
		CoronaLuaError(luaStatePointer, "1st argument must be a Lua function.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch this plugin's runtime context associated with the calling Lua state.
	auto contextPointer = (RuntimeContext*)lua_touserdata(luaStatePointer, lua_upvalueindex(1));
	if (!contextPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the number of active players for this application.
	auto resultHandle = steamUserStatsPointer->GetNumberOfCurrentPlayers();

	// Set up the given Lua function to receive the result of the above async operation.
	RuntimeContext::EventHandlerSettings settings{};
	settings.LuaStatePointer = luaStatePointer;
	settings.LuaFunctionStackIndex = 1;
	settings.SteamCallResultHandle = resultHandle;
	bool wasSuccessful = contextPointer->AddEventHandlerFor
			<NumberOfCurrentPlayers_t, DispatchNumberOfCurrentPlayersEventTask>(settings);

	// Return true to Lua if the above async operation was successfully started/executed.
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/**
	bool steamworks.requestLeaderboardEntries(
	{
		leaderboardName="", listener=myListener [,playerScope=""] [,startIndex=x, endIndex=y]
	})
 */
int OnRequestLeaderboardEntries(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch this plugin's runtime context associated with the calling Lua state.
	auto contextPointer = (RuntimeContext*)lua_touserdata(luaStatePointer, lua_upvalueindex(1));
	if (!contextPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Do not continue if the 1st argument is not a Lua table.
	if (!lua_istable(luaStatePointer, 1))
	{
		CoronaLuaError(luaStatePointer, "1st argument must be a table.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the required leaderboard name from the the Lua table.
	const char* leaderboardName = nullptr;
	{
		const char kLeaderboardNameFieldName[] = "leaderboardName";
		lua_getfield(luaStatePointer, 1, kLeaderboardNameFieldName);
		if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
		{
			leaderboardName = lua_tostring(luaStatePointer, -1);
			if (!leaderboardName || ('\0' == leaderboardName[0]))
			{
				const char* kMessage = "The '%s' field cannot be set to an empty string.";
				CoronaLuaError(luaStatePointer, kMessage, kLeaderboardNameFieldName);
				leaderboardName = nullptr;
			}
		}
		else
		{
			const char* kMessage = "Table must contain a '%s' field of type string.";
			CoronaLuaError(luaStatePointer, kMessage, kLeaderboardNameFieldName);
		}
		lua_pop(luaStatePointer, 1);
		if (!leaderboardName)
		{
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Fetch the optional player scope from the Lua table.
	ELeaderboardDataRequest playerScope = k_ELeaderboardDataRequestGlobal;
	{
		bool hasScopeField = false;
		const char* scopeName = nullptr;
		lua_getfield(luaStatePointer, 1, "playerScope");
		const auto luaValueType = lua_type(luaStatePointer, -1);
		if ((luaValueType != LUA_TNONE) && (luaValueType != LUA_TNIL))
		{
			hasScopeField = true;
			if (luaValueType == LUA_TSTRING)
			{
				scopeName = lua_tostring(luaStatePointer, -1);
			}
			else
			{
				CoronaLuaError(luaStatePointer, "The 'playerScope' field is not of type string.");
			}
		}
		lua_pop(luaStatePointer, 1);
		if (hasScopeField)
		{
			if (scopeName)
			{
				std::string lowercaseScopeName(scopeName);
				std::transform(
						lowercaseScopeName.begin(), lowercaseScopeName.end(), lowercaseScopeName.begin(), ::tolower);
				if (!strcmp(lowercaseScopeName.c_str(), "global"))
				{
					playerScope = k_ELeaderboardDataRequestGlobal;
				}
				else if (!strcmp(lowercaseScopeName.c_str(), "globalarounduser"))
				{
					playerScope = k_ELeaderboardDataRequestGlobalAroundUser;
				}
				else if (!strcmp(lowercaseScopeName.c_str(), "friendsonly"))
				{
					playerScope = k_ELeaderboardDataRequestFriends;
				}
				else
				{
					CoronaLuaError(luaStatePointer, "Given unknown playerScope name '%s'", scopeName);
					scopeName = nullptr;
				}
			}
			if (!scopeName)
			{
				lua_pushboolean(luaStatePointer, 0);
				return 1;
			}
		}
	}

	// Fetch the optional "startIndex" and "endIndex" entry range from the Lua table.
	// Note 1: Indexing is 1-based on Steam, just like Lua.
	// Note 2: Steam ignores the index range when fetching entries for "FriendsOnly" player scope.
	int rangeStartIndex = 0;
	int rangeEndIndex = 0;
	if (playerScope != k_ELeaderboardDataRequestFriends)
	{
		// Fetch the range index fields.
		bool hasRangeStartIndex = false;
		bool hasRangeEndIndex = false;
		{
			bool hasError = false;
			lua_getfield(luaStatePointer, 1, "startIndex");
			const auto luaValueType = lua_type(luaStatePointer, -1);
			if (luaValueType == LUA_TNUMBER)
			{
				rangeStartIndex = (int)lua_tointeger(luaStatePointer, -1);
				hasRangeStartIndex = true;
			}
			else if ((luaValueType != LUA_TNIL) && (luaValueType != LUA_TNONE))
			{
				CoronaLuaError(luaStatePointer, "The 'startIndex' field must be of type number.");
				hasError = true;
			}
			lua_pop(luaStatePointer, 1);
			if (hasError)
			{
				lua_pushboolean(luaStatePointer, 0);
				return 1;
			}
		}
		{
			bool hasError = false;
			lua_getfield(luaStatePointer, 1, "endIndex");
			const auto luaValueType = lua_type(luaStatePointer, -1);
			if (luaValueType == LUA_TNUMBER)
			{
				rangeEndIndex = (int)lua_tointeger(luaStatePointer, -1);
				hasRangeEndIndex = true;
			}
			else if ((luaValueType != LUA_TNIL) && (luaValueType != LUA_TNONE))
			{
				CoronaLuaError(luaStatePointer, "The 'endIndex' field must be of type number.");
				hasError = true;
			}
			lua_pop(luaStatePointer, 1);
			if (hasError)
			{
				lua_pushboolean(luaStatePointer, 0);
				return 1;
			}
		}

		// Validate the fetch indexes, if acquired.
		if (hasRangeStartIndex != hasRangeEndIndex)
		{
			// We do not allow only 1 index field to be provided.
			// As in, the developer must provide both start/end index fields or neither.
			if (hasRangeStartIndex)
			{
				CoronaLuaError(luaStatePointer, "The 'endIndex' field is missing.");
			}
			else
			{
				CoronaLuaError(luaStatePointer, "The 'startIndex' field is missing.");
			}
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
		else if (hasRangeStartIndex && hasRangeEndIndex)
		{
			// Both start/end index fields were provided. Make sure they're valid.
			if ((k_ELeaderboardDataRequestGlobal == playerScope) && (rangeStartIndex < 1))
			{
				// Floor start index to 1 (the minimum allowed) for global entry lookup.
				rangeStartIndex = 1;
			}
			if (rangeEndIndex < rangeStartIndex)
			{
				// Never let the end index be lower than the start index.
				rangeEndIndex = rangeStartIndex;
			}
		}
		else
		{
			// Index fields we're not provided. Set them to their defaults based on player scope setting.
			if (k_ELeaderboardDataRequestGlobal == playerScope)
			{
				// We use absolute indexes when fetching with a "Global" scope.
				// This means top score is index 1. Indexes less than 1 are invalid.
				rangeStartIndex = 1;
				rangeEndIndex = 25;
			}
			else if (k_ELeaderboardDataRequestGlobalAroundUser == playerScope)
			{
				// Indexes are relative to logged in user for "GlobalAroundUser" scope.
				// This means index zero is the logged in user and negative indexes are users with higher scores.
				rangeStartIndex = -12;
				rangeEndIndex = 12;
			}
		}
	}

	// Do not continue if the required Lua listener is not in the table.
	{
		lua_getfield(luaStatePointer, 1, "listener");
		bool hasListener = lua_isfunction(luaStatePointer, -1) ? true : false;
		lua_pop(luaStatePointer, 1);
		if (!hasListener)
		{
			CoronaLuaError(luaStatePointer, "Table must contain a 'listener' field of type function.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Attempt to fetch the leaderboard's handle by its unique name.
	// Note: These handles are cached by the RuntimeContext when a Lua requestLeaderboardInfo() function gets called.
	SteamLeaderboard_t leaderboardHandle = contextPointer->GetCachedLeaderboardHandleByName(leaderboardName);

	// Do not continue if the leaderboard handle has not been cached yet.
	// Instead, do an async request for the handle from Steam and automatically try again later.
	if (0 == leaderboardHandle)
	{
		// Create a lambda to be called by Lua when a leaderboard handle has been received from Steam.
		// This lambda will then call the requestLeaderboardEntries() function again if successful.
		// Note: This lambda is not allowed to use a capture list in order to pass it as a function pointer to Lua.
		auto lambda = [](lua_State* luaStatePointer)->int
		{
			// Verify that the first argument is a Lua event table.
			if (!luaStatePointer || !lua_istable(luaStatePointer, 1))
			{
				return 0;
			}

			// Fetch a pointer to the runtime context associated with the given Lua state.
			// If it's no longer available, then the Corona runtime is terminating. We must abort.
			auto contextPointer = RuntimeContext::GetInstanceBy(luaStatePointer);
			if (!contextPointer)
			{
				return 0;
			}

			// Check if we've successfully fetched the leaderboard's handle.
			bool hasSucceeded = false;
			lua_getfield(luaStatePointer, 1, "isError");
			if (lua_type(luaStatePointer, -1) == LUA_TBOOLEAN)
			{
				hasSucceeded = lua_toboolean(luaStatePointer, -1) ? false : true;
			}
			lua_pop(luaStatePointer, 1);

			// Fetch the leaderboard name.
			const char* leaderboardName = nullptr;
			lua_getfield(luaStatePointer, 1, "leaderboardName");
			if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
			{
				leaderboardName = lua_tostring(luaStatePointer, -1);
			}
			lua_pop(luaStatePointer, 1);

			// Fetch a Lua stack index to the original requestLeaderboardEntries() argument.
			// This is needed in order to re-send the request to Steam.
			int luaSettingsTableStackIndex = lua_upvalueindex(1);
			if (!lua_istable(luaStatePointer, luaSettingsTableStackIndex))
			{
				hasSucceeded = false;
			}

			// Re-send the request if we've successfully fetch the leaderboard handle.
			if (hasSucceeded)
			{
				hasSucceeded = false;
				int luaStackCount = lua_gettop(luaStatePointer);
				PushPluginTableTo(luaStatePointer);
				if (lua_istable(luaStatePointer, -1))
				{
					lua_getfield(luaStatePointer, -1, "requestLeaderboardEntries");
					if (lua_isfunction(luaStatePointer, -1))
					{
						lua_pushvalue(luaStatePointer, luaSettingsTableStackIndex);
						CoronaLuaDoCall(luaStatePointer, 1, 1);
						if (lua_type(luaStatePointer, -1) == LUA_TBOOLEAN)
						{
							hasSucceeded = lua_toboolean(luaStatePointer, -1) ? true : false;
						}
					}
				}
				lua_settop(luaStatePointer, luaStackCount);
			}

			// Dispatch a Lua error event if:
			// - We've failed to acquire the leaderboard handle.
			// - We did fetch the handle, but failed to re-send the request above.
			if (!hasSucceeded)
			{
				// Fetch an index to the Lua listener originally given to the Lua requestLeaderboardEntries() function.
				int luaListenerStackIndex = 0;
				if (lua_istable(luaStatePointer, luaSettingsTableStackIndex))
				{
					lua_getfield(luaStatePointer, luaSettingsTableStackIndex, "listener");
					if (lua_isfunction(luaStatePointer, -1))
					{
						luaListenerStackIndex = lua_gettop(luaStatePointer);
					}
					else
					{
						lua_pop(luaStatePointer, 1);
					}
				}

				// Dispatch an event to the Lua listener.
				if (luaListenerStackIndex)
				{
					// Create a Lua event dispatcher and add the Lua listener to it.
					auto luaEventDispatcherPointer = std::make_shared<LuaEventDispatcher>(luaStatePointer);
					luaEventDispatcherPointer->AddEventListener(
							luaStatePointer,
							DispatchLeaderboardScoresDownloadedEventTask::kLuaEventName,
							luaListenerStackIndex);

					// Dispatch the event to the Lua listener.
					LeaderboardScoresDownloaded_t eventData{};
					if (contextPointer)
					{
						eventData.m_hSteamLeaderboard = contextPointer->GetCachedLeaderboardHandleByName(leaderboardName);
					}
					DispatchLeaderboardScoresDownloadedEventTask task;
					task.SetLuaEventDispatcher(luaEventDispatcherPointer);
					task.AcquireEventDataFrom(eventData);
					task.SetLeaderboardName(leaderboardName);
					task.SetHadIOFailure(true);
					task.Execute();

					// Pop the Lua listener off of the stack.
					lua_pop(luaStatePointer, 1);
				}
			}
			return 0;
		};

		// Push the above callback to Lua.
		// We also store the requestLeaderboardEntries() function's argument as an upvalue so that
		// the above callback (when invoked) can call this function again with that same argument.
		lua_pushvalue(luaStatePointer, 1);
		lua_pushcclosure(luaStatePointer, lambda, 1);

		// Request the leaderboard handle from Steam.
		// Set up the above callback to receive the result of this request.
		auto resultHandle = steamUserStatsPointer->FindLeaderboard(leaderboardName);
		RuntimeContext::EventHandlerSettings settings{};
		settings.LuaStatePointer = luaStatePointer;
		settings.LuaFunctionStackIndex = lua_gettop(luaStatePointer);
		settings.SteamCallResultHandle = resultHandle;
		settings.QueuingEventTaskCallback = CreateQueueingLeaderboardEventTaskCallbackWith(leaderboardName);
		bool wasSuccessful = contextPointer->AddEventHandlerFor
				<LeaderboardFindResult_t, DispatchLeaderboardFindResultEventTask>(settings);

		// Pop the above Lua closure off of the stack.
		lua_pop(luaStatePointer, 1);

		// Return true to Lua if we've successfully sent the above request.
		lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
		return 1;
	}

	// Push the Lua listener function from the Lua table to the top of the stack.
	// Note: We've already determined that it exists in the table up above.
	lua_getfield(luaStatePointer, 1, "listener");
	int luaFunctionStackIndex = lua_gettop(luaStatePointer);

	// Send the leaderboard entry download request to Steam.
	auto resultHandle = steamUserStatsPointer->DownloadLeaderboardEntries(
			leaderboardHandle, playerScope, rangeStartIndex, rangeEndIndex);

	// Set up the given Lua function to receive the result of the above async operation.
	RuntimeContext::EventHandlerSettings settings{};
	settings.LuaStatePointer = luaStatePointer;
	settings.LuaFunctionStackIndex = luaFunctionStackIndex;
	settings.SteamCallResultHandle = resultHandle;
	settings.QueuingEventTaskCallback = CreateQueueingLeaderboardEventTaskCallbackWith(leaderboardName);
	bool wasSuccessful = contextPointer->AddEventHandlerFor
			<LeaderboardScoresDownloaded_t, DispatchLeaderboardScoresDownloadedEventTask>(settings);

	// Pop the Lua listener off of the stack.
	lua_pop(luaStatePointer, 1);

	// Return true to Lua if the above async operation was successfully started/executed.
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/** bool steamworks.requestLeaderboardInfo({leaderboardName="", listener=myListener}) */
int OnRequestLeaderboardInfo(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch this plugin's runtime context associated with the calling Lua state.
	auto contextPointer = (RuntimeContext*)lua_touserdata(luaStatePointer, lua_upvalueindex(1));
	if (!contextPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Do not continue if the 1st argument is not a Lua table.
	if (!lua_istable(luaStatePointer, 1))
	{
		CoronaLuaError(luaStatePointer, "1st argument must be a table.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the leaderboard name from the the Lua table.
	const char* leaderboardName = nullptr;
	{
		const char kLeaderboardNameFieldName[] = "leaderboardName";
		lua_getfield(luaStatePointer, 1, kLeaderboardNameFieldName);
		if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
		{
			leaderboardName = lua_tostring(luaStatePointer, -1);
			if (!leaderboardName || ('\0' == leaderboardName[0]))
			{
				const char* kMessage = "The '%s' field cannot be set to an empty string.";
				CoronaLuaError(luaStatePointer, kMessage, kLeaderboardNameFieldName);
				leaderboardName = nullptr;
			}
		}
		else
		{
			const char* kMessage = "Table must contain a '%s' field of type string.";
			CoronaLuaError(luaStatePointer, kMessage, kLeaderboardNameFieldName);
		}
		lua_pop(luaStatePointer, 1);
		if (!leaderboardName)
		{
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Fetch the Lua listener function from the Lua table and push it to the top of the stack.
	int luaFunctionStackIndex = 0;
	lua_getfield(luaStatePointer, 1, "listener");
	if (lua_isfunction(luaStatePointer, -1))
	{
		luaFunctionStackIndex = lua_gettop(luaStatePointer);
	}
	else
	{
		CoronaLuaError(luaStatePointer, "Table must contain a 'listener' field of type function.");
		lua_pop(luaStatePointer, 1);
	}
	if (!luaFunctionStackIndex)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the number of active players for this application.
	auto resultHandle = steamUserStatsPointer->FindLeaderboard(leaderboardName);

	// Set up the given Lua function to receive the result of the above async operation.
	RuntimeContext::EventHandlerSettings settings{};
	settings.LuaStatePointer = luaStatePointer;
	settings.LuaFunctionStackIndex = luaFunctionStackIndex;
	settings.SteamCallResultHandle = resultHandle;
	settings.QueuingEventTaskCallback = CreateQueueingLeaderboardEventTaskCallbackWith(leaderboardName);
	bool wasSuccessful = contextPointer->AddEventHandlerFor
			<LeaderboardFindResult_t, DispatchLeaderboardFindResultEventTask>(settings);

	// Pop the Lua listener off of the stack.
	lua_pop(luaStatePointer, 1);

	// Return true to Lua if the above async operation was successfully started/executed.
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/** bool steamworks.requestSetHighScore({leaderboardName="", value=x, listener=myListener}) */
int OnRequestSetHighScore(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch this plugin's runtime context associated with the calling Lua state.
	auto contextPointer = (RuntimeContext*)lua_touserdata(luaStatePointer, lua_upvalueindex(1));
	if (!contextPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Do not continue if the 1st argument is not a Lua table.
	if (!lua_istable(luaStatePointer, 1))
	{
		CoronaLuaError(luaStatePointer, "1st argument must be a table.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the required leaderboard name from the the Lua table.
	const char* leaderboardName;
	{
		const char kLeaderboardNameFieldName[] = "leaderboardName";
		lua_getfield(luaStatePointer, 1, kLeaderboardNameFieldName);
		if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
		{
			auto stringPointer = lua_tostring(luaStatePointer, -1);
			if (stringPointer && (stringPointer[0] != '\0'))
			{
				leaderboardName = stringPointer;
			}
			else
			{
				const char* kMessage = "The '%s' field cannot be set to an empty string.";
				CoronaLuaError(luaStatePointer, kMessage, kLeaderboardNameFieldName);
			}
		}
		else
		{
			const char* kMessage = "Table must contain a '%s' field of type string.";
			CoronaLuaError(luaStatePointer, kMessage, kLeaderboardNameFieldName);
		}
		lua_pop(luaStatePointer, 1);
		if (!leaderboardName)
		{
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Fetch the required high score value from the Lua table.
	int scoreValue = 0;
	{
		bool hasValue = false;
		const char kValueFieldName[] = "value";
		lua_getfield(luaStatePointer, 1, kValueFieldName);
		if (lua_type(luaStatePointer, -1) == LUA_TNUMBER)
		{
			scoreValue = (int)lua_tointeger(luaStatePointer, -1);
			hasValue = true;
		}
		else
		{
			CoronaLuaError(luaStatePointer, "Table must contain a '%s' field of type number.", kValueFieldName);
		}
		lua_pop(luaStatePointer, 1);
		if (!hasValue)
		{
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Do not continue if the required Lua listener is not in the table.
	{
		lua_getfield(luaStatePointer, 1, "listener");
		bool hasListener = lua_isfunction(luaStatePointer, -1) ? true : false;
		lua_pop(luaStatePointer, 1);
		if (!hasListener)
		{
			CoronaLuaError(luaStatePointer, "Table must contain a 'listener' field of type function.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Attempt to fetch the leaderboard's handle by its unique name.
	// Note: These handles are cached by the RuntimeContext when a Lua requestLeaderboardInfo() function gets called.
	SteamLeaderboard_t leaderboardHandle = contextPointer->GetCachedLeaderboardHandleByName(leaderboardName);

	// Do not continue if the leaderboard handle has not been cached yet.
	// Instead, do an async request for the handle from Steam and automatically try again later.
	if (0 == leaderboardHandle)
	{
		// Create a lambda to be called by Lua when a leaderboard handle has been received from Steam.
		// This lambda will then call the requestSetHigherScore() function again if successful.
		// Note: This lambda is not allowed to use a capture list in order to pass it as a function pointer to Lua.
		auto lambda = [](lua_State* luaStatePointer)->int
		{
			// Verify that the first argument is a Lua event table.
			if (!luaStatePointer || !lua_istable(luaStatePointer, 1))
			{
				return 0;
			}

			// Fetch a pointer to the runtime context associated with the given Lua state.
			// If it's no longer available, then the Corona runtime is terminating. We must abort.
			auto contextPointer = RuntimeContext::GetInstanceBy(luaStatePointer);
			if (!contextPointer)
			{
				return 0;
			}

			// Check if we've successfully fetched the leaderboard's handle.
			bool hasSucceeded = false;
			lua_getfield(luaStatePointer, 1, "isError");
			if (lua_type(luaStatePointer, -1) == LUA_TBOOLEAN)
			{
				hasSucceeded = lua_toboolean(luaStatePointer, -1) ? false : true;
			}
			lua_pop(luaStatePointer, 1);

			// Fetch the leaderboard name.
			const char* leaderboardName = nullptr;
			lua_getfield(luaStatePointer, 1, "leaderboardName");
			if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
			{
				leaderboardName = lua_tostring(luaStatePointer, -1);
			}
			lua_pop(luaStatePointer, 1);

			// Fetch a Lua stack index to the original requestSetHighScore() argument.
			// This is needed in order to re-send the request to Steam.
			int luaSettingsTableStackIndex = lua_upvalueindex(1);
			if (!lua_istable(luaStatePointer, luaSettingsTableStackIndex))
			{
				hasSucceeded = false;
			}

			// Re-send the request if we've successfully fetch the leaderboard handle.
			if (hasSucceeded)
			{
				hasSucceeded = false;
				int luaStackCount = lua_gettop(luaStatePointer);
				PushPluginTableTo(luaStatePointer);
				if (lua_istable(luaStatePointer, -1))
				{
					lua_getfield(luaStatePointer, -1, "requestSetHighScore");
					if (lua_isfunction(luaStatePointer, -1))
					{
						lua_pushvalue(luaStatePointer, luaSettingsTableStackIndex);
						CoronaLuaDoCall(luaStatePointer, 1, 1);
						if (lua_type(luaStatePointer, -1) == LUA_TBOOLEAN)
						{
							hasSucceeded = lua_toboolean(luaStatePointer, -1) ? true : false;
						}
					}
				}
				lua_settop(luaStatePointer, luaStackCount);
			}

			// Dispatch a Lua error event if:
			// - We've failed to acquire the leaderboard handle.
			// - We did fetch the handle, but failed to re-send the request above.
			if (!hasSucceeded)
			{
				// Fetch an index to the Lua listener originall given to the Lua requestSetHighScore() function.
				int luaListenerStackIndex = 0;
				if (lua_istable(luaStatePointer, luaSettingsTableStackIndex))
				{
					lua_getfield(luaStatePointer, luaSettingsTableStackIndex, "listener");
					if (lua_isfunction(luaStatePointer, -1))
					{
						luaListenerStackIndex = lua_gettop(luaStatePointer);
					}
					else
					{
						lua_pop(luaStatePointer, 1);
					}
				}

				// Dispatch an event to the Lua listener.
				if (luaListenerStackIndex)
				{
					// Create a Lua event dispatcher and add the Lua listener to it.
					auto luaEventDispatcherPointer = std::make_shared<LuaEventDispatcher>(luaStatePointer);
					luaEventDispatcherPointer->AddEventListener(
							luaStatePointer,
							DispatchLeaderboardScoreUploadEventTask::kLuaEventName,
							luaListenerStackIndex);

					// Dispatch the event to the Lua listener.
					LeaderboardScoreUploaded_t eventData{};
					if (contextPointer)
					{
						eventData.m_hSteamLeaderboard = contextPointer->GetCachedLeaderboardHandleByName(leaderboardName);
					}
					DispatchLeaderboardScoreUploadEventTask task;
					task.SetLuaEventDispatcher(luaEventDispatcherPointer);
					task.AcquireEventDataFrom(eventData);
					task.SetLeaderboardName(leaderboardName);
					task.SetHadIOFailure(true);
					task.Execute();

					// Pop the Lua listener off of the stack.
					lua_pop(luaStatePointer, 1);
				}
			}
			return 0;
		};

		// Push the above callback to Lua.
		// We also store the requestSetHighScore() function's argument as an upvalue so that
		// the above callback (when invoked) can call this function again with that same argument.
		lua_pushvalue(luaStatePointer, 1);
		lua_pushcclosure(luaStatePointer, lambda, 1);

		// Request the leaderboard handle from Steam.
		// Set up the above callback to receive the result of this request.
		auto resultHandle = steamUserStatsPointer->FindLeaderboard(leaderboardName);
		RuntimeContext::EventHandlerSettings settings{};
		settings.LuaStatePointer = luaStatePointer;
		settings.LuaFunctionStackIndex = lua_gettop(luaStatePointer);
		settings.SteamCallResultHandle = resultHandle;
		settings.QueuingEventTaskCallback = CreateQueueingLeaderboardEventTaskCallbackWith(leaderboardName);
		bool wasSuccessful = contextPointer->AddEventHandlerFor
				<LeaderboardFindResult_t, DispatchLeaderboardFindResultEventTask>(settings);

		// Pop the above Lua closure off of the stack.
		lua_pop(luaStatePointer, 1);

		// Return true to Lua if we've successfully sent the above request.
		lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
		return 1;
	}

	// Push the table's Lua listener function to the top of the stack.
	// Note: We've already determined that it exists in the table up above.
	lua_getfield(luaStatePointer, 1, "listener");
	int luaFunctionStackIndex = lua_gettop(luaStatePointer);

	// Request Steam to update its leaderboard with the given score.
	auto resultHandle = steamUserStatsPointer->UploadLeaderboardScore(
			leaderboardHandle, k_ELeaderboardUploadScoreMethodKeepBest, scoreValue, nullptr, 0);

	// Set up the given Lua function to receive the result of the above async operation.
	RuntimeContext::EventHandlerSettings settings{};
	settings.LuaStatePointer = luaStatePointer;
	settings.LuaFunctionStackIndex = luaFunctionStackIndex;
	settings.SteamCallResultHandle = resultHandle;
	settings.QueuingEventTaskCallback = CreateQueueingLeaderboardEventTaskCallbackWith(leaderboardName);
	bool wasSuccessful = contextPointer->AddEventHandlerFor
			<LeaderboardScoreUploaded_t, DispatchLeaderboardScoreUploadEventTask>(settings);

	// Pop the Lua listener off of the stack, if provided.
	if (luaFunctionStackIndex)
	{
		lua_pop(luaStatePointer, 1);
	}

	// Return true to Lua if the above async operation was successfully started/executed.
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/** bool steamworks.requestUserProgress([userSteamId]) */
int OnRequestUserProgress(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the optional steam ID of the user.
	CSteamID userSteamId;
	{
		const char* userStringId = nullptr;
		const auto luaArgumentType = lua_type(luaStatePointer, 1);
		if (luaArgumentType == LUA_TSTRING)
		{
			userStringId = lua_tostring(luaStatePointer, 1);
		}
		else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
		{
			CoronaLuaError(luaStatePointer, "Argument (userSteamId) is not of type string.");
			lua_pushnil(luaStatePointer);
			return 1;
		}
		if (userStringId)
		{
			uint64 integerId = 0;
			std::stringstream stringStream;
			stringStream.imbue(std::locale::classic());
			stringStream << userStringId;
			stringStream >> integerId;
			if (!stringStream.fail())
			{
				userSteamId.SetFromUint64(integerId);
			}
			if (userSteamId.IsValid() == false)
			{
				CoronaLuaError(luaStatePointer, "Given user ID is invalid: '%s'", userStringId);
				lua_pushnil(luaStatePointer);
				return 1;
			}
		}
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Request user stats and achievement info.
	// Plugin will dispatch global "userProgressUpdate" event to Lua when a response has been received.
	bool wasSuccessful;
	if (userSteamId.IsValid())
	{
		// Request data for the given user ID.
		// Note: We can ignore the returned call result handle since the RuntimeContext's
		//       global "UserStatsReceived_t" event handler can receive this request's data.
		auto resultHandle = steamUserStatsPointer->RequestUserStats(userSteamId);
		wasSuccessful = (resultHandle != k_uAPICallInvalid);
	}
	else
	{
		// Request data for the currently logged in user.
		wasSuccessful = steamUserStatsPointer->RequestCurrentStats();
	}
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/** bool steamworks.resetUserProgress() */
int OnResetUserProgress(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Clear the user's stats and achievements.
	bool wasSuccessful = false;
	auto steamUserStatsPointer = SteamUserStats();
	if (steamUserStatsPointer)
	{
		wasSuccessful = steamUserStatsPointer->ResetAllStats(true);
	}
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/** bool steamworks.resetUserStats() */
int OnResetUserStats(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Clear the user's stats.
	bool wasSuccessful = false;
	auto steamUserStatsPointer = SteamUserStats();
	if (steamUserStatsPointer)
	{
		wasSuccessful = steamUserStatsPointer->ResetAllStats(false);
	}
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/** bool steamworks.setAchievementProgress(achievementName, value, maxValue) */
int OnSetAchievementProgress(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the achievement name.
	const char* achievementName = nullptr;
	if (lua_type(luaStatePointer, 1) == LUA_TSTRING)
	{
		achievementName = lua_tostring(luaStatePointer, 1);
	}
	if (!achievementName)
	{
		CoronaLuaError(luaStatePointer, "1st argument must be set to the achievement's unique name.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the progress value argument.
	uint32 currentProgressValue;
	{
		const auto luaArgumentType = lua_type(luaStatePointer, 2);
		if (luaArgumentType == LUA_TNUMBER)
		{
			auto intValue = lua_tointeger(luaStatePointer, 2);
			if (intValue < 0)
			{
				intValue = 0;
			}
            currentProgressValue = (uint32)intValue;
		}
		else if (luaArgumentType == LUA_TNONE)
		{
			CoronaLuaError(
					luaStatePointer,
					"2nd argument is missing. Expected current progress value of type number.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
		else
		{
			CoronaLuaError(luaStatePointer, "2nd argument is not of type number.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Fetch the maxium progress value argument.
	uint32 maxProgressValue;
	{
		const auto luaArgumentType = lua_type(luaStatePointer, 3);
		if (luaArgumentType == LUA_TNUMBER)
		{
			auto intValue = lua_tointeger(luaStatePointer, 3);
			if (intValue < 0)
			{
				intValue = 0;
			}
            maxProgressValue = (uint32)intValue;
		}
		else if (luaArgumentType == LUA_TNONE)
		{
			CoronaLuaError(
					luaStatePointer,
					"3rd argument is missing. Expected max progress value of type number.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
		else
		{
			CoronaLuaError(luaStatePointer, "3rd argument is not of type number.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Fetch the Steam object used to access achievements.
	// Will return false to Lua if not currently connected to Steam client.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Update the achievement.
	bool wasSuccessful;
	if (currentProgressValue < maxProgressValue)
	{
		// Update the given achievement's progress.
		wasSuccessful = steamUserStatsPointer->IndicateAchievementProgress(
				achievementName, currentProgressValue, maxProgressValue);
	}
	else
	{
		// Progress is at 100%. Unlock the achievement.
		wasSuccessful = steamUserStatsPointer->SetAchievement(achievementName);
	}
	if (wasSuccessful)
	{
		steamUserStatsPointer->StoreStats();
	}
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/** bool steamworks.setAchievementUnlocked(achievementName) */
int OnSetAchievementUnlocked(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the Steam object used to access achievements.
	// Will return false to Lua if not currently connected to Steam client.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the achievement name.
	const char* achievementName = nullptr;
	if (lua_type(luaStatePointer, 1) == LUA_TSTRING)
	{
		achievementName = lua_tostring(luaStatePointer, 1);
	}
	if (!achievementName)
	{
		CoronaLuaError(luaStatePointer, "1st argument must be set to the achievement's unique name.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Attempt to unlock the given achievement.
	bool wasSuccessful = steamUserStatsPointer->SetAchievement(achievementName);
	if (wasSuccessful)
	{
		steamUserStatsPointer->StoreStats();
	}
	lua_pushboolean(luaStatePointer, wasSuccessful ? 1 : 0);
	return 1;
}

/** bool steamworks.showGameOverlay([overlayName]) */
int OnShowGameOverlay(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the Steam object used to display the overlay.
	// Will return false to Lua if not currently connected to Steam client.
	auto steamFriendsPointer = SteamFriends();
	if (!steamFriendsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}
	
	// Do not continue if unable to display overlays.
	if (!CanShowSteamOverlay())
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the optional overlay name argument.
	const char* overlayName = nullptr;
	const auto luaArgumentType = lua_type(luaStatePointer, 1);
	if (luaArgumentType == LUA_TSTRING)
	{
		overlayName = lua_tostring(luaStatePointer, 1);
	}
	else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
	{
		CoronaLuaError(luaStatePointer, "1st argument is not of type string.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Display the given overlay type.
	// Will display the default overlay if given a null pointer or an unknown name.
	steamFriendsPointer->ActivateGameOverlay(overlayName);
	lua_pushboolean(luaStatePointer, 1);
	return 1;
}

/** bool steamworks.showStoreOverlay([appId]) */
int OnShowStoreOverlay(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the Steam object used to display the overlay.
	// Will return false to Lua if not currently connected to Steam client.
	auto steamFriendsPointer = SteamFriends();
	if (!steamFriendsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}
	
	// Do not continue if unable to display overlays.
	if (!CanShowSteamOverlay())
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the optional app ID string argument.
	std::string stringId;
	const auto luaArgumentType = lua_type(luaStatePointer, 1);
	if (luaArgumentType == LUA_TSTRING)
	{
		auto stringArgument = lua_tostring(luaStatePointer, 1);
		if (stringArgument)
		{
			stringId = stringArgument;
		}
	}
	else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
	{
		CoronaLuaError(luaStatePointer, "Given AppId argument is not of type string.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// If an app ID argument was not provided, then fetch this application's ID.
	if (stringId.empty())
	{
		CopySteamAppIdTo(stringId);
		if (stringId.empty())
		{
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Convert the string ID to integer form.
	AppId_t integerId = 0;
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << stringId;
		stringStream >> integerId;
		if (stringStream.fail())
		{
			CoronaLuaError(
					luaStatePointer, "Given string is an invalid app ID: '%s'", stringId.c_str());
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Display the requested app in the Steam store by its app ID.
	steamFriendsPointer->ActivateGameOverlayToStore(integerId, k_EOverlayToStoreFlag_None);
	lua_pushboolean(luaStatePointer, 1);
	return 1;
}

/** bool steamworks.showUserOverlay(userSteamId, [overlayName]) */
int OnShowUserOverlay(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the Steam object used to display the overlay.
	// Will return false to Lua if not currently connected to Steam client.
	auto steamFriendsPointer = SteamFriends();
	if (!steamFriendsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}
	
	// Do not continue if unable to display overlays.
	if (!CanShowSteamOverlay())
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the required steam ID of the user.
	const char* userStringId = nullptr;
	{
		const auto luaArgumentType = lua_type(luaStatePointer, 1);
		if (luaArgumentType == LUA_TSTRING)
		{
			userStringId = lua_tostring(luaStatePointer, 1);
		}
		else if (luaArgumentType == LUA_TNONE)
		{
			CoronaLuaError(luaStatePointer, "1st argument must be set to the user's steam ID.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
		else
		{
			CoronaLuaError(luaStatePointer, "1st argument (userSteamId) is not of type string.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}
	if (!userStringId || ('\0' == userStringId[0]))
	{
		CoronaLuaError(luaStatePointer, "User ID cannot be set to an empty string.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Convert the string ID to a CSteamID object.
	CSteamID userSteamId;
	{
		uint64 integerId = 0;
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << userStringId;
		stringStream >> integerId;
		if (!stringStream.fail())
		{
			userSteamId.SetFromUint64(integerId);
		}
		if (userSteamId.IsValid() == false)
		{
			CoronaLuaError(luaStatePointer, "Given user ID is invalid: '%s'", userStringId);
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Fetch optional overlay name argument.
	const char* overlayName = "steamid";
	{
		const auto luaArgumentType = lua_type(luaStatePointer, 2);
		if (luaArgumentType == LUA_TSTRING)
		{
			overlayName = lua_tostring(luaStatePointer, 2);
		}
		else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
		{
			CoronaLuaError(luaStatePointer, "2nd argument (overlayName) is not of type string.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Display the given user's info.
	steamFriendsPointer->ActivateGameOverlayToUser(overlayName, userSteamId);
	lua_pushboolean(luaStatePointer, 1);
	return 1;
}

/** bool steamworks.showWebOverlay([url]) */
int OnShowWebOverlay(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the Steam object used to display the overlay.
	// Will return false to Lua if not currently connected to Steam client.
	auto steamFriendsPointer = SteamFriends();
	if (!steamFriendsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}
	
	// Do not continue if unable to display overlays.
	if (!CanShowSteamOverlay())
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the optional URL string.
	const char* url = nullptr;
	{
		const auto luaArgumentType = lua_type(luaStatePointer, 1);
		if (luaArgumentType == LUA_TSTRING)
		{
			url = lua_tostring(luaStatePointer, 1);
		}
		else if ((luaArgumentType != LUA_TNONE) && (luaArgumentType != LUA_TNIL))
		{
			CoronaLuaError(luaStatePointer, "Given URL argument is not of type string.");
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	// Display a web overlay for the given URL.
	// Will display the last viewed web page or empty page if URL is null.
	steamFriendsPointer->ActivateGameOverlayToWebPage(url);
	lua_pushboolean(luaStatePointer, 1);
	return 1;
}

/** bool steamworks.setNotificationPosition(positionName) */
int OnSetNotificationPosition(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the steam object used to set the notification position.
	auto steamUtilsPointer = SteamUtils();
	if (!steamUtilsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the required position name argument.
	if (lua_type(luaStatePointer, 1) != LUA_TSTRING)
	{
		CoronaLuaError(luaStatePointer, "Given argument is not of type string.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}
	const char* positionName = lua_tostring(luaStatePointer, 1);
	if (!positionName)
	{
		positionName = "";
	}

	// Convert the position name to its equivalent Steam enum constant.
	ENotificationPosition positionId;
	if (!strcmp(positionName, "topLeft"))
	{
		positionId = k_EPositionTopLeft;
	}
	else if (!strcmp(positionName, "topRight"))
	{
		positionId = k_EPositionTopRight;
	}
	else if (!strcmp(positionName, "bottomLeft"))
	{
		positionId = k_EPositionBottomLeft;
	}
	else if (!strcmp(positionName, "bottomRight"))
	{
		positionId = k_EPositionBottomRight;
	}
	else
	{
		CoronaLuaError(luaStatePointer, "Given unknown position name '%s'", positionName);
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Change Steam's notification position with given setting.
	steamUtilsPointer->SetOverlayNotificationPosition(positionId);
	lua_pushboolean(luaStatePointer, 1);
	return 1;
}

/** bool steamworks.setUserStatValues({{statName="", type="", value=x}, {name="", type="", value=x}}) */
int OnSetUserStatValues(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Make sure the 1st argument is a Lua array of tables containing at least 1 element.
	if (!lua_istable(luaStatePointer, 1) || (lua_objlen(luaStatePointer, 1) < 1))
	{
		CoronaLuaError(luaStatePointer, "1st argument must be an array of tables.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return null if Steam client is not currently running.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Traverse all of the stat tables in the given Lua array.
	bool hasSetStat = false;
	size_t statCount = lua_objlen(luaStatePointer, 1);
	for (size_t statIndex = 1; statIndex <= statCount; statIndex++, lua_pop(luaStatePointer, 1))
	{
		// Push the next array element to the top of the stack.
		// Note: Will be popped off by the for loop's increment section.
		lua_rawgeti(luaStatePointer, 1, (int)statIndex);

		// Ensure that the nxt array element is a table.
		if (!lua_istable(luaStatePointer, -1))
		{
			CoronaLuaError(luaStatePointer, "Array element [%d] is not a table.", statIndex);
			continue;
		}

		// Fetch the element's stat name.
		const char* statName = nullptr;
		const char kStatNameFieldName[] = "statName";
		lua_getfield(luaStatePointer, -1, kStatNameFieldName);
		if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
		{
			statName = lua_tostring(luaStatePointer, -1);
		}
		lua_pop(luaStatePointer, 1);
		if (!statName || ('\0' == statName[0]))
		{
			const char kMessage[] = "Array element [%d] must contain a '%s' field set to a non-empty string.";
			CoronaLuaError(luaStatePointer, kMessage, statIndex, kStatNameFieldName);
			continue;
		}

		// Fetch the element's stat type.
		SteamStatValueType valueType;
		const char kTypeFieldName[] = "type";
		lua_getfield(luaStatePointer, -1, kTypeFieldName);
		if (lua_type(luaStatePointer, -1) == LUA_TSTRING)
		{
			valueType = SteamStatValueType::FromCoronaStringId(lua_tostring(luaStatePointer, -1));
		}
		lua_pop(luaStatePointer, 1);
		if (SteamStatValueType::kUnknown == valueType)
		{
			const char kMessage[] =
					"Array element [%d] must contain a '%s' field set to either 'int', 'float', or 'averageRate'.";
			CoronaLuaError(luaStatePointer, kMessage, statIndex, kTypeFieldName);
			continue;
		}

		// Fetch the element's stat value.
		double floatValue = 0;
		{
			bool hasValue = false;
			const char kValueFieldName[] = "value";
			lua_getfield(luaStatePointer, -1, kValueFieldName);
			if (lua_type(luaStatePointer, -1) == LUA_TNUMBER)
			{
				floatValue = lua_tonumber(luaStatePointer, -1);
				hasValue = true;
			}
			lua_pop(luaStatePointer, 1);
			if (!hasValue)
			{
				const char kMessage[] = "Array element [%d] must contain a '%s' field set to a number.";
				CoronaLuaError(luaStatePointer, kMessage, statIndex, kValueFieldName);
				continue;
			}
		}

		// Fetch the element's session time, but only if updating an average rate stat.
		double sessionTimeLength = 0;
		if (valueType == SteamStatValueType::kAverageRate)
		{
			bool hasValue = false;
			const char kSessionTimeLengthFieldName[] = "sessionTimeLength";
			lua_getfield(luaStatePointer, -1, kSessionTimeLengthFieldName);
			if (lua_type(luaStatePointer, -1) == LUA_TNUMBER)
			{
				sessionTimeLength = lua_tonumber(luaStatePointer, -1);
				hasValue = true;
			}
			lua_pop(luaStatePointer, 1);
			if (!hasValue)
			{
				const char kMessage[] = "Array element [%d] must contain a '%s' field set to a number.";
				CoronaLuaError(luaStatePointer, kMessage, statIndex, kSessionTimeLengthFieldName);
				continue;
			}
			if (sessionTimeLength <= 0)
			{
				const char kMessage[] = "Array element [%d] field '%s' must be set to a value greater than zero.";
				CoronaLuaError(luaStatePointer, kMessage, statIndex, kSessionTimeLengthFieldName);
				continue;
			}
		}

		// Update the user's stat with the given value.
		bool wasSuccessful = false;
		if (valueType == SteamStatValueType::kFloat)
		{
			wasSuccessful = steamUserStatsPointer->SetStat(statName, (float)floatValue);
		}
		else if (valueType == SteamStatValueType::kInteger)
		{
			long longValue = std::lround(floatValue);
			if (longValue > INT32_MAX)
			{
				longValue = INT32_MAX;
			}
			else if (longValue < INT32_MIN)
			{
				longValue = INT32_MIN;
			}
			wasSuccessful = steamUserStatsPointer->SetStat(statName, (int32)longValue);
		}
		else if (valueType == SteamStatValueType::kAverageRate)
		{
			wasSuccessful = steamUserStatsPointer->UpdateAvgRateStat(statName, (float)floatValue, sessionTimeLength);
		}
		if (wasSuccessful)
		{
			hasSetStat = true;
		}
	}

	// Commit the changes above if we've successfully set at least 1 stat value.
	if (hasSetStat)
	{
		steamUserStatsPointer->StoreStats();
	}

	// Return true to Lua if we've successfully updated at least 1 stat value.
	lua_pushboolean(luaStatePointer, hasSetStat ? 1 : 0);
	return 1;
}

/** bool steamworks.isDlcInstalled([appId]) */
int OnIsDlcInstalled(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the Steam object used to display the overlay.
	// Will return false to Lua if not currently connected to Steam client.
	auto steamApps = SteamApps();
	if (!steamApps)
	{
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Fetch the app ID string argument.
	std::string stringId;
	const auto luaArgumentType = lua_type(luaStatePointer, 1);
	if (luaArgumentType == LUA_TSTRING)
	{
		auto stringArgument = lua_tostring(luaStatePointer, 1);
		if (stringArgument)
		{
			stringId = stringArgument;
		}
	}
	
	if (stringId.empty())
	{
		CoronaLuaError(luaStatePointer, "Given AppId argument should be a string.");
		lua_pushboolean(luaStatePointer, 0);
		return 1;
	}

	// Convert the string ID to integer form.
	AppId_t integerId = 0;
	{
		std::stringstream stringStream;
		stringStream.imbue(std::locale::classic());
		stringStream << stringId;
		stringStream >> integerId;
		if (stringStream.fail())
		{
			CoronaLuaError(
				luaStatePointer, "Given string is an invalid app ID: '%s'", stringId.c_str());
			lua_pushboolean(luaStatePointer, 0);
			return 1;
		}
	}

	bool result = steamApps->BIsDlcInstalled(integerId);

	lua_pushboolean(luaStatePointer, result);
	return 1;
}


/** arrayOfStrings steamworks.getAchievementNames() */
int OnGetAchievementNames(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the Steam interface needed by this API call.
	// Note: Will return an empty array if not connected to Steam client.
	auto steamUserStatsPointer = SteamUserStats();
	if (!steamUserStatsPointer)
	{
		lua_createtable(luaStatePointer, 0, 0);
		return 1;
	}

	// Return an array of all unique achievement names belonging to this application.
	uint32 achievementCount = steamUserStatsPointer->GetNumAchievements();
	lua_createtable(luaStatePointer, achievementCount, 0);
	for (uint32 achievementIndex = 0; achievementIndex < achievementCount; achievementIndex++)
	{
		auto achievementName = steamUserStatsPointer->GetAchievementName(achievementIndex);
		lua_pushstring(luaStatePointer, achievementName ? achievementName : "");
		lua_rawseti(luaStatePointer, -2, (int)achievementIndex + 1);
	}
	return 1;
}

/** steamworks.addEventListener(eventName, listener) */
int OnAddEventListener(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the global Steam event name to listen to.
	const char* eventName = nullptr;
	if (lua_type(luaStatePointer, 1) == LUA_TSTRING)
	{
		eventName = lua_tostring(luaStatePointer, 1);
	}
	if (!eventName || ('\0' == eventName[0]))
	{
		CoronaLuaError(luaStatePointer, "1st argument must be set to an event name.");
		return 0;
	}

	// Determine if the 2nd argument references a Lua listener function/table.
	if (!CoronaLuaIsListener(luaStatePointer, 2, eventName))
	{
		CoronaLuaError(luaStatePointer, "2nd argument must be set to a listener.");
		return 0;
	}

	// Fetch the runtime context associated with the calling Lua state.
	auto contextPointer = (RuntimeContext*)lua_touserdata(luaStatePointer, lua_upvalueindex(1));
	if (!contextPointer)
	{
		return 0;
	}

	// Add the given listener for the global Steam event.
	auto luaEventDispatcherPointer = contextPointer->GetLuaEventDispatcher();
	if (luaEventDispatcherPointer)
	{
		luaEventDispatcherPointer->AddEventListener(luaStatePointer, eventName, 2);
	}
	return 0;
}

/** steamworks.removeEventListener(eventName, listener) */
int OnRemoveEventListener(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the global Steam event name to stop listening to.
	const char* eventName = nullptr;
	if (lua_type(luaStatePointer, 1) == LUA_TSTRING)
	{
		eventName = lua_tostring(luaStatePointer, 1);
	}
	if (!eventName || ('\0' == eventName[0]))
	{
		CoronaLuaError(luaStatePointer, "1st argument must be set to an event name.");
		return 0;
	}

	// Determine if the 2nd argument references a Lua listener function/table.
	if (!CoronaLuaIsListener(luaStatePointer, 2, eventName))
	{
		CoronaLuaError(luaStatePointer, "2nd argument must be set to a listener.");
		return 0;
	}

	// Fetch the runtime context associated with the calling Lua state.
	auto contextPointer = (RuntimeContext*)lua_touserdata(luaStatePointer, lua_upvalueindex(1));
	if (!contextPointer)
	{
		return 0;
	}

	// Remove the given listener from the global Steam event.
	auto luaEventDispatcherPointer = contextPointer->GetLuaEventDispatcher();
	if (luaEventDispatcherPointer)
	{
		luaEventDispatcherPointer->RemoveEventListener(luaStatePointer, eventName, 2);
	}
	return 0;
}

/** Called when a property field is being read from the plugin's Lua table. */
int OnAccessingField(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Fetch the field name being accessed.
	if (lua_type(luaStatePointer, 2) != LUA_TSTRING)
	{
		return 0;
	}
	auto fieldName = lua_tostring(luaStatePointer, 2);
	if (!fieldName)
	{
		return 0;
	}

	// Attempt to fetch the requested field value.
	int resultCount = 0;
	if (!strcmp(fieldName, "appId"))
	{
		// Push the ID assigned to this application by Steam to Lua.
		std::string stringId;
		bool wasSuccessful = CopySteamAppIdTo(stringId);
		if (wasSuccessful && !stringId.empty())
		{
			lua_pushstring(luaStatePointer, stringId.c_str());
		}
		else
		{
			lua_pushnil(luaStatePointer);
		}
		resultCount = 1;
	}
	else if (!strcmp(fieldName, "appOwnerSteamId"))
	{
		// Push the string ID of the user that purchased this app to Lua.
		// Note: This is a 64-bit int which exceeds the digits of precision a Lua number can store.
		//       So, we must return Steam IDs in string form to preserve all of the digits.
		uint64 integerId = 0;
		auto steamAppsPointer = SteamApps();
		if (steamAppsPointer)
		{
			integerId = steamAppsPointer->GetAppOwner().ConvertToUint64();
		}
		if (integerId)
		{
			std::stringstream stringStream;
			stringStream.imbue(std::locale::classic());
			stringStream << integerId;
			auto stringId = stringStream.str();
			lua_pushstring(luaStatePointer, stringId.c_str());
		}
		else
		{
			lua_pushnil(luaStatePointer);
		}
		resultCount = 1;
	}
	else if (!strcmp(fieldName, "userSteamId"))
	{
		// Push the currently logged in user's ID in string form to Lua.
		// Note: This is a 64-bit int which exceeds the digits of precision a Lua number can store.
		//       So, we must return Steam IDs in string form to preserve all of the digits.
		uint64 integerId = 0;
		auto steamUserPointer = SteamUser();
		if (steamUserPointer)
		{
			integerId = steamUserPointer->GetSteamID().ConvertToUint64();
		}
		if (integerId)
		{
			std::stringstream stringStream;
			stringStream.imbue(std::locale::classic());
			stringStream << integerId;
			auto stringId = stringStream.str();
			lua_pushstring(luaStatePointer, stringId.c_str());
		}
		else
		{
			lua_pushnil(luaStatePointer);
		}
		resultCount = 1;
	}
	else if (!strcmp(fieldName, "isLoggedOn"))
	{
		// Push a boolean to Lua indicating if the Steam client is currently running
		// and that the user is currently logged into it.
		// Note: Do not call SteamUser()->BLoggedOn() since it returns false while in "offline mode".
		bool isLoggedOn = false;
		auto steamUserPointer = SteamUser();
		if (steamUserPointer)
		{
			isLoggedOn = steamUserPointer->GetSteamID().IsValid();
		}
		lua_pushboolean(luaStatePointer, isLoggedOn ? 1 : 0);
		resultCount = 1;
	}
	else if (!strcmp(fieldName, "canShowOverlay"))
	{
		// Push a boolean indicating if Steam's overlay can be rendered on top of this app.
		bool canShowOverlay = CanShowSteamOverlay();
		lua_pushboolean(luaStatePointer, canShowOverlay ? 1 : 0);
		resultCount = 1;
	}
	else
	{
		// Unknown field.
		CoronaLuaError(luaStatePointer, "Accessing unknown field: '%s'", fieldName);
	}

	// Return the number of value pushed to Lua as return values.
	return resultCount;
}

/** Called when a property field is being written to in the plugin's Lua table. */
int OnAssigningField(lua_State* luaStatePointer)
{
	// Writing to fields is not currently supported.
	return 0;
}

/**
  Called when the Lua plugin table is being destroyed.
  Expected to happen when the Lua runtime is being terminated.

  Performs finaly cleanup and terminates connection with the Steam client.
 */
int OnFinalizing(lua_State* luaStatePointer)
{
	// Delete this plugin's runtime context from memory.
	auto contextPointer = (RuntimeContext*)lua_touserdata(luaStatePointer, lua_upvalueindex(1));
	if (contextPointer)
	{
		delete contextPointer;
	}

	// Shutdown our connection with Steam if this is the last plugin instance.
	// This must be done after deleting the RuntimeContext above.
	if (RuntimeContext::GetInstanceCount() <= 0)
	{
		SteamAPI_Shutdown();
	}
	return 0;
}


//---------------------------------------------------------------------------------
// Public Exports
//---------------------------------------------------------------------------------

/**
  Called when this plugin is being loaded from Lua via a require() function.
  Initializes itself with Steam and returns the plugin's Lua table.
 */
CORONA_EXPORT int luaopen_plugin_steamworks(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// If this plugin instance is being loaded while another one already exists, then make sure that they're
	// both running on the same thread to avoid race conditions since Steam's event handlers are global.
	// Note: This can only happen if multiple Corona runtimes are running at the same time.
	if (RuntimeContext::GetInstanceCount() > 0)
	{
		if (std::this_thread::get_id() != sMainThreadId)
		{
			luaL_error(luaStatePointer, "Cannot load another instance of 'plugin.steamworks' from another thread.");
			return 0;
		}
	}
	else
	{
		sMainThreadId = std::this_thread::get_id();
	}

	// Create a new runtime context used to receive Steam's event and dispatch them to Lua.
	// Also used to ensure that the Steam overlay is rendered when requested on Windows.
	auto contextPointer = new RuntimeContext(luaStatePointer);
	if (!contextPointer)
	{
		return 0;
	}

	// Push this plugin's Lua table and all of its functions to the top of the Lua stack.
	// Note: The RuntimeContext pointer is pushed as an upvalue to all of these functions via luaL_openlib().
	{
		const struct luaL_Reg luaFunctions[] =
		{
			{ "getAchievementImageInfo", OnGetAchievementImageInfo },
			{ "getAchievementInfo", OnGetAchievementInfo },
			{ "getAchievementNames", OnGetAchievementNames },
			{ "getUserImageInfo", OnGetUserImageInfo },
			{ "getUserInfo", OnGetUserInfo },
			{ "getUserStatValue", OnGetUserStatValue },
			{ "newImageRect", OnNewImageRect },
			{ "newTexture", OnNewTexture },
			{ "requestActivePlayerCount", OnRequestActivePlayerCount },
			{ "requestLeaderboardEntries", OnRequestLeaderboardEntries },
			{ "requestLeaderboardInfo", OnRequestLeaderboardInfo },
			{ "requestSetHighScore", OnRequestSetHighScore },
			{ "requestUserProgress", OnRequestUserProgress },
			{ "resetUserProgress", OnResetUserProgress },
			{ "resetUserStats", OnResetUserStats },
			{ "setAchievementProgress", OnSetAchievementProgress },
			{ "setAchievementUnlocked", OnSetAchievementUnlocked },
			{ "setNotificationPosition", OnSetNotificationPosition },
			{ "setUserStatValues", OnSetUserStatValues },
			{ "showGameOverlay", OnShowGameOverlay },
			{ "showStoreOverlay", OnShowStoreOverlay },
			{ "showUserOverlay", OnShowUserOverlay },
			{ "showWebOverlay", OnShowWebOverlay },
			{ "isDlcInstalled", OnIsDlcInstalled },
			{ "addEventListener", OnAddEventListener },
			{ "removeEventListener", OnRemoveEventListener },
			{ nullptr, nullptr }
		};
		lua_createtable(luaStatePointer, 0, 0);
		lua_pushlightuserdata(luaStatePointer, contextPointer);
		luaL_openlib(luaStatePointer, nullptr, luaFunctions, 1);
	}

	// Add a Lua finalizer to the plugin's Lua table and to the Lua registry.
	// Note: Lua 5.1 tables do not support the "__gc" metatable field, but Lua light-userdata types do.
	{
		// Create a Lua metatable used to receive the finalize event.
		const struct luaL_Reg luaFunctions[] =
		{
			{ "__gc", OnFinalizing },
			{ nullptr, nullptr }
		};
		luaL_newmetatable(luaStatePointer, "plugin.steamworks.__gc");
		lua_pushlightuserdata(luaStatePointer, contextPointer);
		luaL_openlib(luaStatePointer, nullptr, luaFunctions, 1);
		lua_pop(luaStatePointer, 1);

		// Add the finalizer metable to the Lua registry.
		CoronaLuaPushUserdata(luaStatePointer, nullptr, "plugin.steamworks.__gc");
		int luaReferenceKey = luaL_ref(luaStatePointer, LUA_REGISTRYINDEX);

		// Add the finalizer metatable to the plugin's Lua table as an undocumented "__gc" field.
		// Note that a developer can overwrite this field, which is why we add it to the registry above too.
		lua_rawgeti(luaStatePointer, LUA_REGISTRYINDEX, luaReferenceKey);
		lua_setfield(luaStatePointer, -2, "__gc");
	}

	// Wrap the plugin's Lua table in a metatable used to provide readable/writable property fields.
	{
		const struct luaL_Reg luaFunctions[] =
		{
			{ "__index", OnAccessingField },
			{ "__newindex", OnAssigningField },
			{ nullptr, nullptr }
		};
		luaL_newmetatable(luaStatePointer, "plugin.steamworks");
		lua_pushlightuserdata(luaStatePointer, contextPointer);
		luaL_openlib(luaStatePointer, nullptr, luaFunctions, 1);
		lua_setmetatable(luaStatePointer, -2);
	}

	// Acquire and handle the Steam app ID.
	// This needs to be done before calling the SteamAPI_Init() function.
	{
		// First, check if a Steam app ID has already been assigned to this application.
		// This can happen when:
		// - The Corona project has been run more than once in the same app, such as via the Corona Simulator.
		// - This app was launch via the Steam client, which happens with deployed Steam apps.
		std::string currentStringId;
		CopySteamAppIdTo(currentStringId);
		if (currentStringId == "0")
		{
			// Ignore an app ID of zero, which is an invalid ID.
			// This can happen when launching an app from the Steam client that was not installed by Steam.
			// Steam also allows us to switch an app ID of zero to a working/real app ID, which we may do down below.
			currentStringId.erase();
		}

		// Fetch the Steam app ID configured in the "config.lua" file.
		std::string configStringId;
		PluginConfigLuaSettings configLuaSettings;
		configLuaSettings.LoadFrom(luaStatePointer);
		if (configLuaSettings.GetStringAppId())
		{
			configStringId = configLuaSettings.GetStringAppId();
		}

		// Handle/apply the Steam app ID.
		if (currentStringId.empty() && !configStringId.empty())
		{
			// The Steam app ID in the "config.lua" has not been applied to this application process.
			// Apply it now by setting it to a Steam defined environment variable.
#ifdef _WIN32
			_putenv_s(kSteamAppIdEnvironmentVariableName, configStringId.c_str());
#else
			setenv(kSteamAppIdEnvironmentVariableName, configStringId.c_str(), 1);
#endif
		}
		else if (currentStringId.empty() && configStringId.empty())
		{
			// A Steam app ID has not been configured.
			CoronaLuaWarning(
					luaStatePointer,
					"You must set an 'appId' in the 'config.lua' file in order to use the Steamworks plugin.");
		}
		else if (!currentStringId.empty() && !configStringId.empty() && (currentStringId != configStringId))
		{
			// The Steam app ID applied to this application process differs than the one in the "config.lua" file.
			// Steam does not support hot-swapping app IDs for the same application process.
			if (IsRunningInCoronaSimulator(luaStatePointer))
			{
				// We're running in the Corona Simulator.
				// Log a detailed message explaining that the application needs to be restarted to use new app IDs.
				std::string message =
						"You must exit and restart the Corona Simulator in order to test with a new Steam appId.\n"
						"\n"
						"Reason:\n"
						"This is a Steam limitation. Once the Steam client binds to a running application process "
						"with a given Steam appId, it cannot be unbound.\n"
						"\n"
						"Last used appId:   '" + currentStringId + "'\n"
						"Current appId:   '" + configStringId + "'";
				CoronaLuaWarning(luaStatePointer, message.c_str());

				// Also display the above message as a native alert.
				lua_getglobal(luaStatePointer, "native");
				if (lua_istable(luaStatePointer, -1))
				{
					lua_getfield(luaStatePointer, -1, "showAlert");
					if (lua_isfunction(luaStatePointer, -1))
					{
						lua_pushstring(luaStatePointer, "Warning");
						lua_pushstring(luaStatePointer, message.c_str());
						CoronaLuaDoCall(luaStatePointer, 2, 1);
					}
					lua_pop(luaStatePointer, 1);
				}
				lua_pop(luaStatePointer, 1);
			}
			else
			{
				// We're running in desktop app mode. Log a less detailed message.
				const char kMessage[] =
						"This app was launched with Steam appId '%s' which differs from appId '%s' "
						"set in the 'config.lua' file. The Steamworks plugin will use the launched appId. "
						"This can happen when a published app is launched from the Steam client.";
				CoronaLuaWarning(luaStatePointer, kMessage);
			}
		}
	}

//TODO: Figure out a nice way to enable Steam debug messaging via our OnSteamWarningMessageReceived() callback.
//      Especially in the Corona Simulator. These messages explain why Steam APIs fail and return false.
//      Steam only provides these message if:
//      - Command line argument "-debug_steamapi" is set.
//      - If running under the Visual Studio or Xcode debugger.

	// Initialize our connection with Steam if this is the first plugin instance.
	// Note: This avoid initializing twice in case multiple plugin instances exist at the same time.
	if (RuntimeContext::GetInstanceCount() == 1)
	{
		auto wasInitialized = SteamAPI_Init();
		if (!wasInitialized)
		{
			CoronaLuaError(luaStatePointer, "Failed to initialize connection with Steam client.");
		}
	}

	// Set up a callback to receive Steam's info/warning messages to be outputted to Corona's logging functions.
	// Also allows for Steam warning messages to be properly highlighted in the Corona Simulator's logging window.
	auto steamClientPointer = SteamClient();
	if (steamClientPointer)
	{
		steamClientPointer->SetWarningMessageHook(OnSteamWarningMessageReceived);
	}

	// Request the current logged in user's stats and achievement info.
	auto steamUserStatsPointer = SteamUserStats();
	if (steamUserStatsPointer)
	{
		steamUserStatsPointer->RequestCurrentStats();
	}

	// We're returning 1 Lua plugin table.
	return 1;
}
