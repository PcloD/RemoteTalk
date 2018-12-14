set PLUGIN32_DIR="..\Assets\IST\RemoteTalk\Plugins\x86\"
set PLUGIN64_DIR="..\Assets\IST\RemoteTalk\Plugins\x86_64\"
set ASSET_DIR="..\Assets\StreamingAssets\RemoteTalkAssets\"
set OUT32_DIR="_out\Win32_Master\"
set OUT64_DIR="_out\x64_Master\"

xcopy /Y "%OUT32_DIR%RemoteTalkCeVIOCS.exe" "%ASSET_DIR%"
xcopy /Y "%OUT32_DIR%RemoteTalkCeVIOCSHook.dll" "%ASSET_DIR%"
xcopy /Y "%OUT32_DIR%RemoteTalkCeVIOCSManaged.dll" "%ASSET_DIR%"
xcopy /Y "%OUT32_DIR%RemoteTalkVOICEROID2.exe" "%ASSET_DIR%"
xcopy /Y "%OUT32_DIR%RemoteTalkVOICEROID2Hook.dll" "%ASSET_DIR%"
xcopy /Y "%OUT32_DIR%RemoteTalkVOICEROID2Managed.dll" "%ASSET_DIR%"
xcopy /Y "%OUT32_DIR%RemoteTalkClient.dll" "%PLUGIN32_DIR%"
xcopy /Y "%OUT32_DIR%RemoteTalkSAPI.dll" "%PLUGIN32_DIR%"
xcopy /Y "%OUT64_DIR%RemoteTalkClient.dll" "%PLUGIN64_DIR%"
xcopy /Y "%OUT64_DIR%RemoteTalkSAPI.dll" "%PLUGIN64_DIR%"