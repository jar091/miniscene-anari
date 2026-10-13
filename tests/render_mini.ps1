param(
    [Parameter(Mandatory = $true)][string]$Mini,
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][string]$VsrLua,
    [string]$Library = 'barney',
    # Directories with the ANARI libraries and the runtimes Vela was built with.
    # Separated by ';'.
    [string]$RuntimePath = '',
    [int]$Width = 480,
    [int]$Height = 201,
    [int]$Samples = 64,
    # Three numbers separated by ','.
    [string]$CamPos = '0,0,0',
    [string]$CamDir = '0,0,-1',
    [string]$CamUp = '0,1,0',
    [double]$Fovy = 40,
    # Renderer parameters as "name=value" separated by ';', the value a number
    # or r,g,b[,a].
    [string]$Param = ''
)

# Renders a miniScene file with Vela's vsrLua and one ANARI library, see
# render_mini.lua. The camera and the renderer parameters come from the report
# which the miniscene device writes next to the file.
$env:PATH = "$RuntimePath;$env:PATH"
$arguments = @("$PSScriptRoot/render_mini.lua", '--mini', $Mini, '--out', $Output,
    '--library', $Library, '--width', $Width, '--height', $Height, '--samples', $Samples,
    '--campos') + $CamPos.Split(',') + @('--camdir') + $CamDir.Split(',') +
    @('--camup') + $CamUp.Split(',') + @('--fovy', $Fovy)
foreach ($entry in $Param.Split(';', [StringSplitOptions]::RemoveEmptyEntries)) {
    $name, $value = $entry -split '=', 2
    $arguments += @('--param', $name.Trim(), $value.Trim())
}
$watch = [Diagnostics.Stopwatch]::StartNew()
& $VsrLua @arguments *> ([IO.Path]::ChangeExtension($Output, '.log'))
$code = $LASTEXITCODE
Write-Host ("VSRLUA {0} exit={1} in {2:F1} s -> {3}" -f $Library, $code, $watch.Elapsed.TotalSeconds, $Output)
exit $code
