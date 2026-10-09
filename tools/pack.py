import gzip, io, pathlib, subprocess, sys, tarfile

root = pathlib.Path(__file__).resolve().parent.parent
repos = {"framey": root.parent / "Framey", "frame-fan": root.parent / "frame-fan"}
out = root / "generated"
out.mkdir(exist_ok=True)


def inc(name, data):
    (out / name).write_text(",".join(str(b) for b in data) + "\n")


buf = io.BytesIO()
with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0) as gz, tarfile.open(fileobj=gz, mode="w") as dst:
    for name, path in repos.items():
        sha = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        tree = subprocess.run(["git", "-C", str(path), "archive", "--format=tar", "--prefix=" + name + "/", "HEAD"], capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(tree)) as src:
            for member in src:
                data = src.extractfile(member).read() if member.isfile() else None
                if data is not None and b"\0" not in data:
                    data = data.replace(b"\r\n", b"\n")
                    member.size = len(data)
                if data is not None and member.name.endswith(".sh") and b"\r" in data:
                    sys.exit("bundled shell script contains CR: " + member.name)
                dst.addfile(member, io.BytesIO(data) if data is not None else None)
        info = tarfile.TarInfo(name + "/.version")
        info.size = len(sha)
        info.mode = 0o644
        dst.addfile(info, io.BytesIO(sha.encode()))
        print(name, sha)
inc("payload.inc", buf.getvalue())
print("payload", len(buf.getvalue()), "bytes")
