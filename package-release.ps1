# Builds the release archives in dist\ from an existing Release build (run build.cmd first):
#   QuantumBreak-RawInputMouseFix-v<version>-plugin-only.zip
#   QuantumBreak-RawInputMouseFix-v<version>-with-Ultimate-ASI-Loader-v9.7.4.zip   (with -LoaderPath)
# The version comes from CMakeLists.txt and must match the built plugin's version resource.
param(
    [string]$LoaderPath
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$buildDirectory = Join-Path $repositoryRoot 'build'
$distDirectory = Join-Path $repositoryRoot 'dist'
$stagingRoot = Join-Path $buildDirectory 'release-staging'
$plugin = Join-Path $buildDirectory 'Release\QBMouseFix.asi'
$configuration = Join-Path $repositoryRoot 'config\QBMouseFix.ini'
$readmeTemplate = Join-Path $repositoryRoot 'release-readme.txt'
$projectLicense = Join-Path $repositoryRoot 'LICENSE'
$thirdPartyNotice = Join-Path $repositoryRoot 'THIRD-PARTY-NOTICES.txt'
$cmakeLists = Join-Path $repositoryRoot 'CMakeLists.txt'
# Ultimate ASI Loader v9.7.4, Ultimate-ASI-Loader-NoPDB_x64.zip, dinput8.dll
$expectedLoaderHash = '031A3E5576D91DCE1E438D36B9A3D462C7334AB4791990A8FF1E3DDC0E132DAF'
$loaderVersion = 'v9.7.4'

foreach ($requiredFile in @($plugin, $configuration, $readmeTemplate, $projectLicense, $thirdPartyNotice, $cmakeLists)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required release input is missing: $requiredFile"
    }
}

$versionMatch = Select-String -LiteralPath $cmakeLists -Pattern 'project\(QBMouseFix VERSION (\d+\.\d+\.\d+)'
if (-not $versionMatch) { throw "No project VERSION found in $cmakeLists" }
$Version = $versionMatch.Matches[0].Groups[1].Value
$pluginVersion = (Get-Item -LiteralPath $plugin).VersionInfo.ProductVersion
if ($pluginVersion -ne $Version) {
    throw "Built plugin is version $pluginVersion but CMakeLists.txt says $Version; rebuild first"
}

function Confirm-ChildPath {
    param([string]$Path, [string]$Parent)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullParent, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside $fullParent : $fullPath"
    }
    return $fullPath
}

function Reset-StagingDirectory {
    param([string]$Path)

    $safePath = Confirm-ChildPath -Path $Path -Parent $buildDirectory
    if (Test-Path -LiteralPath $safePath) {
        Remove-Item -LiteralPath $safePath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $safePath | Out-Null
    return $safePath
}

function Write-PayloadHashes {
    param([string]$Directory)

    $lines = Get-ChildItem -LiteralPath $Directory -File |
        Where-Object Name -ne 'SHA256SUMS.txt' |
        Sort-Object Name |
        ForEach-Object {
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            '{0}  {1}' -f $hash.ToLowerInvariant(), $_.Name
        }
    Set-Content -LiteralPath (Join-Path $Directory 'SHA256SUMS.txt') -Value $lines -Encoding Ascii
}

function New-ReleaseArchive {
    param(
        [string]$Name,
        [string]$Stage,
        [bool]$IncludeLoader,
        [string]$ResolvedLoader
    )

    $stagePath = Reset-StagingDirectory -Path $Stage
    Copy-Item -LiteralPath $plugin -Destination $stagePath
    Copy-Item -LiteralPath $configuration -Destination $stagePath
    Copy-Item -LiteralPath $projectLicense -Destination (Join-Path $stagePath 'LICENSE.txt')
    # MinHook is compiled into the plugin, so its notice ships in both archives.
    Copy-Item -LiteralPath $thirdPartyNotice -Destination (Join-Path $stagePath 'THIRD-PARTY-NOTICES.txt')

    $releaseReadme = (Get-Content -LiteralPath $readmeTemplate -Raw).Replace('@VERSION@', $Version)
    Set-Content -LiteralPath (Join-Path $stagePath 'README.txt') -Value $releaseReadme -Encoding UTF8

    if ($IncludeLoader) {
        Copy-Item -LiteralPath $ResolvedLoader -Destination (Join-Path $stagePath 'winmm.dll')
    }

    Write-PayloadHashes -Directory $stagePath

    $archive = Join-Path $distDirectory ($Name + '.zip')
    $archiveHash = $archive + '.sha256'
    if ((Test-Path -LiteralPath $archive) -or (Test-Path -LiteralPath $archiveHash)) {
        throw "Release output already exists; refusing to overwrite: $archive"
    }

    Compress-Archive -Path (Join-Path $stagePath '*') -DestinationPath $archive
    $zipHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    $zipHashLine = '{0}  {1}' -f $zipHash.ToLowerInvariant(), [System.IO.Path]::GetFileName($archive)
    Set-Content -LiteralPath $archiveHash -Value $zipHashLine -Encoding Ascii
    Write-Output "created $archive"
}

New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null

New-ReleaseArchive -Name "QuantumBreak-RawInputMouseFix-v$Version-plugin-only" `
    -Stage (Join-Path $stagingRoot 'plugin-only') -IncludeLoader $false -ResolvedLoader ''

if ($LoaderPath) {
    $resolvedLoader = (Resolve-Path -LiteralPath $LoaderPath).Path
    $actualLoaderHash = (Get-FileHash -LiteralPath $resolvedLoader -Algorithm SHA256).Hash
    if ($actualLoaderHash -ne $expectedLoaderHash) {
        throw "Loader hash mismatch: $actualLoaderHash (expected Ultimate ASI Loader $loaderVersion NoPDB x64)"
    }

    New-ReleaseArchive -Name "QuantumBreak-RawInputMouseFix-v$Version-with-Ultimate-ASI-Loader-$loaderVersion" `
        -Stage (Join-Path $stagingRoot 'with-loader') -IncludeLoader $true -ResolvedLoader $resolvedLoader
}
