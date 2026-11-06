#!/usr/bin/env python3
"""Turn your own PC copy's audio into the MP3 files the Vita app plays.

    python tools/convert_audio.py --data "<your copy>/data" --out <folder>

writes <folder>/sfx/*.mp3 and <folder>/music/*.mp3, ready to copy to
ux0:data/CastleCrashers/. Your copy is only read.

The PC game's data/sounds/*.xma and data/music/*.xma aren't Xbox XMA: they're
RIFF xWMA files holding WMA v2 audio, which any normal ffmpeg build can decode.
The game asks for "sound_coin1.wav" or "music/MEDUSA.wav"; the app drops the
extension and opens sfx/sound_coin1.mp3 or music/MEDUSA.mp3 (src/audio.c,
src/cc_host.c). So each file keeps its name and only changes extension.

Needs Python 3 and ffmpeg on the PATH (or give --ffmpeg). Run it again to pick
up where it stopped: finished files are kept unless you pass --force.
"""
import argparse
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

SOURCES = (('sounds', 'sfx'), ('music', 'music'))   # folder in data/, folder on the Vita
INSTALL_HINT = ('Install it first: "winget install Gyan.FFmpeg" on Windows, '
                '"sudo apt install ffmpeg" on Debian or Ubuntu, "brew install ffmpeg" on macOS. '
                'Or point --ffmpeg at the program.')


def find_jobs(data, out, force):
    """Lists (source, target) for every .xma file; also returns how many were already done."""
    jobs, done = [], 0
    for src_name, dst_name in SOURCES:
        src_dir = os.path.join(data, src_name)
        if not os.path.isdir(src_dir):
            sys.exit(f'{src_dir} not found. --data should be the "data" folder of your copy.')
        files = sorted(f for f in os.listdir(src_dir) if f.lower().endswith('.xma'))
        if not files:
            sys.exit(f'No .xma files in {src_dir}.')
        os.makedirs(os.path.join(out, dst_name), exist_ok=True)
        for f in files:
            target = os.path.join(out, dst_name, f[:-4] + '.mp3')
            if not force and os.path.exists(target):
                done += 1
            else:
                jobs.append((os.path.join(src_dir, f), target))
    return jobs, done


def convert(ffmpeg, bitrate, src, target):
    """Returns None on success or ffmpeg's error text. Writes to a side file so that an
    interrupted run never leaves a half file that looks finished."""
    part = target[:-4] + '.part.mp3'
    r = subprocess.run([ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'error', '-y',
                        '-i', src, '-vn', '-map_metadata', '-1',
                        '-ar', '44100', '-c:a', 'libmp3lame', '-b:a', bitrate, part],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                       encoding='utf-8', errors='replace')
    if r.returncode == 0 and os.path.exists(part) and os.path.getsize(part) > 0:
        os.replace(part, target)
        return None
    if os.path.exists(part):
        os.remove(part)
    return r.stderr.strip() or f'ffmpeg exited with {r.returncode}'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--data', required=True, help='the "data" folder of your copy of the game')
    ap.add_argument('--out', required=True, help='folder to write sfx/ and music/ into')
    ap.add_argument('--ffmpeg', default='ffmpeg', help='ffmpeg program, if not on the PATH')
    ap.add_argument('--bitrate', default='320k', help='MP3 bitrate (default 320k)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 1, help='files to convert at once')
    ap.add_argument('--force', action='store_true', help='convert files that already exist too')
    args = ap.parse_args()

    ffmpeg = shutil.which(args.ffmpeg)
    if not ffmpeg:
        sys.exit(f'ffmpeg not found. {INSTALL_HINT}')

    jobs, done = find_jobs(args.data, args.out, args.force)
    failed = 0
    with ThreadPoolExecutor(max(args.jobs, 1)) as pool:
        results = pool.map(lambda job: convert(ffmpeg, args.bitrate, *job), jobs)
        for i, ((src, _), error) in enumerate(zip(jobs, results), 1):
            if error:
                failed += 1
                print(f'FAILED {src}: {error}', file=sys.stderr)
            if i % 50 == 0 or i == len(jobs):
                print(f'{i}/{len(jobs)}')

    print(f'{len(jobs) - failed} converted, {done} already there, {failed} failed, in {args.out}')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
