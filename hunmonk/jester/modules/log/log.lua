module(..., package.seeall)

function log_console(action)
  local message = action.message
  local level = action.level or "info"
  if message then
    jester.log(message, "JESTER LOG", level)
  end
end

function log_file(action)
  local message = action.message
  local file = action.file
  local level = action.level or "INFO"
  if file and message then
    local destination, file_error = io.open(file, "a")
    if destination then
      message = os.date("%Y-%m-%d %H:%M:%S") .. " " .. level .. ": " .. message .. "\n"
      destination:write(message)
      destination:close()
    else
      jester.debug_log("Failed writing to log file '%s'!: %s", file, file_error)
    end
  end
end

