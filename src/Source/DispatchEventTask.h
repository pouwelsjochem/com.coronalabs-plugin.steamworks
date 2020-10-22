// ----------------------------------------------------------------------------
// 
// DispatchEventTask.h
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#pragma once

#include "LuaEventDispatcher.h"
#include "PluginMacros.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
PLUGIN_DISABLE_STEAM_WARNINGS_BEGIN
#	include "steam_api.h"
PLUGIN_DISABLE_STEAM_WARNINGS_END


// Forward declarations.
extern "C"
{
	struct lua_State;
}


/**
  Abstract class used to dispatch an event table to Lua.

  The intended usage is that a derived class' constructor would copy a Steam event's data structure and then
  the event task instance would be queued to a RuntimeContext. A RuntimeContext would then dispatch all queued
  event tasks to Lua via their Execute() methods only if the Corona runtime is currently running (ie: not suspended).
 */
class BaseDispatchEventTask
{
	public:
		BaseDispatchEventTask();
		virtual ~BaseDispatchEventTask();

		std::shared_ptr<LuaEventDispatcher> GetLuaEventDispatcher() const;
		void SetLuaEventDispatcher(const std::shared_ptr<LuaEventDispatcher>& dispatcherPointer);
		virtual const char* GetLuaEventName() const = 0;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const = 0;
		bool Execute();

	private:
		std::shared_ptr<LuaEventDispatcher> fLuaEventDispatcherPointer;
};


/**
  Abstract class used to dispatch all Steam CCallResult related events to Lua.

  Provides SetHadIOFailure() and HadIOFailure() methods used to determine if there was a Steam I/O failure.
  The RuntimeContext::AddEventHandlerFor() method will automatically set this I/O failure flag.
  It is up to the derived class to call HadIOFailure() within the PushLuaEventTableTo() method to use it, if relevant.
 */
class BaseDispatchCallResultEventTask : public BaseDispatchEventTask
{
	public:
		BaseDispatchCallResultEventTask();
		virtual ~BaseDispatchCallResultEventTask();

		bool HadIOFailure() const;
		void SetHadIOFailure(bool value);

	private:
		bool fHadIOFailure;
};


/**
  Abstract class used to dispatch a leaderboard related event table to Lua.

  Provides a SetLeaderboardName() method which, when set, will make it available in the Lua event table.
  This is needed since this leaderboard name is not provided by Steam's leaderboard CCallResult event structs.
 */
class BaseDispatchLeaderboardEventTask : public BaseDispatchCallResultEventTask
{
	public:
		BaseDispatchLeaderboardEventTask();
		virtual ~BaseDispatchLeaderboardEventTask();

		const char* GetLeaderboardName() const;
		void SetLeaderboardName(const char* name);

	private:
		std::string fLeaderboardName;
};


/** Dispatches a Steam "GameOverlayActivated_t" event and its data to Lua. */
class DispatchGameOverlayActivatedEventTask : public BaseDispatchEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchGameOverlayActivatedEventTask();
		virtual ~DispatchGameOverlayActivatedEventTask();

		void AcquireEventDataFrom(const GameOverlayActivated_t& steamEventData);
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		bool fWasActivated;
};


/** Dispatches a Steam "LeaderboardScoresDownloaded_t" event and its data to Lua. */
class DispatchLeaderboardScoresDownloadedEventTask : public BaseDispatchLeaderboardEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchLeaderboardScoresDownloadedEventTask();
		virtual ~DispatchLeaderboardScoresDownloadedEventTask();

		void AcquireEventDataFrom(const LeaderboardScoresDownloaded_t& steamEventData);
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		SteamLeaderboard_t fLeaderboardHandle;
		std::vector<LeaderboardEntry_t> fEntryCollection;
};


