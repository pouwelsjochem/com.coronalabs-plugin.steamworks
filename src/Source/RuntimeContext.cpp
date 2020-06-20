// ----------------------------------------------------------------------------
// 
// RuntimeContext.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "RuntimeContext.h"
#include "CoronaLua.h"
#include "DispatchEventTask.h"
#include "SteamCallResultHandler.h"
#include <exception>
#include <memory>
#include <unordered_set>
extern "C"
{
#	include "lua.h"
}


/** Stores a collection of all RuntimeContext instances that currently exist in the application. */
static std::unordered_set<RuntimeContext*> sRuntimeContextCollection;


RuntimeContext::RuntimeContext(lua_State* luaStatePointer)
:	fLuaEnterFrameCallback(this, &RuntimeContext::OnCoronaEnterFrame, luaStatePointer),
	fWasRenderRequested(false)
{
	// Validate.
	if (!luaStatePointer)
	{
		throw std::exception();
	}

	// If the given Lua state belongs to a coroutine, then use the main Lua state instead.
	{
		auto mainLuaStatePointer = CoronaLuaGetCoronaThread(luaStatePointer);
		if (mainLuaStatePointer && (mainLuaStatePointer != luaStatePointer))
		{
			luaStatePointer = mainLuaStatePointer;
		}
	}

	// Create a Lua EventDispatcher object.
	// Used to dispatch global events to listeners
	fLuaEventDispatcherPointer = std::make_shared<LuaEventDispatcher>(luaStatePointer);

	// Add Corona runtime event listeners.
	fLuaEnterFrameCallback.AddToRuntimeEventListeners("enterFrame");

	// Add this class instance to the global collection.
	sRuntimeContextCollection.insert(this);
}

RuntimeContext::~RuntimeContext()
{
	// Remove our Corona runtime event listeners.
	fLuaEnterFrameCallback.RemoveFromRuntimeEventListeners("enterFrame");

	// Delete our pool of Steam call result handlers.
	for (auto nextHandlerPointer : fSteamCallResultHandlerPool)
	{
		if (nextHandlerPointer)
		{
			delete nextHandlerPointer;
		}
	}

	// Remove this class instance from the global collection.
	sRuntimeContextCollection.erase(this);
}

lua_State* RuntimeContext::GetMainLuaState() const
{
	if (fLuaEventDispatcherPointer)
	{
		return fLuaEventDispatcherPointer->GetLuaState();
	}
	return nullptr;
}

std::shared_ptr<LuaEventDispatcher> RuntimeContext::GetLuaEventDispatcher() const
{
	return fLuaEventDispatcherPointer;
}

SteamLeaderboardEntries_t RuntimeContext::GetCachedLeaderboardHandleByName(const char* name) const
{
	if (name)
	{
		auto iterator = fLeaderboardNameHandleMap.find(std::string(name));
		if (iterator != fLeaderboardNameHandleMap.end())
		{
			return iterator->second;
		}
	}
	return 0;
}

SteamImageInfo RuntimeContext::GetUserImageInfoFor(const CSteamID& userSteamId, const SteamUserImageType& imageType)
{
	// Validate arguments.
	if ((userSteamId.IsValid() == false) || (imageType == SteamUserImageType::kUnknown))
	{
		return SteamImageInfo();
	}

	// Fetch Steam interface needed to perform this operation.
	auto steamFriendsPointer = SteamFriends();
	if (!steamFriendsPointer)
	{
		return SteamImageInfo();
	}

	// Fetch a handle to the requested image.
	int imageHandle = 0;
	if (imageType == SteamUserImageType::kAvatarSmall)
	{
		imageHandle = steamFriendsPointer->GetSmallFriendAvatar(userSteamId);
	}
	else if (imageType == SteamUserImageType::kAvatarMedium)
	{
		imageHandle = steamFriendsPointer->GetMediumFriendAvatar(userSteamId);
	}
	else if (imageType == SteamUserImageType::kAvatarLarge)
	{
		imageHandle = steamFriendsPointer->GetLargeFriendAvatar(userSteamId);
	}
	else
	{
		return SteamImageInfo();
	}

	// If the handle is zero, then Steam hasn't downloaded/cached the image yet. We must request it manually.
	// Note: A handle of -1 can be returned by GetLargeFriendAvatar(), which means it has already sent a request.
	if (0 == imageHandle)
	{
		steamFriendsPointer->RequestUserInformation(userSteamId, false);
	}

	// If the caller is requesting a large avatar, then set up this runtime context to auto-fetch
	// the user's large avatar anytime we detect that the user's smaller avatars have changed.
	// Note: We can't fetch a large avatar via GetLargeFriendAvatar() until smaller avatars have been fetched first
	//       via a call to the RequestUserInformation() function and we must wait for the "PersonaStateChange_t" event.
	//       This means that we must chain the avatar fetching requests.
	if (imageType == SteamUserImageType::kAvatarLarge)
	{
		fLargeAvatarSubscribedUserIdSet.insert(userSteamId.ConvertToUint64());
	}

	// Return Steam's image information with the retrieved handle.
	// Will return an invalid image info object if the handle is invalid.
	return SteamImageInfo::FromImageHandle(imageHandle);
}

