// ----------------------------------------------------------------------------
// 
// SteamStatValueType.h
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#pragma once


/**
  Indicates the type of value a Steam stat can store such as integer, float, and average rate.

  Provides predefined constants kInteger, kFloat, and kAverageRate for identifying the type which
  also provide Corona plugin defined string IDs intended to be used by Lua. Also provides static
  function FromCoronaStringId() for converting a string ID back to a predefined constant type.

  This class is intended to be used by this plugin's Lua setUserStatValues() and getUserStatValue()
  functions.
 */
class SteamStatValueType final
{
	private:
		/**
		  Creates a new stat value type using the given unique string ID.

		  This constructor is private and is only used to create this class' predefined
		  constants such as kInteger, kFloat, and kAverageRate.
		  @param coronaStringId Unique string ID assigned to the value type.
		 */
		SteamStatValueType(const char* coronaStringId);

	public:
		/** Indicates that the Steam stat value type is unknown. */
		static const SteamStatValueType kUnknown;

		/** Indicates that the Steam stat value type is an integer. */
		static const SteamStatValueType kInteger;

		/** Indicates that the Steam stat value type is a float. */
		static const SteamStatValueType kFloat;

		/** Indicates that the Steam stat value type is an average rate. */
		static const SteamStatValueType kAverageRate;
		

		/** Creates a value type initialized to unknown. */
		SteamStatValueType();

		/** Destroys this object. */
		virtual ~SteamStatValueType();

		/**
		  Gets a unique string ID used to identify this stat value type.
		  This string ID is defined by this Corona plugin and not by Steam/Valve.
		  @return Returns the value type's unique string ID.
		 */
		const char* GetCoronaStringId() const;

		/**
		  Determines if this value type matches the given value type.
		  @param valueType The value type to be compared with.
		  @return Returns true if the types match. Returns false if they don't match.
		 */
		bool operator==(const SteamStatValueType& valueType) const;

		/**
		  Determines if this value type does not match the given value type.
		  @param valueType The value type to be compared with.
		  @return Returns true if the types do not match. Returns false if they do.
		 */
		bool operator!=(const SteamStatValueType& valueType) const;

		/**
		  Returns a new instance of this class matching the given string ID.
		  @param stringId Unique string ID identifying the value type such as
		                  "int", "float", or "averageRate".
		  @return Returns a new value type instance matching the string ID such as
		          kInteger, kFloat, or kAverageRate.

		          Returns kUnknown if given an unknown string ID or null.
		 */
		static SteamStatValueType FromCoronaStringId(const char* stringId);

	private:
		/** Unique string ID assigned to the value type such as "int", "float", etc. */
		const char* fCoronaStringId;
};
