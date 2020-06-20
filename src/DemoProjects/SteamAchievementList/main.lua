local steamworks = require("plugin.steamworks")
local json = require('json')
local widget = require('widget')


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

	local obj = nil
	if params.img then
		obj = steamworks.newImageRect(row, params.img, 64, 64 )
	else
		obj = display.newRect( row, 0, 0, 64, 64 )
		obj.fill = {0.2}
	end
	obj:translate( 58, h*0.5 )
	obj.stroke =  { 0, 0, 0 }
	obj.strokeWidth = 0.6
	params.obj = obj

	t = display.newText( {
		parent = row,
		y = h*0.30,
		x = 58+32+10,
		text = params.name,
		font = native.systemFontBold,
		align = "left",
	} )
	t:setFillColor( {0,0,} )
	t:translate( t.width*0.5, 0 )

	t = display.newText( {
		parent = row,
		y = h*0.7,
		x = 58+32+10,
		text = params.desc,
		align = "left",
	} )
	t:setFillColor( {0,0,} )
	t:translate( t.width*0.5, 0 )

end

local achievementsTable = widget.newTableView({
	onRowRender = onRowRender
})


local function onAchievementImageUpdated(event)	
	for i=1,achievementsTable:getNumRows() do
		local row = achievementsTable:getRowAtIndex(i)
		if row and row.params and row.params.aName == event.achievementName then
			local params = row.params
			params.img = event.imageInfo.imageHandle
			local tex = steamworks.newTexture(params.img)
			params.obj.fill = {type="image", filename=tex.filename, baseDir=tex.baseDir}
			tex:releaseSelf()
			break
		end
	end
end
steamworks.addEventListener("achievementImageUpdate", onAchievementImageUpdated)

local achievementNames = steamworks.getAchievementNames()
if #achievementNames > 0 then

	achievementsTable:insertRow( {
		rowHeight = 35,
		params = {
			caption = "Achievements",
			isCategory = true,
		}
	} )

	for i=1,#achievementNames do
		local aName = achievementNames[i]
		local achiv = steamworks.getAchievementInfo(aName)
		if not achiv.hidden then
			local imageHandle = nil
			local imageInfo = steamworks.getAchievementImageInfo(aName)
			if imageInfo then
				imageHandle = imageInfo.imageHandle
			end
			achievementsTable:insertRow( {
				rowHeight=67, 
				params={ 
					img = imageHandle,
					name = achiv.localizedName,
					desc = achiv.localizedDescription,
					aName = aName,
				}
			} )

		end                    
	end
else
	achievementsTable:insertRow( {
		rowHeight = 35,
		params = {
			caption = "Error: Steam must be running!",
			isCategory = true,
		}
	} )
end
