"""Check Android movies and compare the APK with the verified staged container and native binary.
Usage: python Tools/verify_reliability_apk.py path/to/app.apk
"""
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import zipfile


class ZipSlice(io.RawIOBase):
    def __init__(self, path, start, size):
        self.file = open(path, "rb")
        self.start, self.size, self.position = start, size, 0

    def seekable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=0):
        self.position = offset if whence == 0 else self.position + offset if whence == 1 else self.size + offset
        if self.position < 0:
            raise ValueError("Negative seek")
        return self.position

    def read(self, size=-1):
        self.file.seek(self.start + self.position)
        data = self.file.read(max(0, self.size - self.position) if size < 0 else min(size, max(0, self.size - self.position)))
        self.position += len(data)
        return data

    def close(self):
        self.file.close()
        super().close()


def digest(stream):
    value = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        value.update(block)
    return value.hexdigest()


def verify(apk):
    root = Path(__file__).resolve().parents[1]
    expected = list((root / "Content" / "Movies").glob("*.mp4"))
    assert len(expected) == 2, "Expected exactly two required training movies"
    result = {"apk": str(apk.resolve()), "movies": []}
    with zipfile.ZipFile(apk) as archive:
        obb = [entry for entry in archive.infolist() if entry.filename.startswith("assets/") and "obb" in entry.filename]
        assert len(obb) == 1, "Expected one embedded OBB"
        entry = obb[0]
        assert entry.compress_type == zipfile.ZIP_STORED, "Embedded OBB must be stored for offset-based Android access"
        with open(apk, "rb") as file:
            file.seek(entry.header_offset)
            header = file.read(30)
            assert header[:4] == b"PK\x03\x04"
            name_size, extra_size = struct.unpack_from("<HH", header, 26)
        with ZipSlice(apk, entry.header_offset + 30 + name_size + extra_size, entry.file_size) as view:
            with zipfile.ZipFile(view) as assets:
                for movie in expected:
                    matches = [item for item in assets.infolist() if item.filename.replace("\\", "/").endswith("/Content/Movies/" + movie.name)]
                    assert len(matches) == 1, f"Missing or duplicate required movie: {movie.name}"
                    item = matches[0]
                    assert item.compress_type == zipfile.ZIP_STORED, f"Movie must remain seekable: {movie.name}"
                    with assets.open(item) as packaged, open(movie, "rb") as source:
                        packaged_hash, source_hash = digest(packaged), digest(source)
                    assert packaged_hash == source_hash, f"Movie bytes changed: {movie.name}"
                    result["movies"].append({"entry": item.filename, "bytes": item.file_size, "sha256": packaged_hash})
                staged = root / "Saved/StagedBuilds/Android_ASTC/CNABedToWheelchair/Content/Paks"
                result["containers"] = []
                for file in sorted(staged.glob("*")):
                    if file.suffix not in (".pak", ".utoc", ".ucas"):
                        continue
                    name = "CNABedToWheelchair/Content/Paks/" + file.name
                    with assets.open(name) as packaged, open(file, "rb") as source:
                        packaged_hash, source_hash = digest(packaged), digest(source)
                    assert packaged_hash == source_hash, f"APK container differs from staged container: {file.name}"
                    result["containers"].append({"entry": name, "sha256": packaged_hash})
                assert result["containers"], "Staged containers not found"
                listing = (root / "Saved/Reliability/AndroidContainer.csv").read_text()
                result["input_assets"] = ["IA_UIInteract_Left", "IA_UIInteract_Right", "IMC_UIInteract"]
                for name in result["input_assets"]:
                    assert name + ".uasset" in listing, f"Missing cooked input asset: {name}"
        native_files = list((root / "Intermediate/Android/arm64/gradle/app/build/intermediates/stripped_native_libs").glob("**/libUnreal.so"))
        assert len(native_files) == 1, "Expected one final stripped native runtime"
        with archive.open("lib/arm64-v8a/libUnreal.so") as packaged, open(native_files[0], "rb") as source:
            packaged_hash, source_hash = digest(packaged), digest(source)
        assert packaged_hash == source_hash, "APK native runtime differs from final built library"
        result["native_sha256"] = packaged_hash
    with open(apk, "rb") as file:
        result["sha256"] = digest(file)
    print(json.dumps(result, indent=2))
    return result


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
