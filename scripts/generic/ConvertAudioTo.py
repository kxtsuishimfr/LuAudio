import sys
import subprocess
from pathlib import Path

if len(sys.argv) != 2:
    print(f"Usage: python {Path(sys.argv[0]).name} <audio_file>")
    sys.exit(1)

input_file = Path(sys.argv[1])

if not input_file.is_file():
    print(f"File not found: {input_file}")
    sys.exit(1)

output_file = input_file.with_suffix(".ogg")

subprocess.run([
    "ffmpeg",
    "-i", str(input_file),
    "-c:a", "libvorbis",
    "-q:a", "6",
    str(output_file)
], check=True)

print(f"Converted: {output_file}")