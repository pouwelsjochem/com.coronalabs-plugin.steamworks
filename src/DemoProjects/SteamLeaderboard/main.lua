local steamworks = require("plugin.steamworks")
local json = require('json')
local widget = require('widget')

local page = 1
local perPage = 15
local scope = "Global"
local imgSize = "smallAvatar"

transition.to( display.newGroup( ), {x=100, iterations=-1} )

display.setStatusBar( display.HiddenStatusBar )


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


local function getAvatarSize()
	local pixelSize
	if imgSize == "smallAvatar" then
		pixelSize = 32
	elseif imgSize == "mediumAvatar" then
		pixelSize = 64
	else
		pixelSize = 184
	end
	return pixelSize * display.contentScaleY
end
local rowHeight = getAvatarSize()


local function onRowRender( event )
	local row = event.row
	local w,h = row.contentWidth, row.contentHeight
	local params = row.params

	if params.caption then
		local t = display.newText( {
			parent = row,
			y = h*0.5,
			x = w*0.5,
			text = params.caption ,
			fontSize = 16,
			font = native.systemFontBold,
		} )
		t:setFillColor( {0,0,} )
		return
	end

	local t = display.newText( {
		parent = row,
		y = h*0.5,
		x = 40,
		text = tostring( params.rank ),
		align = "right",
	} )
	t:setFillColor( {0,0,} )
	t:translate( -t.width*0.5, 0 )

	local obj
	local imgDims = getAvatarSize()

	if params.rank == 1 then
		obj = display.newCircle( row, 0, 0, imgDims*0.5 )
	else
		obj = display.newRect( row, 0, 0, imgDims, imgDims )
	end
	if params.texture == nil then
		local imageInfo = steamworks.getUserImageInfo( imgSize, params.user )
		if imageInfo then
			params.texture = steamworks.newTexture( imageInfo.imageHandle )
		end
	end
	if params.texture then
		obj.fill =
		{
			type = "image",
			filename = params.texture.filename,
			baseDir = params.texture.baseDir,
		}
	else
		obj.fill = {0.5}
	end
	obj:translate( 40 + imgDims*0.5 + 5, h*0.5 )
	params.obj = obj

	t = display.newText( {
		parent = row,
		y = h*0.5,
		x = 40 + imgDims + 10,
		text = params.name,
		align = "left",
	} )
	t:setFillColor( {0,0,} )
	t:translate( t.width*0.5, 0 )

	t = display.newText( {
		parent = row,
		y = h*0.5,
		x = w-10,
		text = tostring(params.score),
		align = "right",
		font = "Courier New",
	} )
	t:setFillColor( {0,0,} )
	t:translate( -t.width*0.5, 0 )
end

local leaderboardTable


local function onUserInfoUpdated(event)
	if ( imgSize == "smallAvatar" and event.smallAvatarChanged ) or
	   ( imgSize == "mediumAvatar" and event.mediumAvatarChanged ) or
	   ( imgSize == "largeAvatar" and event.largeAvatarChanged )
	then
		for i=1,leaderboardTable:getNumRows() do
			local row = leaderboardTable:getRowAtIndex(i)
			if row and row.params and row.params.user == event.userSteamId then
				local params = row.params
				local imageInfo = steamworks.getUserImageInfo( imgSize, event.userSteamId )
				if imageInfo then
					local newTexture = steamworks.newTexture( imageInfo.imageHandle )
					if newTexture then
						if params.texture then
							params.texture:releaseSelf()
						end
						params.texture = newTexture

						params.obj.fill =
						{
							type = "image",
							filename = params.texture.filename,
							baseDir = params.texture.baseDir
						}
					end
				end
				break
			end
		end
	end
end
steamworks.addEventListener("userInfoUpdate", onUserInfoUpdated)


