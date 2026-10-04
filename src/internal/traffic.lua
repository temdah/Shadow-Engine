-- Suite owns only ambient traffic suppression it successfully requested.
-- No budget changes during registration; parked/scripted traffic is untouched.
return function(notify, log)
    local control = { suppressed = false }
    function control.setEnabled(enabled)
        if enabled and not control.suppressed then return true end
        local ok, err
        if enabled then ok, err = pcall(RestoreBudgets)
        else ok, err = pcall(ChangeVehiclesBudget, 0) end
        if ok then control.suppressed = not enabled end
        log("TRAFFIC enabled=" .. tostring(enabled) .. " success=" .. tostring(ok) ..
            " detail=" .. tostring(err))
        notify(ok and (enabled and "Ambient moving traffic restored" or
            "Ambient moving-vehicle spawns disabled; existing/parked/scripted cars remain") or
            "Traffic request failed; actual state unchanged")
        return ok
    end
    function control.restore()
        if control.suppressed then return control.setEnabled(true) end
        return true
    end
    return control
end
