-- @description PadTrigger - Record audio
local r = reaper
local sourceKey = "P_EXT:PadTriggerSource"
local record = 1013

local function sourceGuid(track)
    local _, guid = r.GetSetMediaTrackInfo_String(track, sourceKey, "", false)
    return guid
end

local function linkedTrack(project, guid)
    for i = 0, r.CountTracks(project) - 1 do
        local track = r.GetTrack(project, i)
        if r.GetTrackGUID(track) == guid then return track end
    end
end

local function checkPadTrigger(track)
    local midiMode
    for fx = 0, r.TrackFX_GetCount(track) - 1 do
        local _, name = r.TrackFX_GetFXName(track, fx, "")
        if midiMode == true then
            local _, instrument = r.TrackFX_GetNamedConfigParm(track, fx, "is_instrument")
            if instrument == "1" and r.TrackFX_GetEnabled(track, fx) and not r.TrackFX_GetOffline(track, fx) then
                return
            end
        elseif (name:find("Padomizer", 1, true) or name:find("Pad Trigger", 1, true))
            and r.TrackFX_GetEnabled(track, fx) and not r.TrackFX_GetOffline(track, fx) then
            for parameter = 0, r.TrackFX_GetNumParams(track, fx) - 1 do
                local _, label = r.TrackFX_GetParamName(track, fx, parameter, "")
                if label == "Sound mode" then
                    local _, mode = r.TrackFX_GetFormattedParamValue(track, fx, parameter, "")
                    if mode == "WAV Sample" then return end
                    if mode == "MIDI Notes" then midiMode = true end
                    break
                end
            end
        end
    end
    if midiMode then error("Place an enabled instrument after PadTrigger in the track FX chain.", 0) end
    error("Select a track with PadTrigger enabled in its regular FX chain, or its linked print track.", 0)
end

local function prepare(project, selected, movingExistingTake)
    assert(r.GetPlayStateEx(project) == 0, "Stop playback or recording before running this action.")
    assert(selected, "Select one PadTrigger track or its linked print track.")
    local linked = sourceGuid(selected)
    local source = linked == "" and selected or linkedTrack(project, linked)
    assert(source, "The selected print track's PadTrigger source is missing.")
    assert(movingExistingTake or r.CountTrackMediaItems(source) == 0,
        "Move existing items off the PadTrigger mic track first so they will not trigger the instrument again.")
    assert(r.GetMediaTrackInfo_Value(source, "I_RECINPUT") >= 0
        and r.GetMediaTrackInfo_Value(source, "I_RECINPUT") < 1024,
        "Select the mic's mono hardware input on the PadTrigger track.")
    assert(r.GetMediaTrackInfo_Value(source, "I_FXEN") ~= 0, "Enable the PadTrigger track's FX.")
    checkPadTrigger(source)

    local guid = r.GetTrackGUID(source)
    local printTrack
    for i = 0, r.CountTracks(project) - 1 do
        local track = r.GetTrack(project, i)
        if sourceGuid(track) == guid then
            assert(not printTrack, "More than one print track is linked to this PadTrigger track.")
            printTrack = track
        end
    end

    local send
    for i = 0, r.GetTrackNumSends(source, 0) - 1 do
        if r.GetTrackSendInfo_Value(source, 0, i, "P_DESTTRACK") == printTrack then
            assert(not send, "More than one send feeds the PadTrigger print track.")
            send = i
        end
    end
    if not printTrack then
        local position = r.CountTracks(project)
        r.InsertTrackAtIndex(position, false)
        printTrack = r.GetTrack(project, position)
        local _, name = r.GetTrackName(source)
        r.GetSetMediaTrackInfo_String(printTrack, "P_NAME", name .. " - stereo print", true)
        r.GetSetMediaTrackInfo_String(printTrack, sourceKey, guid, true)
        r.SetMediaTrackInfo_Value(printTrack, "I_HEIGHTOVERRIDE", 140)
    end
    local setSend = r.SetTrackSendInfo_Value
    if not send then
        send = r.CreateTrackSend(source, printTrack)
        assert(send >= 0, "REAPER could not create the post-FX audio send.")
        setSend(source, 0, send, "D_VOL", 1.0)
    end
    setSend(source, 0, send, "I_SENDMODE", 3) -- post-FX, pre-fader
    setSend(source, 0, send, "I_SRCCHAN", 0)
    setSend(source, 0, send, "I_DSTCHAN", 0)
    setSend(source, 0, send, "I_MIDIFLAGS", 31) -- audio only
    setSend(source, 0, send, "B_MUTE", 0)

    local set = r.SetMediaTrackInfo_Value
    set(source, "I_RECMODE", 2) -- monitor the mic without recording it
    set(source, "I_RECMON", 2) -- tape-style input monitoring
    set(source, "B_MAINSEND", 0) -- monitor via the print track
    set(source, "I_RECARM", 1)
    set(printTrack, "I_RECINPUT", -1)
    set(printTrack, "I_RECMODE", 3) -- latency-compensated stereo output
    set(printTrack, "I_RECMODE_FLAGS", 2) -- post-FX, pre-fader
    set(printTrack, "I_RECMONITEMS", 0)
    set(printTrack, "I_RECARM", 1)
    r.SetOnlyTrackSelected(printTrack)
    r.TrackList_AdjustWindows(false)
    r.UpdateArrange()
    return printTrack
end

-- A caller that has already printed an existing take can move it after preparing the destination.
if (...) == "prepare" then return prepare end

local project = r.EnumProjects(-1, "")
if r.CountSelectedTracks(project) ~= 1 then
    r.MB("Select one PadTrigger track or its linked print track.", "PadTrigger - Record audio", 0)
    return
end
local selected = r.GetSelectedTrack(project, 0)
local name = "Set up PadTrigger stereo recording"
local previousChangeCount = r.GetProjectStateChangeCount(project)
r.Undo_BeginBlock2(project)
local ok, reason = pcall(prepare, project, selected, false)
r.Undo_EndBlock2(project, name, -1)
if not ok then
    if r.GetProjectStateChangeCount(project) ~= previousChangeCount and r.Undo_CanUndo2(project) == name then
        r.Undo_DoUndo2(project)
    end
    local message = tostring(reason)
    r.MB(message:match("^.-:%d+: (.*)$") or message, "PadTrigger - Record audio", 0)
    return
end
r.Main_OnCommandEx(record, 0, project)

