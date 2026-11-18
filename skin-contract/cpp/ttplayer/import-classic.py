"""Import the original artwork used by the Audcore TT.experience skin."""

import argparse
import ctypes
from ctypes import wintypes
from contextlib import contextmanager
import hashlib
from io import BytesIO
from pathlib import Path
import shutil
import struct
import tempfile
from zipfile import ZipFile


CLASSIC_SHA256 = "4a3e33b771d6b98e4d8a63ae69c4720d1f0a169055fcf716c4a846cc5773a563"
ICON_SHA256 = "0c38d4e77e3c9c1d11479c50e8d3e290c7040d15b6ce90552b6a859126716a7c"
ASSETS = (
    "player_skin", "eq_skin", "playlist_skin",
    "open", "minimize", "close", "close2",
    "play", "pause", "stop", "prev", "next", "browser",
    "mute", "playlist", "equalizer", "eq_enabled",
    "eq_profile", "reset", "number", "progress2",
)


def call(name, result_type, arg_types, *args):
    function = getattr(ctypes.WinDLL("kernel32", use_last_error=True), name)
    function.restype = result_type
    function.argtypes = arg_types
    result = function(*args)
    if not result:
        raise ctypes.WinError(ctypes.get_last_error())
    return result

@contextmanager
def resource_module(source):
    handle = wintypes.HANDLE
    # LOAD_LIBRARY_AS_DATAFILE reads the original x86 file as data.
    module = call("LoadLibraryExW", handle, [wintypes.LPCWSTR, handle, wintypes.DWORD], str(source), None, 2)
    try:
        yield module
    finally:
        call("FreeLibrary", wintypes.BOOL, [handle], module)


def resource_pointer(value):
    return ctypes.c_wchar_p(value) if isinstance(value, str) else ctypes.c_void_p(value)


def read_resource(module, name, kind, limit):
    handle = wintypes.HANDLE
    ptr = ctypes.c_void_p
    resource = call("FindResourceW", handle, [handle, ptr, ptr],
                    module, resource_pointer(name), resource_pointer(kind))
    size = call("SizeofResource", wintypes.DWORD, [handle, handle], module, resource)
    if size > limit:
        raise ValueError("原版资源超过预期大小")
    data = call("LoadResource", handle, [handle, handle], module, resource)
    pointer = call("LockResource", ptr, [handle], data)
    return ctypes.string_at(pointer, size)


def application_icon(source):
    with resource_module(source.resolve(strict=True)) as module:
        # The classic player uses icon group 128 as its application default.
        group = read_resource(module, 128, 14, 90)
        if len(group) != 90 or struct.unpack_from("<HHH", group) != (0, 1, 6):
            raise ValueError("需要千千静听 5.7.9 的应用图标")
        images = []
        for index in range(6):
            length, icon_id = struct.unpack_from("<IH", group, 6 + index*14 + 8)
            image = read_resource(module, icon_id, 3, length)
            if len(image) != length:
                raise ValueError("应用图标资源大小不一致")
            images.append((3, icon_id, image))
    if hashlib.sha256(group + b"".join(image for _, _, image in images)).hexdigest() != ICON_SHA256:
        raise ValueError("图标资源与千千静听 5.7.9 的应用图标不一致")
    return [(14, "CLASSIC_APP_ICON", group), *images]


def import_classic(source, base, output, icon_source=None):
    source = source.resolve(strict=True)
    if source.suffix.lower() == ".skn":
        with source.open("rb") as file:
            payload = file.read(130106)
    else:
        with resource_module(source) as module:
            payload = read_resource(module, "<DEFAULT_SKIN>", "ZIP", 130105)
    if hashlib.sha256(payload).hexdigest() != CLASSIC_SHA256:
        raise ValueError("资源与千千静听 5.7.9 的 TT.experience 默认皮肤不一致")

    base = base.resolve(strict=True)
    output = output.resolve()
    for path in (base, output):
        if not path.name.lower().endswith(".skin.audcore"):
            raise ValueError("输入及输出文件名必须以 .skin.audcore 结尾")
    if output.exists():
        raise FileExistsError(output)
    with ZipFile(BytesIO(payload)) as archive:
        assets = [(10, f"CLASSIC_{name.upper()}", archive.read(name + ".bmp")) for name in ASSETS]
    sibling = source.with_name("TTPlayer.exe")
    if icon_source is None and sibling.is_file():
        icon_source = sibling
    if icon_source is not None:
        assets.extend(application_icon(icon_source))
    handle = wintypes.HANDLE
    with tempfile.TemporaryDirectory(prefix=".ttplayer-", dir=output.parent) as temporary:
        stage = Path(temporary) / base.name
        shutil.copyfile(base, stage)
        update = call("BeginUpdateResourceW", handle, [wintypes.LPCWSTR, wintypes.BOOL], str(stage), False)
        discard = True
        try:
            for kind, name, data in assets:
                call("UpdateResourceW", wintypes.BOOL,
                     [handle, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD, ctypes.c_char_p, wintypes.DWORD],
                     update, resource_pointer(kind), resource_pointer(name), 0, data, len(data))
            discard = False
        finally:
            call("EndUpdateResourceW", wintypes.BOOL, [handle, wintypes.BOOL], update, discard)
        stage.rename(output)
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="原版 ttpres.dll 或 DEFAULT_SKIN_579.skn")
    parser.add_argument("base", type=Path, help="未导入原版位图的千千静听 .skin.audcore")
    parser.add_argument("output", type=Path, help="本机使用的 .skin.audcore 副本")
    parser.add_argument("--icon-source", type=Path, help="原版 TTPlayer.exe，默认检查素材同目录")
    args = parser.parse_args()
    try:
        print(import_classic(args.source, args.base, args.output, args.icon_source))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
