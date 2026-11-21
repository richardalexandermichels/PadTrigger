local r = reaper
local folder = assert(r.get_ini_file():match("^(.*)[/\\]"))
local function read(name)
    local file = assert(io.open(folder .. "/" .. name, "rb"))
    local value = file:read("*a")
    file:close()
    return value
end

local ok, message = xpcall(function()
    assert(r.CountTracks(0) == 0, "The render test requires a blank project.")
    r.InsertTrackAtIndex(0, false)
    local track = r.GetTrack(0, 0)
    assert(r.SetTrackStateChunk(track, read("fixture.chunk"), false))
    assert(r.TrackFX_GetCount(track) == 1 and r.TrackFX_GetNumParams(track, 0) > 2,
        "REAPER did not instantiate Padomizer.")
    local _, identity = r.TrackFX_GetNamedConfigParm(track, 0, "fx_ident")
    local bundle = read("bundle.txt"):gsub("\\", "/"):lower() .. "/contents/"
    assert(identity:gsub("\\", "/"):lower():find(bundle, 1, true), identity)

    local item = r.AddMediaItemToTrack(track)
    local take = r.AddTakeToMediaItem(item)
    r.SetMediaItemTake_Source(take, assert(r.PCM_Source_CreateFromFile(folder .. "/input.wav")))
    r.SetMediaItemInfo_Value(item, "D_LENGTH", 1)
    r.SetMediaItemInfo_Value(item, "D_FADEINLEN", 0)
    r.SetMediaItemInfo_Value(item, "D_FADEOUTLEN", 0)

    for key, value in pairs({
        SETTINGS = 0, BOUNDSFLAG = 0, STARTPOS = 0, ENDPOS = 1, TAILFLAG = 1,
        TAILMS = 42400, SRATE = 48000, CHANNELS = 2, DITHER = 16, NORMALIZE = 0,
    }) do
        r.GetSetProjectInfo(0, "RENDER_" .. key, value, true)
    end
    r.GetSetProjectInfo_String(0, "RENDER_FILE", folder, true)
    r.GetSetProjectInfo_String(0, "RENDER_FORMAT", "ZXZhdxAA", true) -- 16-bit PCM WAV
    r.GetSetProjectInfo_String(0, "RENDER_PATTERN", "processed", true)
    r.Main_OnCommand(42230, 0) -- Render project with the most recent render settings.
    assert(r.file_exists(folder .. "/processed.wav"), "REAPER did not write the WAV.")
    return r.GetAppVersion()
end, debug.traceback)

local result = assert(io.open(folder .. "/result.tmp", "w"))
result:write(ok and ("PASS " .. message) or ("FAIL " .. message))
result:close()
assert(os.rename(folder .. "/result.tmp", folder .. "/result.txt"))
