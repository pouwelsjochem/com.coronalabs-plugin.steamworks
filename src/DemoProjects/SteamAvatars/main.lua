-- Load the Steamworks plugin.
local steamworks = require( "plugin.steamworks" )


-- Do not continue if failed to find a logged in Steam user.
-- This means the Steam client is not running or the user is not logged in to it.
if steamworks.isLoggedOn == false then
	-- Handle the native alert's result displayed down below.
	local function onAlertHandler( event )
		-- If the user clicked the "Log In" button,
		-- then display the Steam client via its custom URL scheme.
		if ( event.action == "clicked" ) and ( event.index == 1 ) then
			system.openURL( "steam:" )
		end

		-- Exit this app, regardless of which button was pressed.
		-- The Steam client must be running when this app starts up.
		native.requestExit()
	end

	-- Display a native alert asking the user to log into Steam.
	local message =
			"You must log into Steam in order to play this game.\n" ..
			"After logging in, you must then relaunch this app."
	native.showAlert( "Warning", message, { "Log In", "Close" }, onAlertHandler )

	-- Exit out of the "main.lua" file.
	-- The screen will be black. Only the above native alert will be shown on top.
	return
end


-- Creates a new display object which will be "filled" with a Steam user avatar image
-- and this image will automatically change when the user's changes his/her avatar.
-- Argument "userImageType" must be set to "smallAvatar", "mediumAvatar", or "largeAvatar".
-- Argument "userSteamId" is optional and will default to logged in user if not set.
local function newSteamAvatarImage( userImageType, userSteamId )
	-- Determine the pixel size of the requested image type.
	local defaultPixelSize
	if userImageType == "smallAvatar" then
		defaultPixelSize = 32
	elseif userImageType == "mediumAvatar" then
		defaultPixelSize = 64
	elseif userImageType == "largeAvatar" then
		defaultPixelSize = 184
	else
		return nil
	end

	-- If a user ID argument was not provided, then default to the logged in user.
	if userSteamId == nil then
		userSteamId = steamworks.userSteamId
	end

	-- Create a rectangle which we'll later fill with the avatar image.
	local avatarImage = display.newRect(
			display.contentCenterX, display.contentCenterY,
			defaultPixelSize * display.contentScaleX,
			defaultPixelSize * display.contentScaleY )
	if avatarImage == nil then
		return nil
	end
	
	-- Updates the avatar display object's "fill" to show the newest image.
	local function updateAvatarTexture()
		-- Attempt to fetch info about the user's image.
		-- Note: This function is likely to return nil for large images
		--       or the first time you request a friend's image.
		local imageInfo = steamworks.getUserImageInfo( userImageType, userSteamId )
		if imageInfo == nil then
			return
		end

		-- Load the avatar image into a new texture resource object.
		local newTexture = steamworks.newTexture( imageInfo.imageHandle )
		if newTexture == nil then
			return
		end

		-- Update the display object to show the user image.
		avatarImage.fill =
		{
			type = "image",
			filename = newTexture.filename,
			baseDir = newTexture.baseDir,
		}

		-- Release the texture reference.
		newTexture:releaseSelf()
	end

	-- Attempt to update the display object with Steam's current image, if available.
	-- If not currently available, then this function call will trigger Steam to download
	-- it and dispatch a "userInfoUpdate" event to be received down below. 
	updateAvatarTexture()

	-- Set up a listener to be called when a user's info has changed.
	local function onUserInfoUpdated( event )
		-- Update the "avatar" display object image, but only if a particular user's
		-- avatar image size has been flagged as changed. (This is an optimization.)
		if userSteamId == event.userSteamId then
			if ( userImageType == "smallAvatar" and event.smallAvatarChanged ) or
			   ( userImageType == "mediumAvatar" and event.mediumAvatarChanged ) or
			   ( userImageType == "largeAvatar" and event.largeAvatarChanged )
			then
				updateAvatarTexture()
			end
		end
	end
	steamworks.addEventListener( "userInfoUpdate", onUserInfoUpdated )

	-- Set up a listener to be called when the avatar display object is being removed.
	local function onFinalizing( event )
		-- Remove event listener that was added up above.
		steamworks.removeEventListener( "userInfoUpdate", onUserInfoUpdated )
	end
	avatarImage:addEventListener( "finalize", onFinalizing )

	-- Return the avatar display object created up above.
	return avatarImage
end

-- Handle the native alert's selection result displayed down below.
local function onAvatarSizeSelected( event )
	-- Get the avatar size selection from the alert dialog.
	-- Will default to large avatars if dialog was canceled.
	local avatarImageType = "largeAvatar"
	if event.action == "clicked" then
		if event.index == 1 then
			avatarImageType = "smallAvatar"
		elseif event.index == 2 then
			avatarImageType = "mediumAvatar"
		end
	end

	-- Display the logged in user's avatar in the middle of the window.
	-- Note: This uses our newSteamAvatarImage() helper function above.
	local userAvatar = newSteamAvatarImage( avatarImageType )
	if userAvatar then
		userAvatar.x = display.viewableContentWidth * 0.5
		userAvatar.y = display.contentCenterY
	end

	-- Display the main Corona Labs account avatar on the left side of the window.
	-- Note: This uses our newSteamAvatarImage() helper function above.
	local userAvatar = newSteamAvatarImage( avatarImageType, "76561198206901509" )
	if userAvatar then
		userAvatar.x = display.viewableContentWidth * 0.25
		userAvatar.y = display.contentCenterY
	end

	-- Display the Corona QA account avatar in the right side of the window.
	-- Note: This uses our newSteamAvatarImage() helper function above.
	local userAvatar = newSteamAvatarImage( avatarImageType, "76561198316933388" )
	if userAvatar then
		userAvatar.x = display.viewableContentWidth * 0.75
		userAvatar.y = display.contentCenterY
	end
end

-- Display a native alert asking the user to log into Steam.
native.showAlert( "Select Avatar Size", "", { "Small", "Medium", "Large" }, onAvatarSizeSelected )
