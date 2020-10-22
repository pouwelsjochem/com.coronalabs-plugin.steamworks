set -e

pluginPath=$(pwd)

xcodebuild -project "$pluginPath/Plugin.xcodeproj" -configuration Release clean
xcodebuild -project "$pluginPath/Plugin.xcodeproj" -configuration Release

OUTPUT="$pluginPath/out"
mkdir -p "$OUTPUT"
cp ~/Library/Application\ Support/Corona/Simulator/Plugins/plugin_steamworks.dylib "$OUTPUT"
lipo ~/Library/Application\ Support/Corona/Simulator/Plugins/libsteam_api.dylib -thin x86_64  -output "$OUTPUT/libsteam_api.dylib"