/** Dispatches a Steam "LeaderboardFindResult_t" event and its data to Lua. */
class DispatchLeaderboardFindResultEventTask : public BaseDispatchLeaderboardEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchLeaderboardFindResultEventTask();
		virtual ~DispatchLeaderboardFindResultEventTask();

		void AcquireEventDataFrom(const LeaderboardFindResult_t& steamEventData);
		void ClearEventData();
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		bool fIsError;
		SteamLeaderboard_t fLeaderboardHandle;
		int fEntryCount;
		ELeaderboardDisplayType fDisplayType;
		ELeaderboardSortMethod fSortMethod;
};


/** Dispatches a Steam "LeaderboardScoreUploaded_t" event and its data to Lua. */
class DispatchLeaderboardScoreUploadEventTask : public BaseDispatchLeaderboardEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchLeaderboardScoreUploadEventTask();
		virtual ~DispatchLeaderboardScoreUploadEventTask();

		void AcquireEventDataFrom(const LeaderboardScoreUploaded_t& steamEventData);
		void ClearEventData();
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		bool fIsError;
		SteamLeaderboard_t fLeaderboardHandle;
		bool fWasScoreChanged;
		int fCurrentGlobalRank;
		int fPreviousGlobalRank;
};


/** Dispatches a Steam "MicroTxnAuthorizationResponse_t" event and its data to Lua. */
class DispatchMicrotransactionAuthorizationResponseEventTask : public BaseDispatchEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchMicrotransactionAuthorizationResponseEventTask();
		virtual ~DispatchMicrotransactionAuthorizationResponseEventTask();

		void AcquireEventDataFrom(const MicroTxnAuthorizationResponse_t& steamEventData);
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		bool fWasAuthorized;
		uint64_t fOrderId;
};


/** Dispatches a Steam "NumberOfCurrentPlayers_t" event and its data to Lua. */
class DispatchNumberOfCurrentPlayersEventTask : public BaseDispatchCallResultEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchNumberOfCurrentPlayersEventTask();
		virtual ~DispatchNumberOfCurrentPlayersEventTask();

		void AcquireEventDataFrom(const NumberOfCurrentPlayers_t& steamEventData);
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		bool fIsError;
		int fPlayerCount;
};


/** Dispatches a Steam "UserAchievementStored_t" event and its data to Lua. */
class DispatchUserAchievementStoredEventTask : public BaseDispatchEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchUserAchievementStoredEventTask();
		virtual ~DispatchUserAchievementStoredEventTask();

		void AcquireEventDataFrom(const UserAchievementStored_t& steamEventData);
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		std::string fAchievementName;
		bool fIsGroup;
		uint32 fCurrentProgress;
		uint32 fMaxProgress;
};


/** Dispatches a Steam "UserStatsReceived_t" event and its data to Lua. */
class DispatchUserStatsReceivedEventTask : public BaseDispatchEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchUserStatsReceivedEventTask();
		virtual ~DispatchUserStatsReceivedEventTask();

		void AcquireEventDataFrom(const UserStatsReceived_t& steamEventData);
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		uint64 fUserIntegerId;
		EResult fSteamResultCode;
};


/** Dispatches a Steam "UserStatsStored_t" event and its data to Lua. */
class DispatchUserStatsStoredEventTask : public BaseDispatchEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchUserStatsStoredEventTask();
		virtual ~DispatchUserStatsStoredEventTask();

		void AcquireEventDataFrom(const UserStatsStored_t& steamEventData);
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		uint64 fUserIntegerId;
		EResult fSteamResultCode;
};


/** Dispatches a Steam "UserStatsUnloaded_t" event and its data to Lua. */
class DispatchUserStatsUnloadedEventTask : public BaseDispatchEventTask
{
	public:
		static const char kLuaEventName[];

		DispatchUserStatsUnloadedEventTask();
		virtual ~DispatchUserStatsUnloadedEventTask();

		void AcquireEventDataFrom(const UserStatsUnloaded_t& steamEventData);
		virtual const char* GetLuaEventName() const;
		virtual bool PushLuaEventTableTo(lua_State* luaStatePointer) const;

	private:
		uint64 fUserIntegerId;
};
