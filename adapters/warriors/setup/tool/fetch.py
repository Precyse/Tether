"""Download with resume and a SHA-256 check. Every download lands in its own directory and is used only after the hash matches."""
import os
import urllib.error
import urllib.request

from util import SetupError, file_hash, say

BLOCK = 1 << 20
PROGRESS_STEP = 10 << 20
TIMEOUT_SECONDS = 60
RETRIES = 4
RANGE_NOT_SATISFIABLE = 416


def download(url, directory, sha256, name):
    """Downloads url to directory/name (resuming a .part file) and returns the path once its SHA-256 equals `sha256`.
    With sha256 None the caller checks the content itself (GitHub archives are not byte-stable; the source is checked by tree hash)."""
    os.makedirs(directory, exist_ok=True)
    final, part = os.path.join(directory, name), os.path.join(directory, name + ".part")
    if os.path.exists(final) and (sha256 is None or file_hash(final) == sha256):
        return final
    for attempt in range(1, RETRIES + 1):
        try:
            _transfer(url, part, name)
            break
        except (urllib.error.URLError, OSError) as problem:
            refused = isinstance(problem, urllib.error.HTTPError) and 400 <= problem.code < 500
            if attempt == RETRIES or refused:
                raise SetupError("Could not download %s (%s). Check the internet connection and run warriors-setup again; "
                                 "the download resumes where it stopped." % (url, problem))
            say("      download interrupted (%s), retrying %d/%d" % (problem, attempt, RETRIES - 1))
    if sha256 is not None and file_hash(part) != sha256:
        os.remove(part)
        raise SetupError("The file downloaded from %s does not match its expected SHA-256 and was deleted. "
                         "Run warriors-setup again; if it repeats, the download is being altered on the way." % url)
    os.replace(part, final)
    return final


def _transfer(url, part, name):
    have = os.path.getsize(part) if os.path.exists(part) else 0
    request = urllib.request.Request(url, headers={"Range": "bytes=%d-" % have} if have else {})
    try:
        response = urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS)
    except urllib.error.HTTPError as problem:
        if problem.code == RANGE_NOT_SATISFIABLE and have:
            return
        raise
    with response:
        resumed = response.status == 206
        total = int(response.headers.get("Content-Length", 0)) + (have if resumed else 0)
        got, shown = have if resumed else 0, 0
        with open(part, "ab" if resumed else "wb") as out:
            for block in iter(lambda: response.read(BLOCK), b""):
                out.write(block)
                got += len(block)
                if got - shown >= PROGRESS_STEP:
                    shown = got
                    say("      %s: %d of %d MB" % (name, got >> 20, total >> 20) if total else "      %s: %d MB" % (name, got >> 20))