local function leaderboardLoaded(event)
	native.setActivityIndicator( false )

	leaderboardTable:deleteAllRows()

	if event.isError then
		leaderboardTable:insertRow( {
			rowHeight = 36,
			params = {
				caption = "Error while fetching leaderboard",
				isCategory = true,
			}
		} )	
	end

	leaderboardTable:insertRow( {
		rowHeight = 36,
		params = {
			caption = event.leaderboardName,
			isCategory = true,
		}
	} )

	if #event.entries == 0 then
		leaderboardTable:insertRow( {
			rowHeight = 36,
			params = {
				caption = "No results for '" .. scope .. "' scope",
				isCategory = true,
			}
		} )		
	end

	for i=1,#event.entries do
		local e = event.entries[i]
		local imageHandle = nil
		local imageInfo = steamworks.getUserImageInfo(imgSize, e.userSteamId)
		if imageInfo then
			imageHandle = imageInfo.imageHandle
		end
		local userInfo = steamworks.getUserInfo(e.userSteamId)
		local name = "[unknown]"
		if userInfo then
			name = userInfo.name
			if userInfo.nickname ~= "" then
				name = name .. " (" .. userInfo.nickname ..")"
			end
		end
		leaderboardTable:insertRow( {
			rowHeight=rowHeight, 
			params={ 
				img = imageHandle,
				rank = e.globalRank,
				score = e.score,
				steamId = e.userSteamId,
				name = name,
				user = e.userSteamId,
			}
		} )
		local row = leaderboardTable:getRowAtIndex( i )
		if row then
			function row:finalize( event )
				if self.params.texture then
					self.params.texture:releaseSelf()
					self.params.texture = nil
				end
			end
			row:addEventListener( "finalize" )
		end
	end
end


local function requestLeaderboard( )
	local startIndex = nil
	local endIndex = nil
	if scope == "Global" then
		startIndex = 1 + perPage * (page - 1)
		endIndex = perPage * page
	else
		startIndex = math.floor(perPage / 2) * -1
		endIndex = math.floor(perPage / 2)
	end
	if steamworks.requestLeaderboardEntries({
		leaderboardName = "Feet Traveled",
		startIndex = startIndex,
		endIndex = endIndex,
		listener = leaderboardLoaded,
		playerScope = scope,
	}) then
		native.setActivityIndicator( true )
	else
		leaderboardTable:deleteAllRows()
		leaderboardTable:insertRow( {
			rowHeight = 36,
			params = {
				caption = "Error: Steam is not running!",
				isCategory = true,
			}
		} )	
	end
end

local function onRowTouch( event )
	local phase = event.phase
	local row = event.target
	if ( "release" == phase ) and row.params.user then
		steamworks.showWebOverlay( "http://steamcommunity.com/profiles/" .. row.params.user )
	end
end


leaderboardTable = widget.newTableView({
	onRowRender = onRowRender,
	onRowTouch = onRowTouch,
	x = display.contentCenterX,
	y = display.contentCenterY - 20,
	width = display.actualContentWidth,
	height = display.actualContentHeight - 40,
})

-- image size control

local function imageSize( event )
	local size = event.target.segmentNumber
	if size == 1 then
		imgSize = "smallAvatar"
--		rowHeight = 18
	elseif size == 2 then
		imgSize = "mediumAvatar"
--		rowHeight = 36
	elseif size == 3 then
		imgSize = "largeAvatar"
--		rowHeight = 54
	end
	rowHeight = getAvatarSize() * 1.25
	requestLeaderboard()
end

local sizeSegment = widget.newSegmentedControl {
    segments = { "Small", "Medium", "Large" },
    defaultSegment = 1,
	segmentWidth = 55,
    onPress = imageSize
}
sizeSegment.x = ( sizeSegment.width * 0.5 ) + 10
sizeSegment.y = display.contentHeight - 20

-- page controls

local function pageListener( event )
	page = event.value
	requestLeaderboard()
end

local pageStepper = widget.newStepper {
    initialValue = 1,
    minimumValue = 1,
    maximumValue = 200,
	timerIncrementSpeed = 2000,
	changeSpeedAtIncrement = 1,
    onPress = pageListener,
	x = display.contentCenterX,
	y = display.contentHeight - 20,
}

-- scope controls

local function leaderboardScope( event )
	local segNum = event.target.segmentNumber
	if segNum == 1 then
		scope = "Global"
	elseif segNum == 2 then
		scope = "GlobalAroundUser"
	elseif segNum == 3 then
		scope = "FriendsOnly"
	end
	pageStepper.isVisible = (scope ~= "GlobalAroundUser")
	requestLeaderboard()
end

local scopeSegment = widget.newSegmentedControl {
    segments = { "Global", "Me", "Friends" },
    defaultSegment = 1,
	segmentWidth = 55,
    onPress = leaderboardScope,
}
scopeSegment.x = ( display.contentWidth - ( scopeSegment.width * 0.5 ) ) - 10
scopeSegment.y = display.contentHeight - 20


requestLeaderboard()