RuntimeContext* RuntimeContext::GetInstanceBy(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return nullptr;
	}

	// If the given Lua state belongs to a coroutine, then use the main Lua state instead.
	{
		auto mainLuaStatePointer = CoronaLuaGetCoronaThread(luaStatePointer);
		if (mainLuaStatePointer && (mainLuaStatePointer != luaStatePointer))
		{
			luaStatePointer = mainLuaStatePointer;
		}
	}

	// Return the first runtime context instance belonging to the given Lua state.
	for (auto&& runtimePointer : sRuntimeContextCollection)
	{
		if (runtimePointer && runtimePointer->GetMainLuaState() == luaStatePointer)
		{
			return runtimePointer;
		}
	}
	return nullptr;
}

int RuntimeContext::GetInstanceCount()
{
	return (int)sRuntimeContextCollection.size();
}

int RuntimeContext::OnCoronaEnterFrame(lua_State* luaStatePointer)
{
	// Validate.
	if (!luaStatePointer)
	{
		return 0;
	}

	// Poll steam for events. This will invoke our event handlers.
	SteamAPI_RunCallbacks();

	// Dispatch all queued events received from the above SteamAPI_RunCallbacks() call to Lua.
	while (fDispatchEventTaskQueue.size() > 0)
	{
		auto dispatchEventTaskPointer = fDispatchEventTaskQueue.front();
		fDispatchEventTaskQueue.pop();
		if (dispatchEventTaskPointer)
		{
			dispatchEventTaskPointer->Execute();
		}
	}

	// If Steam's overlay needs to be rendered, then force Corona to render the next frame.
	// We need to do this because Steam renders its overlay by hooking into the OpenGL/Direct3D rendering process.
	auto steamUtilsPointer = SteamUtils();
	bool isSteamShowingOverlay = (steamUtilsPointer && steamUtilsPointer->BOverlayNeedsPresent());
	if (isSteamShowingOverlay || fWasRenderRequested)
	{
		// We can force Corona to render the next frame by toggling the Lua stage object's visibility state.
		lua_getglobal(luaStatePointer, "display");
		if (!lua_isnil(luaStatePointer, -1))
		{
			lua_getfield(luaStatePointer, -1, "currentStage");
			{
				lua_getfield(luaStatePointer, -1, "isVisible");
				if (lua_type(luaStatePointer, -1) == LUA_TBOOLEAN)
				{
					bool isVisible = lua_toboolean(luaStatePointer, -1) ? true : false;
					lua_pushboolean(luaStatePointer, !isVisible ? 1 : 0);
					lua_setfield(luaStatePointer, -3, "isVisible");
					lua_pushboolean(luaStatePointer, isVisible ? 1 : 0);
					lua_setfield(luaStatePointer, -3, "isVisible");
				}
				lua_pop(luaStatePointer, 1);
			}
			lua_pop(luaStatePointer, 1);
		}
		lua_pop(luaStatePointer, 1);
	}
	{
		// We must always force Corona to render 1 more time while the Steam overlay is shown.
		// This is needed to erase the last rendered frame of a Steam fade-out animation.
		fWasRenderRequested = isSteamShowingOverlay;
	}

	return 0;
}

