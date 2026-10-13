-- SPDX-License-Identifier: Apache-2.0

-- Renders a miniScene file with Vela (vsrLua) and an ANARI library, with the
-- camera and the renderer parameters of the report which the miniscene device
-- writes next to the file (<file>.mini.txt): the format holds neither.
--
--   vsrLua render_mini.lua --mini scene.mini --out image.png --library barney
--       --campos x y z --camdir x y z --camup x y z --fovy degrees
--       [--width 480] [--height 201] [--samples 64]
--       [--aperture radius --focus distance]
--       [--param name value]...     renderer parameter, number or r,g,b[,a]
--
-- vsrLua has no near clip distance: a camera which stands behind a wall is
-- given with its position moved forward by the near distance of the report.

local function vector(text)
    local values = {}
    for value in string.gmatch(text, "[^,]+") do
        values[#values + 1] = tonumber(value)
    end
    return values
end

local options = {
    library = "barney", width = 480, height = 201, samples = 64, fovy = 40.0,
    campos = {0, 0, 0}, camdir = {0, 0, -1}, camup = {0, 1, 0},
    aperture = 0.0, focus = 1.0,
}
local rendererParams = {}

local i = 1
while arg and i <= #arg do
    local name = arg[i]
    if name == "--mini" or name == "--out" or name == "--library" then
        options[string.sub(name, 3)] = arg[i + 1]
        i = i + 2
    elseif name == "--width" or name == "--height" or name == "--samples"
        or name == "--fovy" or name == "--aperture" or name == "--focus" then
        options[string.sub(name, 3)] = tonumber(arg[i + 1])
        i = i + 2
    elseif name == "--campos" or name == "--camdir" or name == "--camup" then
        options[string.sub(name, 3)] =
            {tonumber(arg[i + 1]), tonumber(arg[i + 2]), tonumber(arg[i + 3])}
        i = i + 4
    elseif name == "--param" then
        local values = vector(arg[i + 2])
        -- One value is an integer parameter as "2" and a float one as "2.0".
        if #values == 1 then
            if string.match(arg[i + 2], "^-?%d+$") then
                rendererParams[arg[i + 1]] = math.tointeger(values[1])
            else
                rendererParams[arg[i + 1]] = values[1] + 0.0
            end
        else
            rendererParams[arg[i + 1]] = values
        end
        i = i + 3
    else
        error("unknown argument: " .. tostring(name))
    end
end
assert(options.mini and options.out, "--mini and --out are required")

print("Loading " .. options.mini)
vsr.io.importMINI(scene, animationMgr, options.mini)

local device = vsr.render.loadDevice(options.library)
local renderIndex = vsr.render.createRenderIndex(scene, device)
renderIndex:populate()

local camera = vsr.CameraSetup.new()
camera.position = vsr.float3(options.campos[1], options.campos[2], options.campos[3])
camera.direction = vsr.normalize(
    vsr.float3(options.camdir[1], options.camdir[2], options.camdir[3]))
camera.up = vsr.float3(options.camup[1], options.camup[2], options.camup[3])
camera.fovy = options.fovy
camera.aspect = options.width / options.height
camera.aperture = options.aperture
camera.focusDistance = options.focus

local renderer = vsr.render.createRenderer(
    options.width, options.height, device, renderIndex, camera, rendererParams)
renderer:renderToFile(options.samples, options.out)
print(string.format("RENDERED %s %dx%d %d samples -> %s",
    options.library, options.width, options.height, options.samples, options.out))
