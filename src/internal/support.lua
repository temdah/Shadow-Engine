-- Public one-action support report UI. Native worker owns stages and files.
local api = {}
local labels = {
    [1] = "Stage 1/4: measuring shadow workload.",
    [2] = "Stage 2/4: recording shadows. Drive through the problem area.",
    [3] = "Stage 3/4: collecting the vehicle-shadow sample.",
    [4] = "Stage 4/4: saving your support report."
}
function api.parse(text, status)
    if type(text) ~= "string" or #text >= 256 or not status then return end
    local pid, session, serial, phase, elapsed, gaps, errorCode, name = text:match(
        "^SEB1 (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) ([%w%._%-]+)\n$")
    if pid ~= status.pid or session ~= status.session then return end
    if not serial or #serial > 8 or #phase > 1 or #elapsed > 7 or #gaps > 10 or #errorCode > 10 then return end
    serial, phase, elapsed = tonumber(serial), tonumber(phase), tonumber(elapsed)
    if serial < 1 or serial > 16777215 or phase < 1 or phase > 7 or elapsed > 4294967 then return end
    if tonumber(gaps) > 4294967295 or tonumber(errorCode) > 4294967295 then return end
    if name ~= "none" and name ~= "ShadowEngine-Support-" .. pid .. "-" .. session .. "-" .. string.format("%.0f", serial) .. ".zip" then return end
    if (phase == 5 or phase == 6) ~= (name ~= "none") then return end
    return { pid=pid, session=session, serial=serial, phase=phase, elapsed=elapsed,
        gaps=tonumber(gaps), errorCode=tonumber(errorCode), name=name }
end
function api.create(deps)
    local ui = { message = "Create one report, reproduce the problem, then share the saved ZIP.", request = nil }
    local function show(message, announce)
        if ui.message == message then return end
        ui.message = message
        if ui.root then pcall(function() ui.root:Rerender() end) end
        if announce then deps.notify(message) end
    end
    local function read(status)
        if not status or not status.path then return end
        local path = status.path:gsub("ShadowEngineDiagnostics%.status$", "ShadowEngineSupport.status")
        if path == status.path then return end
        local file = io.open(path, "rb")
        if not file then return end
        local text = file:read(256); file:close()
        return api.parse(text, status)
    end
    function ui.poll(status)
        if not status then return end
        if ui.current and (ui.current.pid ~= status.pid or ui.current.session ~= status.session) then ui.current = nil end
        local request = ui.request
        if request and (request.pid ~= status.pid or request.session ~= status.session) then
            ui.request = nil; ui.current = nil
            show("The game session changed. The previous report's completion is unconfirmed.", true)
            return
        end
        if request and status.processedSeq == request.sequence and status.result < 0 then
            ui.request = nil
            show("The report could not start. Wait for any active capture to finish and use the matching Shadow Engine build.", true)
            return
        end
        local report = read(status)
        if not report or (request and report.serial <= request.previous) then return end
        local changed = not ui.current or ui.current.serial ~= report.serial or ui.current.phase ~= report.phase
        if not changed and report.phase >= 5 and not request then return end
        ui.current = report
        if report.phase <= 4 then
            show(labels[report.phase] .. " Elapsed: " .. string.format("%.0f", report.elapsed) .. " seconds.", false)
        elseif report.phase == 5 or report.phase == 6 then
            ui.request = nil
            local message = (report.phase == 6 and "Report saved with missing or incomplete sections: " or "Report saved: ") .. report.name
            message = message .. ". Find it in Watch_Dogs/bin and share that ZIP with us."
            show(message, changed)
        else
            ui.request = nil
            show("Could not save the support report. Error " .. string.format("%.0f", report.errorCode) .. ". Existing logs remain in Watch_Dogs/bin.", changed)
        end
    end
    function ui.start()
        if ui.request or (ui.current and ui.current.phase <= 4) then
            deps.notify("A support report is already running. Keep playing until the saved notification."); return
        end
        local status, sequence = deps.submit()
        if not status then return end
        local previous = read(status)
        ui.request = { pid=status.pid, session=status.session, sequence=sequence, previous=previous and previous.serial or 0 }
        show("Support report queued. Keep playing and reproduce the problem; it normally takes about 80 seconds. Keep shadow settings unchanged.", true)
    end
    function ui.render(root)
        ui.root = root
        root:Text("Support report", { size = 18, bold = true })
        root:Text("One recording collects the diagnostic stages and saves one ZIP to share with us.")
        root:Button("Create support report", deps.guard(ui.start))
        root:Text(ui.message)
        root:Text("You may close this menu while recording. Keep the game running until the report is saved.")
    end
    function ui.remove() ui.root = nil; ui.request = nil end
    return ui
end
return api
