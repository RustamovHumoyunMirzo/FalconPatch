local module = {}

function module.runtime_version()
    return fpatch.version()
end

return module
