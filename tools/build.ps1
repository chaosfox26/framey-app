$root = Split-Path -Parent $PSScriptRoot
$vs = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$ninja = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$llvm = 'C:\Program Files\LLVM\bin'
python "$root\tools\pack.py"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$cfg = "cmake -S `"$root`" -B `"$root\build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-rc"
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set PATH=$llvm;$ninja;%PATH% && $cfg && cmake --build `"$root\build`""
exit $LASTEXITCODE
