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

-- Stores a reference to achievment display objects created by
-- the updateAchievementImageFor() function below. 
local achievementImage1 = nil
local achievementImage2 = nil

-- Creates/updates 1 of 2 achievement images on-screen.
-- Argument "achievementName" must be set to 1 of the following:
-- * "ACH_TRAVEL_FAR_ACCUM"
-- * "ACH_TRAVEL_FAR_SINGLE"
local function updateAchievementImageFor( achievementName )
	-- Fetch information about the given achievement.
	local imageInfo = steamworks.getAchievementImageInfo( achievementName )
	if imageInfo == nil then
		return
	end

	-- Update the achievment image on-screen.
	if achievementName == "ACH_TRAVEL_FAR_ACCUM" then
		-- Remove the previous display object, if it exists.
		if achievementImage1 then
			achievementImage1:removeEventListener( "tap" )
			achievementImage1:removeSelf()
		end

		-- Display this achievement unscaled at its original pixel resolution.
		achievementImage1 = steamworks.newImageRect(
				imageInfo.imageHandle,
				imageInfo.pixelWidth * display.contentScaleX,
				imageInfo.pixelHeight * display.contentScaleY )
		achievementImage1.x = display.contentWidth * 0.25
		achievementImage1.y = display.contentCenterY

		-- Set up this achievement to be unlocked when clicked.
		function achievementImage1:tap( event )
			steamworks.setAchievementUnlocked( "ACH_TRAVEL_FAR_ACCUM" )
		end
		achievementImage1:addEventListener( "tap" )

	elseif achievementName == "ACH_TRAVEL_FAR_SINGLE" then
		-- Remove the previous display object, if it exists.
		if achievementImage2 then
			achievementImage2:removeEventListener( "tap" )
			achievementImage2:removeSelf()
		end

		-- Display this achievement scaled to a content size of 64x64.
		achievementImage2 = steamworks.newImageRect( imageInfo.imageHandle, 64, 64 )
		achievementImage2.x = display.contentWidth * 0.75
		achievementImage2.y = display.contentCenterY

		-- Set up this achievement to be unlocked when clicked.
		function achievementImage2:tap( event )
			steamworks.setAchievementUnlocked( "ACH_TRAVEL_FAR_SINGLE" )
		end
		achievementImage2:addEventListener( "tap" )
	end
end

-- Set up a listener to be called when an achievement's image/status has changed.
-- Will update the above achievement display objects when:
-- * An achievement image has been loaded/changed.
-- * An achievement has been unlocked.
local function onAchievementUpdated( event )
	updateAchievementImageFor( event.achievementName )
end
steamworks.addEventListener( "achievementImageUpdate", onAchievementUpdated )
steamworks.addEventListener( "achievementInfoUpdate", onAchievementUpdated )

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

	-- Display the following achievements as display objects.
	updateAchievementImageFor( "ACH_TRAVEL_FAR_ACCUM" )
	updateAchievementImageFor( "ACH_TRAVEL_FAR_SINGLE" )

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
text.y = display.viewableContentHeight - ( text.contentHeight * 3 )
local text = display.newText( "(Left achievement is unscaled.  Right achievement is scaled.)", 0, 0 )
text.anchorX = 0
text.anchorY = 0
text.x = display.contentCenterX - ( text.contentWidth / 2 )
text.y = display.viewableContentHeight - ( text.contentHeight * 2 )
