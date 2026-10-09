import io, pathlib, subprocess, tarfile

root = pathlib.Path(__file__).resolve().parent.parent
repos = {"framey": str(root.parent / "Framey"), "frame-fan": str(root.parent / "frame-fan")}
out = root / "payload"
out.mkdir(exist_ok=True)
with tarfile.open(out / "payload.tar", "w") as dst:
    for name, path in repos.items():
        sha = subprocess.run(["git", "-C", path, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        tree = subprocess.run(["git", "-C", path, "archive", "--format=tar", "--prefix=" + name + "/", "HEAD"], capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(tree)) as src:
            for member in src:
                dst.addfile(member, src.extractfile(member) if member.isfile() else None)
        info = tarfile.TarInfo(name + "/.version")
        info.size = len(sha)
        info.mode = 0o644
        dst.addfile(info, io.BytesIO(sha.encode()))
        print(name, sha)
print("payload", (out / "payload.tar").stat().st_size, "bytes")
