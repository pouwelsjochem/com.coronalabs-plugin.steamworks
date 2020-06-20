// ----------------------------------------------------------------------------
// 
// SteamUserImageType.h
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#pragma once


/**
  Indicates a type of image assigned to a user such as small, medium, or large avatar image.

  Provides predefined constants kAvatarSmall, kAvatarMedium, and kAvatarLarge allowing the caller
  too access their unique string IDs used in Lua as well as their default pixel width and height
  size on Steam. This class also provides static function FromCoronaStringId() for converting
  a string ID back to one of this class' predefined constant image type.
 */
class SteamUserImageType final
{
	private:
		/**
		  Creates a new image type using the given unique string ID and default pixel size.

		  This constructor is private and is only used to create this class' predefined
		  constants such as kAvatarSmall, kAvatarMedium, and kAvatarLarge.
		  @param coronaStringId Unique string ID assigned to the image type.
		  @param pixelWidth Default width of the image in pixels.
		  @param pixelHeight Default height of the image in pixels.
		  */
		SteamUserImageType(const char* coronaStringId, int pixelWidth, int pixelHeight);

	public:
		/** Indicates that the image type is unknown or invalid. */
		static const SteamUserImageType kUnknown;

		/** Indicates that that this type references a small 32x32 pixel user avatar image. */
		static const SteamUserImageType kAvatarSmall;

		/** Indicates that that this type references a medium 64x64 pixel user avatar image. */
		static const SteamUserImageType kAvatarMedium;

		/** Indicates that that this type references a large 184x184 pixel user avatar image. */
		static const SteamUserImageType kAvatarLarge;



		/** Creates a value type initialized to unknown. */
		SteamUserImageType();

		/** Destroys this object. */
		virtual ~SteamUserImageType();

		/**
		  Gets a unique string ID used to identify this image type.
		  This string ID is defined by this Corona plugin and not by Steam/Valve.
		  @return Returns the image type's unique string ID.
		 */
		const char* GetCoronaStringId() const;

		/**
		  Gets the default pixel width of this image type on Steam.
		  @return Returns the default width of this image type in pixels.

		          Returns zero if the image type is kUnknown.
		 */
		int GetDefaultPixelWidth() const;

		/**
		  Gets the default pixel height of this image type on Steam.
		  @return Returns the default height of this image type in pixels.

		          Returns zero if the image type is kUnknown.
		 */
		int GetDefaultPixelHeight() const;

		/**
		  Determines if this image type matches the given type.
		  @param imageType The image type to be compared with.
		  @return Returns true if the types match. Returns false if they don't match.
		 */
		bool operator==(const SteamUserImageType& imageType) const;

		/**
		  Determines if this image type does not match the given type.
		  @param imageType The image type to be compared with.
		  @return Returns true if the types do not match. Returns false if they do.
		 */
		bool operator!=(const SteamUserImageType& imageType) const;

		/**
		  Returns a new instance of this class matching the given string ID.
		  @param stringId Unique string identifying the image type such as
		                  "smallAvatar", "mediumAvatar", or "largeAvatar".
		  @return Returns a new image type instance matching the string ID such as
		          kAvatarSmall, kAvatarMedium, or kAvatarLarge.

		          Returns kUnknown if given an unknown string ID or null.
		 */
		static SteamUserImageType FromCoronaStringId(const char* stringId);

	private:
		/** Unique string ID assigned to the value type such as "avatarSmall", "avatarMedium", etc. */
		const char* fCoronaStringId;

		/** The image's default pixel width on Steam. */
		int fDefaultPixelWidth;

		/** The image's default pixel height on Steam. */
		int fDefaultPixelHeight;
};
