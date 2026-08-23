from pathlib import Path
import shutil

ROOT = Path(r"C:\Users\Katsu\Downloads\vorbis-main\Vorbis")

# Build/development files we don't need when vendoring Vorbis.
FILES_TO_DELETE = {
    ".gitignore",
    ".gitlab-ci.yml",
    ".travis.yml",
    ".ycm_extra_conf.py",
    "appveyor.yml",
    "autogen.sh",
    "Brewfile",
    "configure.ac",
    "libvorbis.spec.in",
    "Makefile.am",
    "README.md",
    "releases.sha2",
    "vorbis-uninstalled.pc.in",
    "vorbis.m4",
    "vorbis.pc.in",
    "vorbisenc-uninstalled.pc.in",
    "vorbisenc.pc.in",
    "vorbisfile-uninstalled.pc.in",
    "vorbisfile.pc.in",
}

DIRS_TO_DELETE = {
    "contrib",
    "debian",
    "doc",
    "examples",
    "m4",
    "macosx",
    "symbian",
    "test",
    "win32",
}


def delete_file(path: Path):
    if path.exists():
        print(f"DELETE FILE:   {path.name}")
        path.unlink()


def delete_dir(path: Path):
    if path.exists():
        print(f"DELETE FOLDER: {path.name}/")
        shutil.rmtree(path)


for filename in FILES_TO_DELETE:
    delete_file(ROOT / filename)

for dirname in DIRS_TO_DELETE:
    delete_dir(ROOT / dirname)

print("\nDone.")
print("Kept:")
print("  CMakeLists.txt")
print("  AUTHORS")
print("  COPYING")
print("  CHANGES")
print("  cmake/")
print("  include/")
print("  lib/")
print("  vq/")