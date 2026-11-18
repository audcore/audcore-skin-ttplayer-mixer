"""Build the single-file x64 skin with a serial, below-normal compiler job."""

import argparse
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import re
import subprocess
import sys


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CREATE_NO_WINDOW = 0x08000000
MEMORY_LIMIT = 1536 * 1024 * 1024


class BasicLimits(c.Structure):
    _fields_ = [
        ("process_time", c.c_int64), ("job_time", c.c_int64), ("flags", w.DWORD),
        ("min_ws", c.c_size_t), ("max_ws", c.c_size_t), ("processes", w.DWORD),
        ("affinity", c.c_size_t), ("priority", w.DWORD), ("scheduling", w.DWORD),
    ]


class ExtendedLimits(c.Structure):
    _fields_ = [
        ("basic", BasicLimits), ("io", c.c_uint64 * 6),
        ("process_memory", c.c_size_t), ("job_memory", c.c_size_t),
        ("peak_process", c.c_size_t), ("peak_job", c.c_size_t),
    ]


def call(name, result_type, arg_types, *args):
    function = getattr(c.WinDLL("kernel32", use_last_error=True), name)
    function.restype = result_type
    function.argtypes = arg_types
    result = function(*args)
    if not result:
        raise c.WinError(c.get_last_error())
    return result


def compile_skin(output, vcvars, build_tests):
    lock = call("CreateMutexW", w.HANDLE, [c.c_void_p, w.BOOL, w.LPCWSTR],
                None, False, r"Global\Utae.StreamingBuild.Single")
    wait = c.WinDLL("kernel32", use_last_error=True).WaitForSingleObject
    wait.argtypes = [w.HANDLE, w.DWORD]
    wait.restype = w.DWORD
    acquired = False
    try:
        acquired = wait(lock, 0) in (0, 0x80)
        if not acquired:
            raise RuntimeError("全机直播编译互斥锁正在使用")
        running = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command",
             "(Get-Process cargo,rustc,cl,link,rc,cmake,g++,cc1plus,clang-cl -ErrorAction SilentlyContinue).Name; exit 0"],
            creationflags=CREATE_NO_WINDOW, text=True,
        )
        if running.strip():
            raise RuntimeError(f"已有构建进程 {running.strip()}")
        job = call("CreateJobObjectW", w.HANDLE, [c.c_void_p, w.LPCWSTR], None, None)
        try:
            limits = ExtendedLimits()
            # Enforce job-wide committed memory and below-normal priority for
            # the compiler/linker and descendants, including the runner itself.
            limits.basic.flags = 0x200 | 0x20
            limits.basic.priority = 0x4000
            limits.job_memory = MEMORY_LIMIT
            call("SetInformationJobObject", w.BOOL, [w.HANDLE, c.c_int, c.c_void_p, w.DWORD],
                 job, 9, c.byref(limits), c.sizeof(limits))
            process = call("GetCurrentProcess", w.HANDLE, [])
            call("AssignProcessToJobObject", w.BOOL, [w.HANDLE, w.HANDLE], job, process)
            call("SetPriorityClass", w.BOOL, [w.HANDLE, w.DWORD], process, 0x4000)
            output.mkdir(parents=True, exist_ok=True)
            version = re.search(r'VALUE "FileVersion", "([\d.]+)"', (HERE / "skin.rc").read_text()).group(1)
            package = output / f"声声静听-v{version}.skin.audcore"
            # Paths travel via argv/environment. The batch stays ASCII on any
            # Windows console code page, including when the output name is Chinese.
            batch = output / "build-ttplayer.bat"
            env = {**os.environ, "TTPLAYER_VCVARS": str(vcvars), "TTPLAYER_SRC": str(HERE),
                   "TTPLAYER_ROOT": str(ROOT), "CL": "", "_CL_": "", "LINK": "", "_LINK_": ""}
            commands = [
                'call "%TTPLAYER_VCVARS%" >nul',
                'rc /nologo /i "%TTPLAYER_SRC%" /fo skin.res "%TTPLAYER_SRC%\\skin.rc"',
                'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /O1 /MT /LD /experimental:deterministic '
                '"/pathmap:%TTPLAYER_ROOT%=." "%TTPLAYER_SRC%\\ttplayer_skin.cpp" '
                '"%TTPLAYER_SRC%\\window_drag.cpp" skin.res /Fe:ttplayer.dll '
                '/link /INCREMENTAL:NO user32.lib gdi32.lib msimg32.lib comctl32.lib',
            ]
            if build_tests:
                commands.append(
                    'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /O1 /MT '
                    '"%TTPLAYER_SRC%\\ttplayer_test.cpp" "%TTPLAYER_SRC%\\window_drag.cpp" /Fe:ttplayer-test.exe '
                    '/link /INCREMENTAL:NO user32.lib gdi32.lib msimg32.lib comctl32.lib'
                )
            batch.write_text("@echo off\n" + "\n".join(command + " || exit /b 1" for command in commands),
                             encoding="ascii")
            compiler = subprocess.run(["cmd.exe", "/d", "/c", batch.name], cwd=output, env=env,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      creationflags=CREATE_NO_WINDOW)
            print(compiler.stdout.decode("oem", errors="replace"), end="")
            compiler.check_returncode()
            binary = output / "ttplayer.dll"
            subprocess.run([sys.executable, str(ROOT / "scripts/audit-release.py"), str(binary), "--check"],
                           check=True, creationflags=CREATE_NO_WINDOW)
            os.replace(binary, package)
            call("QueryInformationJobObject", w.BOOL,
                 [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.c_void_p],
                 job, 9, c.byref(limits), c.sizeof(limits), None)
            print(f"{package}\n编译 Job 峰值 {limits.peak_job / 2**20:.1f} MiB / {MEMORY_LIMIT / 2**20:.0f} MiB")
        finally:
            call("CloseHandle", w.BOOL, [w.HANDLE], job)
    finally:
        if acquired:
            call("ReleaseMutex", w.BOOL, [w.HANDLE], lock)
        call("CloseHandle", w.BOOL, [w.HANDLE], lock)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/ttplayer-skin")
    parser.add_argument("--vcvars", type=Path, default=Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"))
    parser.add_argument("--tests", action="store_true")
    args = parser.parse_args()
    compile_skin(args.output.resolve(), args.vcvars.resolve(strict=True), args.tests)
