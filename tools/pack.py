import io, pathlib, subprocess, tarfile

root = pathlib.Path(__file__).resolve().parent.parent
repos = {"framey": root.parent / "Framey", "frame-fan": root.parent / "frame-fan"}
out = root / "generated"
out.mkdir(exist_ok=True)


def inc(name, data):
    (out / name).write_text(",".join(str(b) for b in data) + "\n")


buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w:gz", compresslevel=9) as dst:
    for name, path in repos.items():
        sha = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        tree = subprocess.run(["git", "-C", str(path), "archive", "--format=tar", "--prefix=" + name + "/", "HEAD"], capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(tree)) as src:
            for member in src:
                dst.addfile(member, src.extractfile(member) if member.isfile() else None)
        info = tarfile.TarInfo(name + "/.version")
        info.size = len(sha)
        info.mode = 0o644
        dst.addfile(info, io.BytesIO(sha.encode()))
        print(name, sha)
inc("payload.inc", buf.getvalue())
inc("ui.inc", (root / "src" / "ui.html").read_bytes())
print("payload", len(buf.getvalue()), "bytes")
