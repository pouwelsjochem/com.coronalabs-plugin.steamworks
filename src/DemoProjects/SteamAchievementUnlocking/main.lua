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


-- Creates a new display object which will automatically be "filled" with an
-- achievement image associated with the given unique achivement name.
-- Image will automatically change when achievement changes from locked to unlocked.
local function newSteamAchievementImage( achievementName )
	-- Create a rectangle which we'll later fill with the achievement image.
	-- Note that Steam expects you to upload 64x64 pixel achievment images.
	local defaultAchievementPixelSize = 64
	local achievementImage = display.newRect(
			display.contentCenterX, display.contentCenterY,
			defaultAchievementPixelSize * display.contentScaleX,
			defaultAchievementPixelSize * display.contentScaleY )
	if achievementImage == nil then
		return nil
	end
	
	-- Updates the achievement display object's "fill" to show the newest image.
	local function updateAchievementTexture()
		-- Attempt to fetch info about the achievement's image.
		-- Note: This function is likely to return nil on app startup.
		local imageInfo = steamworks.getAchievementImageInfo( achievementName )
		if imageInfo == nil then
			return
		end

		-- Load the achievement image into a new texture resource object.
		local newTexture = steamworks.newTexture( imageInfo.imageHandle )
		if newTexture == nil then
			return
		end

		-- Update the display object to show the achievement image.
		achievementImage.fill =
		{
			type = "image",
			filename = newTexture.filename,
			baseDir = newTexture.baseDir,
		}

		-- Release the texture reference created above.
		newTexture:releaseSelf()
	end

	-- Attempt to update the display object with Steam's current image, if available.
	-- If not currently available, then this function call will trigger Steam to download
	-- it and dispatch an "achievementImageUpdate" event to be received down below.
	updateAchievementTexture()

	-- Set up a listener to be called when an achievement's' image/status has changed.
	local function onAchievementUpdated( event )
		if event.achievementName == achievementName then
			updateAchievementTexture()
		end
	end
	steamworks.addEventListener( "achievementImageUpdate", onAchievementUpdated )
	steamworks.addEventListener( "achievementInfoUpdate", onAchievementUpdated )

	-- Set up a listener to be called when the achievement display object is being removed.
	local function onFinalizing( event )
		-- Remove event listener that were added up above.
		steamworks.removeEventListener( "achievementImageUpdate", onAchievementUpdated )
		steamworks.removeEventListener( "achievementInfoUpdate", onAchievementUpdated )
	end
	achievementImage:addEventListener( "finalize", onFinalizing )

	-- Return the achievement display object created up above.
	return achievementImage
end


-- Called when a user's achievement and stat data has been updated/loaded.
-- Initializes this app when the logged in user's info has been received on startup.
local function onSteamUserProgressUpdated( event )
	-- We're only listening for the logged in user's event.
	if event.userSteamId ~= steamworks.userSteamId then
		return
	end

	-- Lock all achievements and reset all user stats on startup.
	-- Note: This is for testing purposes. Don't do this in a published game.
	steamworks.resetUserProgress()

	-- Display 1st achievement image. (Clicking it will unlock achievement.)
	local achievement1 = newSteamAchievementImage( "ACH_TRAVEL_FAR_ACCUM" )
	if achievement1 then
		achievement1.x = display.contentWidth * 0.25
		achievement1.y = display.contentCenterY
		function achievement1:tap( event )
			-- Unlock this achievement when clicked, if not done already.
			steamworks.setAchievementUnlocked( "ACH_TRAVEL_FAR_ACCUM" )
		end
		achievement1:addEventListener( "tap" )
	end

	-- Display 2nd achievement image. (Clicking it will unlock achievement.)
	local achievement2 = newSteamAchievementImage( "ACH_TRAVEL_FAR_SINGLE" )
	if achievement2 then
		achievement2.x = display.contentWidth * 0.75
		achievement2.y = display.contentCenterY
		function achievement2:tap( event )
			-- Unlock this achievement when clicked, if not done already.
			steamworks.setAchievementUnlocked( "ACH_TRAVEL_FAR_SINGLE" )
		end
		achievement2:addEventListener( "tap" )
	end

	-- Remove this function from Steam plugin's event listener collection.
	-- We only want the first event that happens on app startup.
	steamworks.removeEventListener( "userProgressUpdate", onSteamUserProgressUpdated )
end

-- Set up the above listener to be called when Steam user info has been loaded/updated.
-- This event is guaranteed to be dispatched on app startup if "steamworks.isLoggedOn" is true.
-- Note: We can't access achievement and stat data the 1st time the app has been launched.
--       We have to wait for this event to be received, which indicates the Steam client has
--       now cached this data, making it available for subsequent launches of this app.
steamworks.addEventListener( "userProgressUpdate", onSteamUserProgressUpdated )

-- Display text at the bottom of the screen stating what this app can do.
local text = display.newText( "Click the above achievements to unlock them.", 0, 0 )
text.anchorX = 0
text.anchorY = 0
text.x = display.contentCenterX - ( text.contentWidth / 2 )
text.y = display.viewableContentHeight - ( text.contentHeight * 2 )