template<class TSteamResultType, class TDispatchEventTask>
void RuntimeContext::OnHandleGlobalSteamEvent(TSteamResultType* eventDataPointer)
{
	// Triggers a compiler error if template type "TDispatchEventTask" does not derive from "BaseDispatchEventTask".
	static_assert(
			std::is_base_of<BaseDispatchEventTask, TDispatchEventTask>::value,
			"OnReceivedGlobalSteamEvent<TSteamResultType, TDispatchEventTask>() method's 'TDispatchEventTask' type"
			" must be set to a class type derived from the 'BaseDispatchEventTask' class.");

	// Validate.
	if (!eventDataPointer)
	{
		return;
	}

	// Create and configure the event dispatcher task.
	auto taskPointer = new TDispatchEventTask();
	if (!taskPointer)
	{
		return;
	}
	taskPointer->SetLuaEventDispatcher(fLuaEventDispatcherPointer);
	taskPointer->AcquireEventDataFrom(*eventDataPointer);

	// Special handling of particular Steam events goes here.
	if (typeid(*eventDataPointer) == typeid(PersonaStateChange_t))
	{
		// User info has changed.
		auto steamFriendsPointer = SteamFriends();
		auto concreteEvenDatatPointer = (PersonaStateChange_t*)eventDataPointer;
		auto concreteTaskPointer = (DispatchPersonaStateChangedEventTask*)taskPointer;
		if (steamFriendsPointer && (concreteEvenDatatPointer->m_nChangeFlags & k_EPersonaChangeAvatar))
		{
			// The user's small/medium avatar image has changed.
			// Check if we're set up to auto-fetch this user's large avatar.
			// Note: You can't load a large avatar until the small/medium avatars have been loaded first.
			auto iterator = fLargeAvatarSubscribedUserIdSet.find(concreteEvenDatatPointer->m_ulSteamID);
			if (iterator != fLargeAvatarSubscribedUserIdSet.end())
			{
				// Request an image handle to this user's large avatar.
				const int imageHandle = steamFriendsPointer->GetLargeFriendAvatar(concreteEvenDatatPointer->m_ulSteamID);
				if (SteamImageInfo::FromImageHandle(imageHandle).IsValid())
				{
					// The large avatar has already been loaded. Flag the change in the Lua event dispatcher task.
					// Note: In this case, we'll never get an "AvatarImageLoaded_t" event from Steam.
					concreteTaskPointer->SetHasLargeAvatarChanged(true);
				}
				else if (imageHandle == -1)
				{
					// An image handle of -1 indicates that Steam has started downloading the large avatar image.
					// Steam will dispatch an "AvatarImageLoaded_t" event later once downloaded.
				}
			}
		}
	}

	// Queue the received Steam event data to be dispatched to Lua later.
	// This ensures that Lua events are only dispatched while Corona is running (ie: not suspended).
	fDispatchEventTaskQueue.push(std::shared_ptr<BaseDispatchEventTask>(taskPointer));
}

template<class TSteamResultType, class TDispatchEventTask>
void RuntimeContext::OnHandleGlobalSteamEventWithGameId(TSteamResultType* eventDataPointer)
{
	// Validate.
	if (!eventDataPointer)
	{
		return;
	}

	// Ignore the given event if it belongs to another application.
	// Note: The below if-check works for "m_nGameID" fields that are of type uint64 and CGameID.
	auto steamUtilsPointer = SteamUtils();
	if (steamUtilsPointer)
	{
		if (CGameID(steamUtilsPointer->GetAppID()) != CGameID(eventDataPointer->m_nGameID))
		{
			return;
		}
	}

	// Handle the given event.
	OnHandleGlobalSteamEvent<TSteamResultType, TDispatchEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamAvatarImageLoaded(AvatarImageLoaded_t* eventDataPointer)
{
	OnHandleGlobalSteamEvent<AvatarImageLoaded_t, DispatchPersonaStateChangedEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamGameOverlayActivated(GameOverlayActivated_t* eventDataPointer)
{
	OnHandleGlobalSteamEvent<GameOverlayActivated_t, DispatchGameOverlayActivatedEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamMicrotransactionAuthorizationReceived(MicroTxnAuthorizationResponse_t* eventDataPointer)
{
	OnHandleGlobalSteamEvent<
			MicroTxnAuthorizationResponse_t, DispatchMicrotransactionAuthorizationResponseEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamPersonaStateChanged(PersonaStateChange_t* eventDataPointer)
{
	OnHandleGlobalSteamEvent<PersonaStateChange_t, DispatchPersonaStateChangedEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamUserAchievementIconFetched(UserAchievementIconFetched_t* eventDataPointer)
{
	OnHandleGlobalSteamEventWithGameId<
			UserAchievementIconFetched_t, DispatchUserAchievementIconFetchedEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamUserAchievementStored(UserAchievementStored_t* eventDataPointer)
{
	OnHandleGlobalSteamEventWithGameId<
			UserAchievementStored_t, DispatchUserAchievementStoredEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamUserStatsReceived(UserStatsReceived_t* eventDataPointer)
{
	OnHandleGlobalSteamEventWithGameId<UserStatsReceived_t, DispatchUserStatsReceivedEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamUserStatsStored(UserStatsStored_t* eventDataPointer)
{
	OnHandleGlobalSteamEventWithGameId<UserStatsStored_t, DispatchUserStatsStoredEventTask>(eventDataPointer);
}

void RuntimeContext::OnSteamUserStatsUnloaded(UserStatsUnloaded_t* eventDataPointer)
{
	OnHandleGlobalSteamEvent<UserStatsUnloaded_t, DispatchUserStatsUnloadedEventTask>(eventDataPointer);
}
