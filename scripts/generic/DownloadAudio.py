import yt_dlp
from pathlib import Path

output_dir = Path(r"C:\Users\Katsu\source\repos\LuAudio\tests\Audios")
output_dir.mkdir(parents=True, exist_ok=True)

options = {
    "format": "bestaudio/best",
    "outtmpl": str(output_dir / "%(title)s.%(ext)s"),
    "postprocessors": [
        {
            "key": "FFmpegExtractAudio",
            "preferredcodec": "mp3",
        }
    ],
    "js_runtimes": {
        "deno": {}
    },
}

with yt_dlp.YoutubeDL(options) as ydl:
    ydl.download(["https://youtu.be/sjYD-OUZ1t8?list=RDujZSQJW-3-E"])